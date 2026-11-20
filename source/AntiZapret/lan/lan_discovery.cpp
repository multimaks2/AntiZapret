#include "lan_discovery.h"

#ifdef ANTIZAPRET_LAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <netlistmgr.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <future>
#include <optional>
#include <tuple>

namespace lan
{
namespace
{
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;
constexpr auto Interval = 5s;
constexpr auto ReplyWindow = 900ms;
constexpr auto PeerLifetime = 11s;
constexpr size_t MaxPeers = 64;
#ifdef ANTIZAPRET_LAN_TEST
// Isolate the autonomous test clients from the production discovery group.
constexpr unsigned short Port = 45938;
constexpr auto GroupHost = 0xEFFFF138u;
#else
constexpr unsigned short Port = 45937;
constexpr auto GroupHost = 0xEFFFF137u;
#endif
// Group addresses are administratively scoped.
constexpr std::array<char, 8> Magic{'A','Z','L','A','N',1,0,0};
using Id = std::array<UCHAR, 16>;
struct Packet
{
    std::array<char, 8> magic = Magic;
    UCHAR type = 0;
    Id request{};
    Id sender{};
    UCHAR nameLength = 0;
    std::array<char, 64> name{};
};
static_assert(sizeof(Packet) == 106);

std::string Key(const Id& id)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string key;
    for (auto byte : id) { key += hex[byte >> 4]; key += hex[byte & 15]; }
    return key;
}

bool Random(Id& id)
{
    return BCryptGenRandom(nullptr, id.data(), static_cast<ULONG>(id.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 && id != Id{};
}

struct Interface
{
    ULONG index, addr, mask; // addresses in host byte order
    GUID profile{};
    bool wifi = false;
    auto Key() const { return std::tie(index, addr, mask); }
    bool operator==(const Interface& other) const { return Key() == other.Key(); }
};

bool Private(ULONG addr)
{
    return (addr >> 24) == 10 || (addr >> 20) == 0xAC1 || (addr >> 16) == 0xC0A8;
}

bool OnLink(const Interface& net, ULONG addr)
{
    const ULONG host = addr & ~net.mask;
    return Private(addr) && (addr & net.mask) == (net.addr & net.mask) && host && host != ~net.mask;
}

std::vector<Interface> PrivateInterfaces(NLM_NETWORK_CATEGORY wanted = NLM_NETWORK_CATEGORY_PRIVATE)
{
    // Discovery uses Private only. Public profiles are enumerated for settings, never for sockets.
    ComPtr<INetworkListManager> manager;
    if (FAILED(CoCreateInstance(__uuidof(NetworkListManager), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(manager.GetAddressOf())))) return {};
    ComPtr<IEnumNetworkConnections> connections;
    if (FAILED(manager->GetNetworkConnections(connections.GetAddressOf()))) return {};
    std::vector<std::pair<GUID, GUID>> adaptersAndProfiles;
    ComPtr<INetworkConnection> connection;
    while (connections->Next(1, connection.ReleaseAndGetAddressOf(), nullptr) == S_OK)
    {
        VARIANT_BOOL connected = VARIANT_FALSE;
        ComPtr<INetwork> network;
        NLM_NETWORK_CATEGORY category{};
        GUID guid{}, profile{};
        if (SUCCEEDED(connection->get_IsConnected(&connected)) && connected == VARIANT_TRUE &&
            SUCCEEDED(connection->GetNetwork(network.GetAddressOf())) &&
            SUCCEEDED(network->GetCategory(&category)) && category == wanted &&
            SUCCEEDED(connection->GetAdapterId(&guid)) && SUCCEEDED(network->GetNetworkId(&profile)))
            adaptersAndProfiles.emplace_back(guid, profile);
    }
    ULONG size = 16384;
    std::vector<BYTE> buffer(size);
    auto adapters = [&] { return reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); };
    constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters(), &size);
    if (rc == ERROR_BUFFER_OVERFLOW && size <= 1024 * 1024)
    {
        buffer.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters(), &size);
    }
    if (rc != NO_ERROR) return {};
    std::vector<Interface> result;
    for (auto* a = adapters(); a; a = a->Next)
    {
        if (a->OperStatus != IfOperStatusUp || (a->Flags & IP_ADAPTER_NO_MULTICAST) ||
            (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211)) continue;
        GUID guid{};
        const std::string ascii(a->AdapterName);
        const std::wstring guidText(ascii.begin(), ascii.end());
        if (FAILED(CLSIDFromString(guidText.c_str(), &guid))) continue;
        const auto match = std::find_if(adaptersAndProfiles.begin(), adaptersAndProfiles.end(),
            [&](const auto& pair) { return pair.first == guid; });
        if (match == adaptersAndProfiles.end()) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET ||
                u->OnLinkPrefixLength < 8 || u->OnLinkPrefixLength > 30 || u->DadState != IpDadStatePreferred) continue;
            const auto addr = ntohl(reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            if (Private(addr))
            {
                result.push_back({a->IfIndex, addr, 0xffffffffu << (32 - u->OnLinkPrefixLength),
                    match->second, a->IfType == IF_TYPE_IEEE80211});
                break; // A single membership per adapter.
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Key() < b.Key(); });
    return result;
}

