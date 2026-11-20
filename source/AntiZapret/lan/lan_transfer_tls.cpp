#include "lan_transfer_tls.h"
#if defined(ANTIZAPRET_LAN)
#include "version.h"
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#include <ncrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Secur32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Ncrypt.lib")
#pragma comment(lib, "Bcrypt.lib")

namespace lan::transfer
{
namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::uint32_t LanProtocolRevision = 3;
constexpr std::size_t MaxRecord = 20 * 1024, MaxHandshake = 64 * 1024;

[[noreturn]] void Fail(const char* message, long code = 0)
{
    throw std::runtime_error(std::string(message) + " (" + std::to_string(code) + ")");
}
struct SocketOwner
{
    SOCKET value;
    ~SocketOwner() { if (value != INVALID_SOCKET) closesocket(value); }
};
struct Token
{
    SecBuffer buffer{0, SECBUFFER_TOKEN, nullptr};
    ~Token() { if (buffer.pvBuffer) FreeContextBuffer(buffer.pvBuffer); }
};
struct Cert
{
    NCRYPT_PROV_HANDLE provider{};
    NCRYPT_KEY_HANDLE key{};
    PCCERT_CONTEXT context{};
    std::wstring keyName;
    ~Cert()
    {
        if (context) CertFreeCertificateContext(context);
        if (key && NCryptDeleteKey(key, 0) != ERROR_SUCCESS) NCryptFreeObject(key);
        if (provider) NCryptFreeObject(provider);
    }
    void Create()
    {
        auto result = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
        if (result) Fail("Open ephemeral key provider", result);
        // Schannel's isolated key service requires a named key. Use a new,
        // non-exportable, current-user CNG key for each session (no cert store).
        std::array<unsigned char, 16> nonce{};
        result = BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (result) Fail("Generate session key name", result);
        constexpr wchar_t hex[] = L"0123456789ABCDEF";
        keyName = L"AntiZapret LAN transfer ";
        for (auto byte : nonce) { keyName += hex[byte >> 4]; keyName += hex[byte & 15]; }
        result = NCryptCreatePersistedKey(provider, &key, NCRYPT_ECDSA_P256_ALGORITHM, keyName.c_str(), 0, 0);
        if (result) Fail("Create session certificate key", result);
        result = NCryptFinalizeKey(key, 0);
        if (result) Fail("Finalize certificate key", result);
        std::array<BYTE, 256> dn{};
        DWORD size = static_cast<DWORD>(dn.size());
        if (!CertStrToNameW(X509_ASN_ENCODING, L"CN=AntiZapret LAN Transfer", CERT_X500_NAME_STR,
                           nullptr, dn.data(), &size, nullptr)) Fail("Certificate subject", GetLastError());
        CERT_NAME_BLOB name{size, dn.data()};
        CRYPT_ALGORITHM_IDENTIFIER alg{const_cast<LPSTR>(szOID_ECDSA_SHA256), {0, nullptr}};
        CRYPT_KEY_PROV_INFO info{};
        info.pwszContainerName = keyName.data();
        info.pwszProvName = const_cast<LPWSTR>(MS_KEY_STORAGE_PROVIDER);
        info.dwKeySpec = 0; // CNG provider, not a legacy AT_* key.
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER time{};
        time.HighPart = ft.dwHighDateTime; time.LowPart = ft.dwLowDateTime;
        time.QuadPart += 3600ULL * 10000000;
        ft.dwHighDateTime = time.HighPart; ft.dwLowDateTime = time.LowPart;
        SYSTEMTIME expiry{};
        if (!FileTimeToSystemTime(&ft, &expiry)) Fail("Certificate expiry", GetLastError());
        context = CertCreateSelfSignCertificate(0, &name, 0, &info, &alg, nullptr, &expiry, nullptr);
        if (!context) Fail("Create self-signed certificate", GetLastError());
    }
};
}

