#include "lan/lan_transfer.h"
#include "lan/lan_speed.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <windows.h>
#include <stdexcept>
#include <thread>
#include <vector>

using lan::transfer::TlsChannel;

void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Socket
{
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
    SOCKET Release() { const auto result = value; value = INVALID_SOCKET; return result; }
};
struct Pair
{
    Socket client{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)}, server;
    Pair()
    {
        Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        Require(client.value != INVALID_SOCKET && listener.value != INVALID_SOCKET, "Cannot create test sockets");
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(addr);
        Require(bind(listener.value, reinterpret_cast<sockaddr*>(&addr), length) == 0 &&
                listen(listener.value, 1) == 0 &&
                getsockname(listener.value, reinterpret_cast<sockaddr*>(&addr), &length) == 0 &&
                connect(client.value, reinterpret_cast<sockaddr*>(&addr), length) == 0, "Loopback pair");
        server.value = accept(listener.value, nullptr, nullptr);
        Require(server.value != INVALID_SOCKET, "Loopback accept");
    }
};

struct Decision { bool yes; std::string code; };
bool Verify(void* p, const std::string& code)
{
    auto& d = *static_cast<Decision*>(p);
    d.code = code;
    return d.yes;
}
void Test(bool accept, bool cancel, std::string& previousCode)
{
    Pair pair;
    Decision client{true, {}}, server{accept, {}};
    std::atomic_bool cancelClient{false}, cancelServer{false};
    std::exception_ptr clientError, serverError;
    std::vector<unsigned char> sent(180000), got(sent.size());
    for (size_t i = 0; i < sent.size(); ++i) sent[i] = static_cast<unsigned char>(i * 13);
    std::thread worker([&] {
        try
        {
            TlsChannel ch(pair.server.Release(), true, cancelServer);
            char dummy{};
            bool blocked = false;
            try { ch.Read(&dummy, 1); } catch (const std::runtime_error&) { blocked = true; }
            Require(blocked, "Preconfirmation read must fail");
            Require(ch.Confirm(Verify, &server) == accept, "Server confirmation result");
            if (accept && !cancel) ch.Read(got.data(), got.size());
        }
        catch (...) { serverError = std::current_exception(); }
    });
    try
    {
        TlsChannel ch(pair.client.Release(), false, cancelClient);
        bool blocked = false;
        try { ch.Write(sent.data(), sent.size()); } catch (const std::runtime_error&) { blocked = true; }
        Require(blocked, "Preconfirmation write must fail");
        Require(ch.Confirm(Verify, &client) == accept, "Client confirmation result");
        if (cancel) cancelClient = true;
        blocked = false;
        try { ch.Write(sent.data(), sent.size()); } catch (const std::runtime_error&) { blocked = true; }
        Require(blocked == (!accept || cancel), "Write result after decision");
    }
    catch (...) { clientError = std::current_exception(); }
    worker.join();
    if (serverError) std::rethrow_exception(serverError);
    if (clientError) std::rethrow_exception(clientError);
    Require(client.code == server.code && client.code.size() == 29 && client.code != previousCode,
            "Both codes match only within their session");
    if (accept && !cancel) Require(sent == got, "Bytes must survive all TLS records");
    previousCode = client.code;
}
namespace fs = std::filesystem;
using namespace lan::transfer;
struct Both
{
    std::optional<Result> sender, receiver;
    std::exception_ptr senderError, receiverError;
};
Both Session(const std::function<Result(TlsChannel&)>& send, const std::function<Result(TlsChannel&)>& receive,
             const std::atomic_bool& senderCancel, const std::atomic_bool& receiverCancel)
{
    Pair pair;
    Decision client{true, {}}, server{true, {}};
    Both out;
    std::thread worker([&] {
        try
        {
            TlsChannel ch(pair.server.Release(), true, receiverCancel);
            Require(ch.Confirm(Verify, &server), "Receiver confirmed");
            out.receiver = receive(ch);
        }
        catch (...) { out.receiverError = std::current_exception(); }
    });
    try
    {
        TlsChannel ch(pair.client.Release(), false, senderCancel);
        Require(ch.Confirm(Verify, &client), "Sender confirmed");
        out.sender = send(ch);
    }
    catch (...) { out.senderError = std::current_exception(); }
    worker.join();
    Require(client.code == server.code && !client.code.empty(), "Per-session comparison code");
    return out;
}
void Expect(const Both& both, Result value)
{
    for (const auto& error : {both.senderError, both.receiverError})
        if (error) try { std::rethrow_exception(error); } catch (const std::exception& e) { puts(e.what()); }
    if (both.receiverError) std::rethrow_exception(both.receiverError);
    if (both.senderError) std::rethrow_exception(both.senderError);
    Require(both.sender == value && both.receiver == value, "Both endpoints must agree on result");
}
void ValidateTests()
{
    Require(ValidateManifest({{"Папка", true, 0}, {"Папка/файл.txt", false, 99}}) == 99, "Unicode manifest");
    constexpr auto MaxBytes = 64ULL * 1024 * 1024 * 1024;
    Require(ValidateManifest({{"at-limit", false, MaxBytes}}) == MaxBytes, "Byte boundary");
    std::vector<Item> maxCount;
    for (unsigned i = 0; i < MaxEntries; ++i) maxCount.push_back({std::to_string(i), true, 0});
    Require(ValidateManifest(maxCount) == 0, "Count boundary");
    maxCount.push_back({"overflow", true, 0});
    const std::vector<std::vector<Item>> invalid{{}, maxCount, {{"../evil"}}, {{"/evil"}}, {{"a//b"}},
        {{"a\\evil"}}, {{"C:evil"}}, {{"evil.txt:stream"}}, {{"CON.txt"}}, {{"LPT².ext"}}, {{"evil. "}},
        {{"a/"}}, {{"a", false, MaxBytes + 1}}, {{"a", true, 1}}, {{"a/b"}},
        {{"a"}, {"a/b"}}, {{"a", true}, {"A/b"}}, {{"Папка"}, {"папка"}},
        {{std::string(1, static_cast<char>(0xff))}}, {{std::string("a\0b", 3)}}};
    for (const auto& manifest : invalid)
    {
        bool rejected = false;
        try { ValidateManifest(manifest); } catch (const std::runtime_error&) { rejected = true; }
        Require(rejected, "Invalid manifest must be rejected");
    }
    puts("PASS: UTF-8, traversal, devices, ADS, names, case collisions, parent kinds and limits");
}
struct Fixture
{
    fs::path root, receive, file;
    std::string payload = std::string(170000, 'a');
    explicit Fixture(const fs::path& parent)
    {
        root = parent / (L"fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        Require(fs::create_directory(root), "New test root");
        receive = root / L"incoming";
        Require(fs::create_directory(receive), "Fixture receive root");
        file = root / L"source.dat";
        for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>((i * 13) >> 3);
        std::ofstream stream(file, std::ios::binary);
        Require(static_cast<bool>(stream.write(payload.data(), payload.size())), "Fixture payload");
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    std::vector<Source> Sources() const
    {
        return {{{"Папка", true}, {}}, {{"Папка/пусто", true}, {}}, {{"Папка/файл.dat"}, file.native()}};
    }
    Destination Target(const wchar_t* name, std::uint64_t budget = UINT64_MAX) const
    {
        return {receive.native(), name, budget};
    }
    bool NoParts() const
    {
        for (const auto& p : fs::directory_iterator(receive)) if (p.path().extension() == L".part") return false;
        return true;
    }
};

void FileTests(const fs::path& base)
{
    Fixture f(fs::absolute(base).make_preferred());
    std::atomic_bool senderCancel{false}, receiverCancel{false};
    std::vector<Source> large{{{"Bin", true}, {}}};
    for (unsigned i = 0; i < 8219; ++i)
        large.push_back({{"Bin/dir-" + std::to_string(i), true}, {}});
    large.push_back({{"Bin/last.dat"}, f.file.native()});
    unsigned offers = 0;
    Expect(Session([&](TlsChannel& ch) { return Send(ch, large); }, [&](TlsChannel& ch) {
        return Receive(ch, [&](const auto& items, auto bytes) {
            ++offers;
            Require(items.size() == 8221 && bytes == f.payload.size(), "One complete multi-batch offer");
            return Destination{f.receive.native(), {}};
        });
    }, senderCancel, receiverCancel), Result::Saved);
    Require(offers == 1 && fs::file_size(f.receive / L"Bin" / L"last.dat") == f.payload.size(), "Batch transfer saved directly");
    fs::remove_all(f.receive / L"Bin");
    unsigned sendProgress = 0, receiveProgress = 0;
    auto sender = [&](TlsChannel& ch) { return Send(ch, f.Sources(), [&](auto, auto) { ++sendProgress; }); };
    auto run = [&](const Offer& offer, const Progress& progress = {}) {
        return Session(sender, [&](TlsChannel& ch) { return Receive(ch, offer, progress); }, senderCancel, receiverCancel);
    };
    auto choose = [&](const wchar_t* name, std::uint64_t budget = UINT64_MAX) -> Offer {
        return [&, name, budget](const auto& items, auto bytes) -> std::optional<Destination> {
            Require(items.size() == 3 && bytes == f.payload.size(), "Correct receiver proposal");
            return f.Target(name, budget);
        };
    };
    Expect(run([](const auto&, auto) { return std::nullopt; }), Result::Declined);
    Expect(run(choose(L"no-space", 1)), Result::NoSpace);
    Require(sendProgress == 0 && fs::is_empty(f.receive), "No bytes or files before acceptance");
    puts("PASS: refusal and insufficient receive budget, no payload or stage");
    Expect(run(choose(L"bundle"), [&](auto, auto) { ++receiveProgress; }), Result::Saved);
    auto savedFile = f.receive / L"bundle" / L"Папка" / L"файл.dat";
    std::ifstream file(savedFile, std::ios::binary);
    const std::string saved((std::istreambuf_iterator<char>(file)), {});
    file.close();
    Require(saved == f.payload && fs::is_empty(f.receive / L"bundle" / L"Папка" / L"пусто"), "Complete directory tree and exact bytes");
    Require(sendProgress > 2 && receiveProgress == sendProgress && f.NoParts(), "Progress at both endpoints and no staging files");
    Expect(run(choose(L"bundle")), Result::InvalidDestination);
    Require(fs::file_size(savedFile) == f.payload.size() && f.NoParts(), "Existing bundle untouched");
    puts("PASS: Unicode file and empty folder, both progress callbacks, no overwrite");
    auto both = run(choose(L"cancel"), [&](auto done, auto) { if (done) receiverCancel = true; });
    Require(both.senderError && both.receiverError && f.NoParts() && !fs::exists(f.receive / L"cancel"), "Cancellation rolls back owned stage");
    puts("PASS: receiver cancellation mid-file, both endpoints stop and staging removed");
    receiverCancel = false;
    both = Session([&](TlsChannel& ch) { return Send(ch, f.Sources(), [&](auto done, auto) { if (done) senderCancel = true; }); },
        [&](TlsChannel& ch) { return Receive(ch, choose(L"send-cancel")); }, senderCancel, receiverCancel);
    Require(both.senderError && both.receiverError && f.NoParts() && !fs::exists(f.receive / L"send-cancel"), "Sender cancellation rolls back owned stage");
    puts("PASS: sender cancellation mid-file stops both endpoints and removes staging");
    senderCancel = false;

}

void MergeFiles(const fs::path& base)
{
    Fixture f(fs::absolute(base).make_preferred());
    fs::create_directory(f.receive / L"data");
    const auto dir = f.receive / L"data";
    const auto write = [](const fs::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary);
        Require(static_cast<bool>(file << text), "Create merge fixture");
    };
    write(dir / L"same.dat", f.payload);
    write(dir / L"replace.dat", std::string(f.payload.size(), 'x'));
    write(dir / L"skip.dat", "local-only bytes");
    write(dir / L"unrelated.dat", "keep");
    const std::vector<Source> files{{{"data", true}, {}}, {{"data/same.dat"}, f.file.native()},
        {{"data/replace.dat"}, f.file.native()}, {{"data/skip.dat"}, f.file.native()}, {{"data/new.dat"}, f.file.native()}};
    std::atomic_bool senderCancel{false}, receiverCancel{false};
    std::uint64_t sent = 0;
    Expect(Session([&](TlsChannel& ch) {
        return Send(ch, files, [&](auto done, auto) { sent = done; });
    }, [&](TlsChannel& ch) {
        return Receive(ch, [&](const auto&, auto) { return f.Target(L"", 2 * f.payload.size()); }, {},
            [&](const auto& conflicts) -> std::optional<std::vector<bool>> {
                Require(conflicts.size() == 3 && conflicts[0].identical && !conflicts[1].identical &&
                    !conflicts[2].identical, "Conflicts grouped by content, not just file size");
                return std::vector<bool>{false, true, false};
            });
    }, senderCancel, receiverCancel), Result::Saved);
    const auto content = [](const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    Require(sent == 2 * f.payload.size() && content(dir / L"same.dat") == f.payload &&
        content(dir / L"replace.dat") == f.payload && content(dir / L"new.dat") == f.payload &&
        content(dir / L"skip.dat") == "local-only bytes" && content(dir / L"unrelated.dat") == "keep",
        "Only selected files transferred and existing tree preserved");
    for (const auto& entry : fs::directory_iterator(dir)) Require(entry.path().extension() == L".dat", "No staging residue");
    puts("PASS: merge, SHA-256 decisions, selected byte budget, preservation of skipped files");
}

void CompatibilityTest(std::string_view version, std::uint32_t revision, const char* expected)
{
    Pair pair;
    std::atomic_bool cancel{false};
    Decision local{true, {}}, remote{true, {}};
    auto reject = [&](SOCKET socket, bool server, Decision& decision, std::string_view v, std::uint32_t r) {
        TlsChannel channel(socket, server, cancel);
        try { channel.ConfirmForTest(Verify, &decision, v, r); }
        catch (const std::runtime_error& e)
        {
            Require(std::string_view(e.what()).find(expected) != std::string_view::npos, "Expected compatibility failure");
            char b{};
            bool blocked = false;
            try { channel.Write(&b, 1); } catch (const std::runtime_error&) { blocked = true; }
            Require(blocked, "Compatibility rejection leaves data gate closed");
            return;
        }
        throw std::runtime_error("Incompatible session accepted");
    };
    std::exception_ptr peerError, localError;
    std::thread worker([&] {
        try { reject(pair.server.Release(), true, remote, version, revision); }
        catch (...) { peerError = std::current_exception(); }
    });
    try { reject(pair.client.Release(), false, local, "1.5.0-i", 1); }
    catch (...) { localError = std::current_exception(); }
    worker.join();
    if (peerError) std::rethrow_exception(peerError);
    if (localError) std::rethrow_exception(localError);
}

void SpeedTests()
{
    CompatibilityTest("1.5.0-I", 1, "application version mismatch");
    CompatibilityTest("1.5.0-i", 2, "LAN revision mismatch");
    puts("PASS: exact version suffix/case and LAN revision rejected on both ends");
    std::atomic_bool cancelClient{false}, cancelServer{false};
    bool offered = false;
    std::optional<SpeedResult> client, server;
    auto run = [&](std::uint32_t bytes, bool yes, const SpeedProgress& sender = {}, const SpeedProgress& receiver = {}) {
        offered = false;
        client.reset(); server.reset();
        return Session([&](TlsChannel& ch) {
            client = RequestSpeed(ch, bytes, sender);
            return client ? Result::Saved : Result::Declined;
        }, [&](TlsChannel& ch) {
            server = AnswerSpeed(ch, [&](auto proposed) { offered = true; Require(proposed == bytes, "Exact speed budget"); return yes; }, receiver);
            return server ? Result::Saved : Result::Declined;
        }, cancelClient, cancelServer);
    };
    auto noTraffic = [](bool, auto, auto) { throw std::runtime_error("Unexpected speed data"); };
    Expect(run(1024, false, noTraffic, noTraffic), Result::Declined);
    Require(offered && !client && !server, "Refusal has no measurements");
    puts("PASS: speed refusal, no payload or measurements");
    std::array<std::uint32_t, 2> clientDone{}, serverDone{};
    auto progress = [](auto& done) -> SpeedProgress {
        return [&](bool sending, auto n, auto total) {
            Require(n >= done[sending] && n <= total && total == MaxSpeedBytesPerDirection, "Bounded speed progress");
            done[sending] = n;
        };
    };
    Expect(run(MaxSpeedBytesPerDirection, true, progress(clientDone), progress(serverDone)), Result::Saved);
    Require(client && server && client->bytesPerDirection == MaxSpeedBytesPerDirection &&
        client->upload == server->download && client->download == server->upload &&
        client->upload.count() > 0 && client->download.count() > 0 &&
        clientDone[0] == MaxSpeedBytesPerDirection && clientDone[1] == MaxSpeedBytesPerDirection &&
        clientDone == serverDone, "Both directions and timing receipt");
    puts("PASS: 16 MiB per direction, matched timing receipts and both progress callbacks");
    for (auto invalid : {0u, MaxSpeedBytesPerDirection + 1})
    {
        auto both = Session([&](TlsChannel& ch) {
            ch.BeginTransfer(false, true);
            const std::array<unsigned char, 4> budget{static_cast<unsigned char>(invalid >> 24),
                static_cast<unsigned char>(invalid >> 16), static_cast<unsigned char>(invalid >> 8), static_cast<unsigned char>(invalid)};
            ch.Write(budget.data(), budget.size());
            char r{}; ch.Read(&r, 1);
            return Result::Saved;
        }, [&](TlsChannel& ch) {
            AnswerSpeed(ch, [](auto) -> bool { throw std::logic_error("Invalid offer displayed"); });
            return Result::Saved;
        }, cancelClient, cancelServer);
        Require(both.senderError && both.receiverError, "Invalid wire budget rejected");
        try { std::rethrow_exception(both.receiverError); }
        catch (const std::runtime_error& e) { Require(std::string_view(e.what()).find("budget invalid") != std::string_view::npos, "Budget must be root cause"); }
    }
    puts("PASS: zero and over-cap incoming speed proposals rejected before prompting");
    const auto stop = run(MaxSpeedBytesPerDirection, true, {}, [&](bool sending, auto n, auto) {
        if (!sending && n) cancelServer = true;
    });
    Require(stop.senderError && stop.receiverError && !client && !server, "Cancelled benchmark returns no measurement");
    puts("PASS: speed cancellation after first chunk stops both endpoints");
}

void WireTests(const fs::path& base)
{
    Fixture f(fs::absolute(base).make_preferred());
    std::atomic_bool cancelClient{false}, cancelServer{false};
    auto wire = [](TlsChannel& ch, const std::string& name, bool sendContent) {
        ch.BeginTransfer(false);
        const unsigned char header[]{'A','Z','L','A','N','F','3',10, 0, 1, 0, 1, 0, 0,0,0,0,0,0,0,1, 0, static_cast<unsigned char>(name.size())};
        ch.Write(header, sizeof(header));
        ch.Write(name.data(), name.size());
        unsigned char reply{};
        ch.Read(&reply, 1);
        Require(sendContent && reply == 1, "Expected acceptance of one-byte valid offer");
        ch.Read(&reply, 1);
        Require(reply == 1, "Receiver requested the offered file");
        std::array<unsigned char, 34> bad{}; // one content byte, wrong SHA-256, and final tag
        bad.back() = 5;
        ch.Write(bad.data(), bad.size());
        ch.Read(&reply, 1);
        return Result::Saved;
    };
    bool offered = false;
    auto receiver = [&](TlsChannel& ch) {
        return Receive(ch, [&](const auto&, auto) { offered = true; return f.Target(L"wire"); });
    };
    auto bad = Session([&](TlsChannel& ch) { return wire(ch, "../evil", false); }, receiver, cancelClient, cancelServer);
    Require(bad.senderError && bad.receiverError && !offered && fs::is_empty(f.receive), "Wire traversal rejected before consent");
    bad = Session([&](TlsChannel& ch) { return wire(ch, "valid.dat", true); }, receiver, cancelClient, cancelServer);
    Require(bad.senderError && bad.receiverError && offered && fs::is_empty(f.receive), "Digest mismatch removes only test data");
    try { std::rethrow_exception(bad.receiverError); }
    catch (const std::runtime_error& e) { Require(std::string_view(e.what()).find("SHA-256 mismatch") != std::string_view::npos, "Digest must be root cause"); }
    puts("PASS: wire traversal rejected before offer, bad SHA-256 rolls back receive");
}
void JunctionTest(const fs::path& base, const fs::path& junction)
{
    Fixture f(fs::absolute(base).make_preferred());
    auto link = fs::absolute(junction).make_preferred();
    const auto attributes = GetFileAttributesW(link.c_str());
    Require(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT), "Test must use a real reparse point");
    std::atomic_bool c{false};
    Expect(Session([&](TlsChannel& ch) { return Send(ch, f.Sources()); }, [&](TlsChannel& ch) {
        return Receive(ch, [&](const auto&, auto) { return Destination{link.native(), L"unsafe-reparse"}; });
    }, c, c), Result::InvalidDestination);
    Require(!fs::exists(link / L"unsafe-reparse"), "Destination junction was not followed");
    puts("PASS: real destination junction refused, nothing written through it");
}

void LegacyAndDeadlineTests()
{
    using namespace std::chrono;
    using namespace std::chrono_literals;
    std::atomic_bool cancel{false};
    {
        Pair pair;
        Decision local{true, {}}, remote{true, {}};
        std::exception_ptr peerError, localError;
        std::thread worker([&] {
            try
            {
                TlsChannel old(pair.server.Release(), true, cancel);
                Require(old.ConfirmForTest(Verify, &remote, "1.5.0", 0), "Legacy simulation confirmation");
                constexpr unsigned char oldBytes[]{'A','Z','L','A','N','F','1',10,0,1};
                old.Write(oldBytes, sizeof(oldBytes));
            }
            catch (...) { peerError = std::current_exception(); }
        });
        bool rejected = false;
        try
        {
            TlsChannel modern(pair.client.Release(), false, cancel);
            try { modern.Confirm(Verify, &local); }
            catch (const std::runtime_error& e) {
                rejected = std::string_view(e.what()).find("Unknown peer compatibility protocol") != std::string_view::npos;
            }
        }
        catch (...) { localError = std::current_exception(); }
        worker.join();
        if (peerError) std::rethrow_exception(peerError);
        if (localError) std::rethrow_exception(localError);
        Require(rejected, "Old framing must fail closed");
        puts("PASS: pre-compatibility framing refused");
    }
    auto stall = Session([&](TlsChannel& ch) {
        ch.BeginTransfer(false, true);
        constexpr unsigned char proposal[]{0, 1, 0, 0};
        ch.Write(proposal, sizeof(proposal));
        unsigned char reply{};
        ch.Read(&reply, 1); Require(reply == 1, "Stalled test was accepted");
        ch.Read(&reply, 1); Require(reply == 2, "Receiver is ready for stalled test");
        ch.Read(&reply, 1); // never send payload, wait for receiver to close
        return Result::Saved;
    }, [&](TlsChannel& ch) {
        const auto started = steady_clock::now();
        try { AnswerSpeed(ch, [](auto) { return true; }); }
        catch (const std::runtime_error& e)
        {
            const auto spent = steady_clock::now() - started;
            Require(std::string_view(e.what()).find("Transfer timed out") != std::string_view::npos &&
                spent >= 9s && spent < 14s, "Stalled direction must honor the 10-second deadline");
            return Result::Declined;
        }
        throw std::logic_error("Stalled speed test completed unexpectedly");
    }, cancel, cancel);
    Require(stall.senderError && !stall.receiverError && stall.receiver == Result::Declined, "Stalled test stopped");
    puts("PASS: stalled test direction times out with no measurement");
}

int wmain(int argc, wchar_t** argv)
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return 1;
    int result = 0;
    try
    {
        Require(argc == 2 || argc == 3, "Pass the local NTFS test workspace directory");
        ValidateTests();
        SpeedTests();
        LegacyAndDeadlineTests();
        FileTests(fs::path(argv[1]));
        MergeFiles(fs::path(argv[1]));
        WireTests(fs::path(argv[1]));
        if (argc == 3) JunctionTest(fs::path(argv[1]), fs::path(argv[2]));
        std::string code;
        Test(true, false, code); puts("PASS: mutually confirmed TLS, multi-record bytes and pre-confirmation gates");
        Test(false, false, code); puts("PASS: declined confirmation");
        Test(true, true, code); puts("PASS: cancelled channel refuses writes");
    }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); result = 1; }
    WSACleanup();
    return result;
}