std::string ComputerName()
{
    wchar_t wide[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD length = static_cast<DWORD>(std::size(wide));
    char utf8[64]{};
    if (!GetComputerNameW(wide, &length)) return "AntiZapret";
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
        static_cast<int>(length), utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
    return size > 0 ? std::string(utf8, size) : "AntiZapret";
}

std::string ValidName(const Packet& p)
{
    if (!p.nameLength || p.nameLength > p.name.size()) return {};
    wchar_t wide[64]{};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p.name.data(), p.nameLength, wide, 64);
    if (!size || std::any_of(wide, wide + size, [](wchar_t c) {
        return c < 32 || (c >= 127 && c <= 159) || (c >= 0x202a && c <= 0x202e) ||
            (c >= 0x2066 && c <= 0x2069);
    })) return {};
    return std::string(p.name.data(), p.nameLength);
}
}

struct Discovery::Impl
{
    WSADATA ws{};
    const bool initialized = WSAStartup(MAKEWORD(2, 2), &ws) == 0;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SOCKET socket = INVALID_SOCKET;
    LPFN_WSARECVMSG receive = nullptr;
    Id id{}, request{}, previousRequest{};
    std::string name = ComputerName();
    std::vector<Interface> interfaces;
    std::vector<Peer> peers;
    struct PingResult { std::string id; std::uint32_t local, remote; std::optional<std::uint32_t> ms; };
    std::future<PingResult> ping;
    std::vector<Route> routes;
    std::optional<Invitation> incoming;
    std::vector<std::pair<Ticket, Clock::time_point>> recentTickets;
    bool exchangeVisible = false;
    Clock::time_point nextRefresh{}, nextQuery{}, sent{}, previousSent{}, replyWindowEnd{};
    unsigned replies = 0;
    bool wasBrowsing = false;
    std::optional<GUID> restoreProfile;

    Impl() { Random(id); }
    ~Impl() { Close(); if (SUCCEEDED(com)) CoUninitialize(); if (initialized) WSACleanup(); }

    void Close()
    {
        if (socket != INVALID_SOCKET) closesocket(socket);
        socket = INVALID_SOCKET;
        interfaces.clear();
    }

    void Configure(const std::vector<Interface>& nets)
    {
        if (interfaces == nets && socket != INVALID_SOCKET) return;
        Close();
        if (nets.empty()) return;
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == INVALID_SOCKET) return;
        auto opt = [&](int level, int key, const auto& value) {
            return setsockopt(socket, level, key, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
        };
        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(Port);
        GUID extension = WSAID_WSARECVMSG;
        DWORD written = 0;
        u_long nonblocking = 1;
        if (!opt(SOL_SOCKET, SO_REUSEADDR, 1) || !opt(IPPROTO_IP, IP_PKTINFO, 1) ||
            !opt(IPPROTO_IP, IP_MULTICAST_TTL, 1) || !opt(IPPROTO_IP, IP_MULTICAST_LOOP, 1) ||
            bind(socket, reinterpret_cast<const sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0 ||
            ioctlsocket(socket, FIONBIO, &nonblocking) != 0 ||
            WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &extension, sizeof(extension),
                &receive, sizeof(receive), &written, nullptr, nullptr) != 0) { Close(); return; }
        for (const auto& net : nets)
        {
            ip_mreq membership{};
            membership.imr_multiaddr.s_addr = htonl(GroupHost);
            membership.imr_interface.s_addr = htonl(net.addr);
            if (!opt(IPPROTO_IP, IP_ADD_MEMBERSHIP, membership)) { Close(); return; }
        }
        interfaces = nets;
    }