struct TlsChannel::Impl
{
    SocketOwner socket;
    Cert cert;
    CredHandle cred{};
    CtxtHandle context{};
    SecPkgContext_StreamSizes sizes{};
    const std::atomic_bool& cancelled;
    std::function<bool()> permitted;
    bool server, confirmed = false, confirmAttempted = false, transferStarted = false;
    std::vector<unsigned char> encrypted, plain;
    std::size_t consumed = 0;

    Impl(SocketOwner& owner, bool isServer, const std::atomic_bool& c, std::function<bool()> check)
        : socket{owner.value}, cancelled(c), permitted(std::move(check)), server(isServer)
    {
        owner.value = INVALID_SOCKET;
        SecInvalidateHandle(&cred);
        SecInvalidateHandle(&context);
    }
    ~Impl()
    {
        if (SecIsValidHandle(&context)) DeleteSecurityContext(&context);
        if (SecIsValidHandle(&cred)) FreeCredentialsHandle(&cred);
    }
    void Check() const { if (cancelled.load() || (permitted && !permitted())) Fail("Transfer cancelled or LAN path expired"); }
    void Wait(bool writing, Clock::time_point deadline)
    {
        while (true)
        {
            Check();
            if (Clock::now() >= deadline) Fail("Transfer timed out");
            fd_set ready;
            FD_ZERO(&ready);
            FD_SET(socket.value, &ready);
            timeval slice{0, 100000};
            const auto result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, nullptr, &slice);
            if (result > 0)
            {
                Check();
                if (Clock::now() >= deadline) Fail("Transfer timed out");
                return;
            }
            if (result == SOCKET_ERROR) Fail("Socket select", WSAGetLastError());
        }
    }
    void SendRaw(const void* ptr, std::size_t size, Clock::time_point deadline)
    {
        auto data = static_cast<const char*>(ptr);
        while (size)
        {
            Wait(true, deadline);
            int n = send(socket.value, data, static_cast<int>(size), 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
            if (n <= 0) Fail("Socket send", WSAGetLastError());
            data += n;
            size -= static_cast<std::size_t>(n);
        }
    }
    void ReceiveRaw(std::size_t limit, Clock::time_point deadline)
    {
        if (encrypted.size() >= limit) Fail("TLS input limit exceeded");
        std::array<char, 4096> buffer{};
        const auto capacity = (std::min)(buffer.size(), limit - encrypted.size());
        while (true)
        {
            Wait(false, deadline);
            int n = recv(socket.value, buffer.data(), static_cast<int>(capacity), 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
            if (n <= 0) Fail("Socket closed before completion", n ? WSAGetLastError() : 0);
            encrypted.insert(encrypted.end(), buffer.data(), buffer.data() + n);
            return;
        }
    }
    void RetainExtra(const SecBuffer& extra)
    {
        const std::size_t bytes = extra.BufferType == SECBUFFER_EXTRA ? extra.cbBuffer : 0;
        if (bytes > encrypted.size()) Fail("Invalid TLS extra length");
        encrypted.erase(encrypted.begin(), encrypted.end() - bytes);
    }
    void Handshake()
    {
        u_long nonblocking = 1;
        if (ioctlsocket(socket.value, FIONBIO, &nonblocking)) Fail("Set nonblocking socket", WSAGetLastError());
        SCHANNEL_CRED cfg{};
        cfg.dwVersion = SCHANNEL_CRED_VERSION;
        cfg.grbitEnabledProtocols = server ? SP_PROT_TLS1_2_SERVER : SP_PROT_TLS1_2_CLIENT;
        cfg.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_DISABLE_RECONNECTS;
        if (server)
        {
            cert.Create();
            cfg.cCreds = 1;
            cfg.paCred = &cert.context;
        }
        else cfg.dwFlags |= SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS;
        auto status = AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W),
            server ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND, nullptr, &cfg, nullptr, nullptr, &cred, nullptr);
        if (status != SEC_E_OK) Fail("Acquire Schannel credentials", status);
        const auto deadline = Clock::now() + 15s;
        if (server) ReceiveRaw(MaxHandshake, deadline);
        for (unsigned i = 0; i != 128; ++i)
        {
            Token out;
            unsigned long attrs = 0;
            SecBuffer input[] = {{static_cast<unsigned long>(encrypted.size()), SECBUFFER_TOKEN, encrypted.data()},
                                 {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc inDesc{SECBUFFER_VERSION, 2, input}, outDesc{SECBUFFER_VERSION, 1, &out.buffer};
            CtxtHandle* prior = SecIsValidHandle(&context) ? &context : nullptr;
            if (server)
            {
                constexpr unsigned long flags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
                    ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;
                status = AcceptSecurityContext(&cred, prior, &inDesc, flags, SECURITY_NATIVE_DREP,
                                               &context, &outDesc, &attrs, nullptr);
            }
            else
            {
                constexpr unsigned long flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                    ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
                status = InitializeSecurityContextW(&cred, prior, nullptr, flags, 0, SECURITY_NATIVE_DREP,
                    prior ? &inDesc : nullptr, 0, &context, &outDesc, &attrs, nullptr);
            }
            if (status == SEC_E_INCOMPLETE_MESSAGE) { ReceiveRaw(MaxHandshake, deadline); continue; }
            if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) Fail("TLS handshake failed", status);
            if (out.buffer.cbBuffer) SendRaw(out.buffer.pvBuffer, out.buffer.cbBuffer, deadline);
            RetainExtra(input[1]);
            if (status == SEC_E_OK)
            {
                if (!(attrs & (server ? ASC_RET_CONFIDENTIALITY : ISC_RET_CONFIDENTIALITY)))
                    Fail("TLS confidentiality unavailable");
                SecPkgContext_CipherInfo cipher{};
                cipher.dwVersion = SECPKGCONTEXT_CIPHERINFO_V1;
                status = QueryContextAttributesW(&context, SECPKG_ATTR_CIPHER_INFO, &cipher);
                if (status != SEC_E_OK ||
                    (std::wcscmp(cipher.szCipherSuite, L"TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256") != 0 &&
                     std::wcscmp(cipher.szCipherSuite, L"TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384") != 0))
                    Fail("TLS cipher suite rejected", status);
                status = QueryContextAttributesW(&context, SECPKG_ATTR_STREAM_SIZES, &sizes);
                if (status != SEC_E_OK) Fail("TLS stream sizes", status);
                const auto overhead = static_cast<std::size_t>(sizes.cbHeader) + sizes.cbTrailer;
                if (!sizes.cbMaximumMessage || overhead + sizes.cbMaximumMessage > MaxRecord)
                    Fail("Unexpected TLS record sizes");
                return;
            }
            if (encrypted.empty()) ReceiveRaw(MaxHandshake, deadline);
        }
        Fail("TLS handshake iteration limit exceeded");
    }
    std::string Code()
    {
        SecPkgContext_Bindings binding{};
        auto status = QueryContextAttributesW(&context, SECPKG_ATTR_UNIQUE_BINDINGS, &binding);
        struct Owner { SEC_CHANNEL_BINDINGS* p; ~Owner() { if (p) FreeContextBuffer(p); } } owner{binding.Bindings};
        if (status != SEC_E_OK || !owner.p || binding.BindingsLength < sizeof(*owner.p))
            Fail("TLS channel binding unavailable", status);
        const auto length = owner.p->cbApplicationDataLength;
        const auto offset = owner.p->dwApplicationDataOffset;
        constexpr char prefix[] = "tls-unique:";
        constexpr std::size_t prefixLen = sizeof(prefix) - 1;
        if (offset < sizeof(*owner.p) || offset > binding.BindingsLength ||
            length > binding.BindingsLength - offset || length != prefixLen + 12)
            Fail("Unexpected TLS 1.2 channel binding size");
        const auto bytes = reinterpret_cast<const unsigned char*>(owner.p) + offset;
        if (std::memcmp(bytes, prefix, prefixLen) != 0) Fail("Unexpected TLS binding type");
        constexpr char hex[] = "0123456789ABCDEF";
        std::string code;
        for (std::size_t i = 0; i < 12; ++i)
        {
            if (i && i % 2 == 0) code += '-';
            const auto value = bytes[prefixLen + i];
            code += hex[value >> 4];
            code += hex[value & 15];
        }
        return code;
    }
    void Write(const void* data, std::size_t size, Clock::time_point deadline)
    {
        auto ptr = static_cast<const unsigned char*>(data);
        std::array<unsigned char, MaxRecord> record{};
        while (size)
        {
            Check();
            const auto chunk = static_cast<unsigned long>((std::min)(size, static_cast<std::size_t>(sizes.cbMaximumMessage)));
            std::memcpy(record.data() + sizes.cbHeader, ptr, chunk);
            SecBuffer parts[] = {{sizes.cbHeader, SECBUFFER_STREAM_HEADER, record.data()},
                {chunk, SECBUFFER_DATA, record.data() + sizes.cbHeader},
                {sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, record.data() + sizes.cbHeader + chunk},
                {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc desc{SECBUFFER_VERSION, 4, parts};
            auto status = EncryptMessage(&context, 0, &desc, 0);
            if (status != SEC_E_OK) Fail("TLS encrypt", status);
            for (const auto& part : parts) if (part.cbBuffer) SendRaw(part.pvBuffer, part.cbBuffer, deadline);
            ptr += chunk;
            size -= chunk;
        }
    }
    void Read(void* data, std::size_t size, Clock::time_point deadline)
    {
        auto ptr = static_cast<unsigned char*>(data);
        while (size)
        {
            Check();
            if (Clock::now() >= deadline) Fail("Transfer timed out");
            if (consumed < plain.size())
            {
                const auto chunk = (std::min)(size, plain.size() - consumed);
                std::memcpy(ptr, plain.data() + consumed, chunk);
                consumed += chunk;
                size -= chunk;
                ptr += chunk;
                continue;
            }
            if (encrypted.empty()) ReceiveRaw(MaxRecord, deadline);
            SecBuffer parts[] = {{static_cast<unsigned long>(encrypted.size()), SECBUFFER_DATA, encrypted.data()},
                {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc desc{SECBUFFER_VERSION, 4, parts};
            const auto status = DecryptMessage(&context, &desc, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) { ReceiveRaw(MaxRecord, deadline); continue; }
            if (status != SEC_E_OK) Fail("TLS record rejected or channel closed", status);
            plain.clear();
            consumed = 0;
            const SecBuffer* extra = nullptr;
            for (const auto& part : parts)
            {
                if (part.BufferType == SECBUFFER_DATA && part.cbBuffer)
                {
                    const auto start = static_cast<const unsigned char*>(part.pvBuffer);
                    plain.assign(start, start + part.cbBuffer);
                }
                if (part.BufferType == SECBUFFER_EXTRA) extra = &part;
            }
            if (extra) RetainExtra(*extra); else encrypted.clear();
        }
    }
};

TlsChannel::TlsChannel(SOCKET socket, bool server, const std::atomic_bool& cancel, std::function<bool()> permitted)
{
    SocketOwner owner{socket};
    impl = std::make_unique<Impl>(owner, server, cancel, std::move(permitted));
    impl->Handshake();
}
TlsChannel::~TlsChannel() = default;
bool TlsChannel::Confirm(Verify verify, void* context)
{
    return ConfirmImpl(verify, context, ANTIZAPRET_VERSION, LanProtocolRevision);
}
#ifdef ANTIZAPRET_LAN_TEST
bool TlsChannel::ConfirmForTest(Verify verify, void* context, std::string_view version, std::uint32_t revision)
{
    return ConfirmImpl(verify, context, version, revision);
}
#endif
bool TlsChannel::ConfirmImpl(Verify verify, void* context, std::string_view version, std::uint32_t revision)
{
    if (!verify || impl->confirmAttempted) Fail("Invalid confirmation state");
    impl->confirmAttempted = true;
    const std::string code = impl->Code();
    const unsigned char local = verify(context, code) ? 1 : 0;
    unsigned char remote = 0;
    const auto deadline = Clock::now() + 120s;
    impl->Write(&local, 1, deadline);
    if (!local) return false;
    impl->Read(&remote, 1, deadline);
    if (remote > 1) Fail("Invalid peer confirmation");
    if (!remote) return false;
#ifdef ANTIZAPRET_LAN_TEST
    // Standalone harness can emulate pre-compatibility peers; not a production path.
    if (!revision) return impl->confirmed = true;
#endif
    static constexpr std::size_t VersionLimit = 64;
    const auto valid = [](std::string_view value) {
        return !value.empty() && value.size() <= VersionLimit &&
            std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 33 && c <= 126; });
    };
    if (!valid(version) || !revision) Fail("Invalid local version or LAN revision");
    const auto compatDeadline = Clock::now() + 5s;
    // Fresh, encrypted, and compared byte-for-byte, including any suffix.
    const std::array<unsigned char, 9> header{'A', 'Z', 'V', '1',
        static_cast<unsigned char>(revision >> 24), static_cast<unsigned char>(revision >> 16),
        static_cast<unsigned char>(revision >> 8), static_cast<unsigned char>(revision),
        static_cast<unsigned char>(version.size())};
    impl->Write(header.data(), header.size(), compatDeadline);
    impl->Write(version.data(), version.size(), compatDeadline);
    std::array<unsigned char, header.size()> peer{};
    impl->Read(peer.data(), peer.size(), compatDeadline);
    if (!std::equal(header.begin(), header.begin() + 4, peer.begin()) || !peer.back() || peer.back() > VersionLimit)
        Fail("Unknown peer compatibility protocol");
    std::string peerVersion(peer.back(), char(0));
    impl->Read(peerVersion.data(), peerVersion.size(), compatDeadline);
    if (!std::equal(header.begin() + 4, header.begin() + 8, peer.begin() + 4)) Fail("Peer LAN revision mismatch");
    if (version != peerVersion) Fail("Peer application version mismatch");
    impl->confirmed = true;
    return true;
}
void TlsChannel::BeginTransfer(bool receiver, bool speed)
{
    if (!impl->confirmed || impl->transferStarted || receiver != impl->server) Fail("Invalid transfer state");
    impl->Check();
    impl->transferStarted = true;
    const unsigned char op = speed ? 0x53 : 0x46;
    unsigned char peer = 0;
    const auto deadline = Clock::now() + 5s;
    impl->Write(&op, 1, deadline);
    impl->Read(&peer, 1, deadline);
    if (peer != op) Fail("Peer operation mismatch");
}
void TlsChannel::CheckCancelled() const { impl->Check(); }
void TlsChannel::Read(void* data, std::size_t size) { ReadUntil(data, size, Clock::now() + 30s); }
void TlsChannel::Write(const void* data, std::size_t size) { WriteUntil(data, size, Clock::now() + 30s); }
void TlsChannel::ReadUntil(void* data, std::size_t size, Deadline deadline)
{
    if (!impl->confirmed) Fail("Unconfirmed TLS channel");
    impl->Read(data, size, deadline);
}
void TlsChannel::WriteUntil(const void* data, std::size_t size, Deadline deadline)
{
    if (!impl->confirmed) Fail("Unconfirmed TLS channel");
    impl->Write(data, size, deadline);
}
}
#endif
