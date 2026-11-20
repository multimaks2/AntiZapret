#pragma once
#if defined(ANTIZAPRET_LAN)
#include "lan_discovery.h"
#include "lan_speed.h"
#include "lan_transfer.h"

namespace lan
{
class Session
{
public:
    enum class Phase { Idle, Connecting, Pair, Consent, FileOffer, Measuring, Transferring, Finished, Declined, Cancelled, Failed };
    struct Status
    {
        Phase phase = Phase::Idle;
        std::string code, error, peer;
        std::optional<transfer::SpeedResult> speed;
        std::uint64_t done = 0, total = 0;
        std::size_t itemCount = 0;
        std::vector<std::string> sample;
        std::wstring bundle;
        Ticket ticket{};
        Operation operation = Operation::Speed;
        bool sending = false;
    };
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void Update(Discovery&, bool visible);
    bool CanStart(const Discovery&, const std::string& peer) const;
    bool Busy() const;
    bool Start(Discovery&, const std::string& peer, std::vector<transfer::Source> sources = {});
    Status Read() const;
    void Decide(Phase, bool);
    void Cancel();
    void ChooseDestination(const Ticket&, std::optional<std::wstring>);
#ifdef ANTIZAPRET_LAN_TEST
    void StartLoopbackForTest(bool receiver, Ticket ticket, Operation = Operation::Speed, std::vector<transfer::Source> sources = {});
    void PumpLoopbackForTest(bool leaseValid);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
#endif