    void Send(const Packet& p, const Interface& net)
    {
        in_addr local{};
        local.s_addr = htonl(net.addr);
        if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local), sizeof(local))) return;
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(Port);
        dest.sin_addr.s_addr = htonl(GroupHost);
        sendto(socket, reinterpret_cast<const char*>(&p), sizeof(p), 0,
            reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
    }

    void Receive(bool browsing, bool visible)
    {
        for (int i = 0; i < 32; ++i)
        {
            Packet p;
            sockaddr_in src{};
            alignas(WSACMSGHDR) char control[WSA_CMSG_SPACE(sizeof(IN_PKTINFO))]{};
            WSABUF payload{sizeof(p), reinterpret_cast<char*>(&p)};
            WSAMSG msg{reinterpret_cast<sockaddr*>(&src), sizeof(src), &payload, 1,
                {sizeof(control), control}, 0};
            DWORD got = 0;
            if (receive(socket, &msg, &got, nullptr, nullptr) != 0)
            {
                if (WSAGetLastError() == WSAEMSGSIZE) continue;
                break;
            }
            if (got != sizeof(p) || msg.dwFlags & (MSG_TRUNC | MSG_CTRUNC) || src.sin_family != AF_INET ||
                src.sin_port != htons(Port) || p.magic != Magic || p.request == Id{} || p.sender == Id{} || p.sender == id) continue;
            const IN_PKTINFO* info = nullptr;
            for (auto* h = WSA_CMSG_FIRSTHDR(&msg); h; h = WSA_CMSG_NXTHDR(&msg, h))
                if (h->cmsg_level == IPPROTO_IP && h->cmsg_type == IP_PKTINFO &&
                    h->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO)))
                    info = reinterpret_cast<const IN_PKTINFO*>(WSA_CMSG_DATA(h));
            if (!info || info->ipi_addr.s_addr != htonl(GroupHost)) continue;
            const auto match = std::find_if(interfaces.begin(), interfaces.end(), [&](const Interface& net) {
                return net.index == info->ipi_ifindex && OnLink(net, ntohl(src.sin_addr.s_addr));
            });
            if (match == interfaces.end()) continue;
            const auto now = Clock::now();
            const auto key = Key(p.sender);
            Route path{key, match->addr, ntohl(src.sin_addr.s_addr), match->index, now + PeerLifetime};
            auto route = std::find_if(routes.begin(), routes.end(), [&](const Route& r) { return r.SamePath(path); });
            if ((p.type == 5 || p.type == 6) && exchangeVisible && p.nameLength == 2 * id.size() && route != routes.end() &&
                route->challengeExpires > now && std::memcmp(p.name.data(), id.data(), id.size()) == 0 &&
                ((request != Id{} && now - sent <= PeerLifetime && std::memcmp(p.name.data() + id.size(), request.data(), request.size()) == 0) ||
                 (previousRequest != Id{} && now - previousSent <= PeerLifetime && std::memcmp(p.name.data() + id.size(), previousRequest.data(), previousRequest.size()) == 0)) &&
                recentTickets.size() < MaxPeers &&
                std::none_of(recentTickets.begin(), recentTickets.end(), [&](const auto& r) { return r.first == p.request; }))
            {
                recentTickets.emplace_back(p.request, now + 30s);
                if (!incoming) incoming = Invitation{*route, p.request, p.type == 5 ? Operation::Speed : Operation::Files};
                continue;
            }
            const bool visibleQuery = p.type == 3 && exchangeVisible && p.request != request && !ValidName(p).empty();
            const bool visibleReply = p.type == 4 && exchangeVisible && p.request == request && now - sent <= ReplyWindow && !ValidName(p).empty();
            if (visibleQuery || visibleReply)
            {
                if (visibleQuery) { path.challenge = p.request; path.challengeExpires = now + PeerLifetime; }
                else if (route != routes.end()) { path.challenge = route->challenge; path.challengeExpires = route->challengeExpires; }
                if (route != routes.end()) *route = path;
                else if (routes.size() < MaxPeers) routes.push_back(path);
            }
            if ((p.type == 1 && p.request != request && p.nameLength == 0 && visible) || visibleQuery)
            {
                if (now >= replyWindowEnd) { replyWindowEnd = now + 1s; replies = 0; }
                if (replies >= 8) continue;
                Packet reply;
                reply.type = visibleQuery ? 4 : 2;
                reply.request = p.request;
                reply.sender = id;
                reply.nameLength = static_cast<UCHAR>(name.size());
                std::copy(name.begin(), name.end(), reply.name.begin());
                ++replies;
                Send(reply, *match);
            }
            else if (visibleReply || (p.type == 2 && browsing && p.request == request && now - sent <= ReplyWindow))
            {
                auto label = ValidName(p);
                if (label.empty()) continue;
                auto peer = std::find_if(peers.begin(), peers.end(), [&](const Peer& x) { return x.id == key; });
                if (peer == peers.end())
                {
                    if (peers.size() < MaxPeers) peers.push_back({key, std::move(label), {}, now + PeerLifetime, path.local, path.remote});
                }
                else if (peer->expires < sent + PeerLifetime)
                {
                    if (peer->local != path.local || peer->remote != path.remote)
                        *peer = {key, std::move(label), {}, now + PeerLifetime, path.local, path.remote};
                    else
                    {
                        peer->name = std::move(label);
                        peer->expires = now + PeerLifetime;
                    }
                }
            }
        }
    }

    void UpdatePing(Clock::time_point now)
    {
        if (ping.valid())
        {
            if (ping.wait_for(0ms) != std::future_status::ready) return;
            const auto result = ping.get();
            for (auto& peer : peers)
                if (peer.id == result.id && peer.local == result.local && peer.remote == result.remote)
                    peer.latencyMs = result.ms;
        }
        const auto next = std::min_element(peers.begin(), peers.end(), [](const Peer& a, const Peer& b) {
            return a.nextPing < b.nextPing;
        });
        if (next == peers.end() || next->nextPing > now) return;
        next->nextPing = now + Interval;
        try
        {
            ping = std::async(std::launch::async, [result = PingResult{next->id, next->local, next->remote, {}}]() mutable {
                const HANDLE handle = IcmpCreateFile();
                if (handle == INVALID_HANDLE_VALUE) return result;
                char payload[] = "AZLAN";
                alignas(ICMP_ECHO_REPLY) unsigned char reply[sizeof(ICMP_ECHO_REPLY) + sizeof(payload) + 8]{};
                const auto count = IcmpSendEcho2Ex(handle, nullptr, nullptr, nullptr, htonl(result.local),
                    htonl(result.remote), payload, sizeof(payload), nullptr, reply, sizeof(reply), 300);
                const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply);
                if (count && echo->Status == IP_SUCCESS && echo->Address == htonl(result.remote))
                    result.ms = echo->RoundTripTime;
                IcmpCloseHandle(handle);
                return result;
            });
        }
        catch (...) { next->latencyMs.reset(); }
    }

    void Tick(bool browsing, bool visible, Clock::time_point now)
    {
        if (browsing && now >= nextQuery)
        {
            nextQuery = now + Interval;
            Packet query;
            query.type = exchangeVisible ? 3 : 1;
            if (exchangeVisible)
            {
                query.sender = id;
                query.nameLength = static_cast<UCHAR>(name.size());
                std::copy(name.begin(), name.end(), query.name.begin());
            }
            // Hidden queries do not expose a persistent session ID.
            if (Random(query.request) && (exchangeVisible || Random(query.sender)))
            {
                previousRequest = request;
                previousSent = sent;
                request = query.request;
                sent = now;
                for (const auto& net : interfaces)
                {
                    Send(query, net);
                    if (exchangeVisible && Random(query.sender))
                    {
                        Packet legacy;
                        legacy.type = 1;
                        legacy.request = query.request;
                        legacy.sender = query.sender;
                        Send(legacy, net);
                        query.sender = id;
                    }
                }
            }
        }
        peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& p) { return p.expires <= now; }), peers.end());
        routes.erase(std::remove_if(routes.begin(), routes.end(), [&](const Route& r) { return r.expires <= now; }), routes.end());
        recentTickets.erase(std::remove_if(recentTickets.begin(), recentTickets.end(), [&](const auto& r) { return r.second <= now; }), recentTickets.end());
        Receive(browsing, visible);
        if (browsing) UpdatePing(now);
    }
};

