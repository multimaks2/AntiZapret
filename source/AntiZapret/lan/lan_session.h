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
    enum class Phase { Idle, Connecting, Pair, Consent, FileOffer, CheckingFiles, FileConflict, Measuring, Transferring, Finished, Declined, Cancelled, Failed };
    struct Status
    {
        Phase phase = Phase::Idle;
        std::string code, error, peer;
        std::optional<transfer::SpeedResult> speed;
        std::uint64_t done = 0, total = 0;
        std::size_t itemCount = 0;
        std::vector<std::string> sample;
        transfer::Conflict conflict{};
        std::size_t conflictsOfKind = 0;
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
    void ChooseConflict(const Ticket&, std::size_t index, bool replace, bool applyToAll);
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
