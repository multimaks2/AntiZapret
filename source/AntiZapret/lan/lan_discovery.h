#pragma once

#ifdef ANTIZAPRET_LAN
#include <chrono>
#include <array>
#include <cstdint>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace lan
{
using Ticket = std::array<unsigned char, 16>;
struct Route
{
    std::string peer;
    std::uint32_t local, remote, index; // host byte order
    std::chrono::steady_clock::time_point expires;
    std::chrono::steady_clock::time_point challengeExpires{};
    Ticket challenge{}; // Latest visible query from this peer.
    bool SamePath(const Route&) const;
};
enum class Operation { Speed, Files };
struct Invitation { Route route; Ticket ticket; Operation operation; };
// Requires COM initialized on the caller. Revalidate when connecting and during I/O.
bool IsPrivateDirect(const Route&);

struct Peer
{
    std::string id;
    std::string name;
    std::optional<std::uint32_t> latencyMs;
    std::chrono::steady_clock::time_point expires;
    std::uint32_t local, remote; // host byte order
    std::chrono::steady_clock::time_point nextPing{};
};

class Discovery
{
public:
    Discovery();
    ~Discovery();
    void Update(bool browsing, bool visible);
    bool Available() const;
    const std::vector<Peer>& Peers() const;
    const std::vector<Route>& Routes() const;
    std::optional<Invitation> Invite(const std::string& peer, Operation = Operation::Speed);
    bool RepeatInvite(const Invitation&);
    std::optional<Invitation> TakeInvitation();
    const wchar_t* PrepareNetworkSettings(bool& tracked);
    bool RestoreNetworkProfile();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
#endif