Discovery::Discovery() : impl(std::make_unique<Impl>()) {}
Discovery::~Discovery() = default;

void Discovery::Update(bool browsing, bool visible)
{
    auto& d = *impl;
    if (!d.initialized || d.id == Id{}) return;
    if (d.exchangeVisible != (browsing && visible))
    {
        d.exchangeVisible = browsing && visible;
        d.routes.clear(); d.incoming.reset(); d.nextQuery = {}; d.request = {}; d.previousRequest = {};
        if (d.exchangeVisible)
        {
            Id fresh{};
            if (Random(fresh)) d.id = fresh;
            else d.exchangeVisible = false;
        }
    }
    if (!browsing && !visible)
    {
        if (d.socket != INVALID_SOCKET) d.Close();
        d.peers.clear();
        d.wasBrowsing = false;
        d.nextRefresh = {};
        return;
    }
    const auto now = Clock::now();
    if (browsing && !d.wasBrowsing) { d.nextRefresh = {}; d.nextQuery = {}; }
    if (!browsing) d.peers.clear();
    d.wasBrowsing = browsing;
    if (now >= d.nextRefresh)
    {
        d.nextRefresh = now + Interval;
        const auto old = d.interfaces;
        d.Configure(PrivateInterfaces());
        if (old != d.interfaces) { d.peers.clear(); d.routes.clear(); d.incoming.reset(); d.nextQuery = {}; }
    }
    if (d.socket != INVALID_SOCKET) d.Tick(browsing, visible, Clock::now());
}

