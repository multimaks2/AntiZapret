#include "lan_speed.h"
#if defined(ANTIZAPRET_LAN)
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <stdexcept>

#pragma comment(lib, "Bcrypt.lib")

namespace lan::transfer
{
namespace
{
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto DirectionLimit = 10s;
constexpr auto ConsentLimit = 30s;
constexpr unsigned char Yes = 1, No = 0, Ready = 2, Finished = 3;

void Validate(std::uint32_t bytes)
{
    if (!bytes || bytes > MaxSpeedBytesPerDirection) throw std::runtime_error("Speed byte budget invalid");
}
void SendTag(TlsChannel& ch, unsigned char tag, TlsChannel::Deadline limit)
{
    ch.WriteUntil(&tag, 1, limit);
}
unsigned char ReadTag(TlsChannel& ch, TlsChannel::Deadline limit)
{
    unsigned char tag{};
    ch.ReadUntil(&tag, 1, limit);
    return tag;
}
void WriteU32(TlsChannel& ch, std::uint32_t n, TlsChannel::Deadline limit)
{
    const std::array<unsigned char, 4> bytes{static_cast<unsigned char>(n >> 24),
        static_cast<unsigned char>(n >> 16), static_cast<unsigned char>(n >> 8), static_cast<unsigned char>(n)};
    ch.WriteUntil(bytes.data(), bytes.size(), limit);
}
std::uint32_t ReadU32(TlsChannel& ch, TlsChannel::Deadline limit)
{
    std::array<unsigned char, 4> bytes{};
    ch.ReadUntil(bytes.data(), bytes.size(), limit);
    std::uint32_t n = 0;
    for (auto b : bytes) n = (n << 8) | b;
    return n;
}

std::chrono::nanoseconds Stream(TlsChannel& ch, bool sending, std::uint32_t bytes, const SpeedProgress& progress)
{
    std::array<unsigned char, 64 * 1024> buffer{};
    if (progress) progress(sending, 0, bytes);
    const auto begin = Clock::now();
    const auto end = begin + DirectionLimit;
    if (sending)
    {
        if (BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG))
            throw std::runtime_error("Cannot generate speed test data");
        if (ReadTag(ch, end) != Ready) throw std::runtime_error("Invalid speed readiness marker");
    }
    else SendTag(ch, Ready, end);
    const auto measuredStart = Clock::now();
    for (std::uint32_t done = 0; done < bytes; )
    {
        const auto n = (std::min)(bytes - done, static_cast<std::uint32_t>(buffer.size()));
        if (sending) ch.WriteUntil(buffer.data(), n, end);
        else ch.ReadUntil(buffer.data(), n, end);
        done += n;
        if (progress) progress(sending, done, bytes);
    }
    const auto elapsed = Clock::now() - measuredStart;
    if (sending)
    {
        const auto micros = std::chrono::microseconds(ReadU32(ch, end));
        if (micros < 1us || micros > DirectionLimit) throw std::runtime_error("Invalid peer speed duration");
        return micros;
    }
    const auto micros = (std::max)(1us, std::chrono::ceil<std::chrono::microseconds>(elapsed));
    if (micros > DirectionLimit) throw std::runtime_error("Speed direction timed out");
    WriteU32(ch, static_cast<std::uint32_t>(micros.count()), end);
    return micros;
}
}

std::optional<SpeedResult> RequestSpeed(TlsChannel& channel, std::uint32_t bytes, const SpeedProgress& progress)
{
    Validate(bytes);
    channel.BeginTransfer(false, true);
    const auto consent = Clock::now() + ConsentLimit;
    WriteU32(channel, bytes, consent);
    const auto reply = ReadTag(channel, consent);
    if (reply == No) return std::nullopt;
    if (reply != Yes) throw std::runtime_error("Invalid speed offer decision");
    const auto up = Stream(channel, true, bytes, progress);
    const auto down = Stream(channel, false, bytes, progress);
    if (ReadTag(channel, Clock::now() + 2s) != Finished) throw std::runtime_error("Invalid speed completion marker");
    return SpeedResult{bytes, up, down};
}
std::optional<SpeedResult> AnswerSpeed(TlsChannel& channel, const SpeedOffer& consent, const SpeedProgress& progress)
{
    channel.BeginTransfer(true, true);
    const auto end = Clock::now() + ConsentLimit;
    const auto bytes = ReadU32(channel, end);
    Validate(bytes);
    const bool allowed = consent && consent(bytes);
    SendTag(channel, allowed ? Yes : No, end);
    if (!allowed) return std::nullopt;
    const auto down = Stream(channel, false, bytes, progress);
    const auto up = Stream(channel, true, bytes, progress);
    SendTag(channel, Finished, Clock::now() + 2s);
    return SpeedResult{bytes, up, down};
}
}
#endif