bool Route::SamePath(const Route& r) const
{
    return peer == r.peer && local == r.local && remote == r.remote && index == r.index;
}

bool IsPrivateDirect(const Route& r)
{
    const auto nets = PrivateInterfaces();
    if (r.local == r.remote || std::none_of(nets.begin(), nets.end(), [&](const Interface& net) {
        return net.index == r.index && net.addr == r.local && OnLink(net, r.remote);
    })) return false;
    MIB_IF_ROW2 physical{};
    physical.InterfaceIndex = r.index;
    if (GetIfEntry2(&physical) != NO_ERROR || !physical.InterfaceAndOperStatusFlags.HardwareInterface) return false;
    MIB_IPINTERFACE_ROW iface{};
    InitializeIpInterfaceEntry(&iface);
    iface.Family = AF_INET;
    iface.InterfaceIndex = r.index;
    if (GetIpInterfaceEntry(&iface) != NO_ERROR || iface.WeakHostReceive || iface.WeakHostSend) return false;
    SOCKADDR_INET src{}, dst{}, bestSource{};
    src.Ipv4.sin_family = dst.Ipv4.sin_family = AF_INET;
    src.Ipv4.sin_addr.s_addr = htonl(r.local);
    dst.Ipv4.sin_addr.s_addr = htonl(r.remote);
    MIB_IPFORWARD_ROW2 best{};
    // Check the actual preferred route, not merely the source subnet.
    return GetBestRoute2(nullptr, 0, &src, &dst, 0, &best, &bestSource) == NO_ERROR &&
        best.InterfaceIndex == r.index && bestSource.Ipv4.sin_addr.s_addr == src.Ipv4.sin_addr.s_addr &&
        best.NextHop.Ipv4.sin_family == AF_INET && best.NextHop.Ipv4.sin_addr.s_addr == 0;
}
const std::vector<Route>& Discovery::Routes() const { return impl->routes; }

std::optional<Invitation> Discovery::Invite(const std::string& peer, Operation operation)
{
    const auto now = Clock::now();
    const auto& routes = impl->routes;
    const auto first = std::find_if(routes.begin(), routes.end(), [&](const Route& r) { return r.peer == peer && r.expires > now && r.challengeExpires > now; });
    if (first == routes.end() || std::any_of(first + 1, routes.end(), [&](const Route& r) { return r.peer == peer && r.expires > now && r.challengeExpires > now; }))
        return std::nullopt; // Never guess between multiple interfaces/addresses for the same ID.
    Invitation invite{*first, {}, operation};
    if (!Random(invite.ticket) || !RepeatInvite(invite)) return std::nullopt;
    return invite;
}
bool Discovery::RepeatInvite(const Invitation& invite)
{
    auto& d = *impl;
    if (!d.exchangeVisible || d.socket == INVALID_SOCKET || invite.ticket == Ticket{}) return false;
    const auto now = Clock::now();
    const auto current = std::find_if(d.routes.begin(), d.routes.end(), [&](const Route& r) {
        return r.SamePath(invite.route) && r.expires > now && r.challengeExpires > now;
    });
    if (current == d.routes.end()) return false;
    const auto net = std::find_if(d.interfaces.begin(), d.interfaces.end(), [&](const Interface& n) {
        return n.addr == invite.route.local && n.index == invite.route.index && OnLink(n, invite.route.remote);
    });
    if (net == d.interfaces.end() || invite.route.peer.size() != 32) return false;
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    Packet packet;
    packet.type = invite.operation == Operation::Speed ? 5 : 6;
    packet.sender = d.id;
    packet.request = invite.ticket;
    packet.nameLength = 32;
    for (std::size_t i = 0; i < 16; ++i)
        packet.name[i] = static_cast<char>(16 * digit(invite.route.peer[i * 2]) + digit(invite.route.peer[i * 2 + 1]));
    std::memcpy(packet.name.data() + 16, current->challenge.data(), current->challenge.size());
    d.Send(packet, *net);
    return true;
}
std::optional<Invitation> Discovery::TakeInvitation()
{
    auto result = std::move(impl->incoming);
    impl->incoming.reset();
    if (result && result->route.expires <= Clock::now()) return {};
    return result;
}

bool Discovery::Available() const { return impl->socket != INVALID_SOCKET; }
const std::vector<Peer>& Discovery::Peers() const { return impl->peers; }

const wchar_t* Discovery::PrepareNetworkSettings(bool& tracked)
{
    tracked = false;
    if (!PrivateInterfaces().empty()) return nullptr;
    const auto publicNets = PrivateInterfaces(NLM_NETWORK_CATEGORY_PUBLIC);
    if (publicNets.empty()) return nullptr;
    if (publicNets.size() != 1 || impl->restoreProfile) return L"ms-settings:network-status";
    impl->restoreProfile = publicNets.front().profile;
    tracked = true;
    return publicNets.front().wifi ? L"ms-settings:network-wifi" : L"ms-settings:network-ethernet";
}

bool Discovery::RestoreNetworkProfile()
{
    if (!impl->restoreProfile) return true;
    ComPtr<INetworkListManager> manager;
    ComPtr<INetwork> network;
    NLM_NETWORK_CATEGORY category{};
    if (FAILED(CoCreateInstance(__uuidof(NetworkListManager), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(manager.GetAddressOf()))) ||
        FAILED(manager->GetNetwork(*impl->restoreProfile, network.GetAddressOf())) ||
        FAILED(network->GetCategory(&category))) return false;
    if (category == NLM_NETWORK_CATEGORY_PRIVATE &&
        (FAILED(network->SetCategory(NLM_NETWORK_CATEGORY_PUBLIC)) ||
         FAILED(network->GetCategory(&category)) || category != NLM_NETWORK_CATEGORY_PUBLIC)) return false;
    impl->restoreProfile.reset();
    impl->nextRefresh = {};
    return true;
}
}
#endif
