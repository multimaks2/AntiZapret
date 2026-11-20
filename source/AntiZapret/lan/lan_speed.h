#pragma once
#include "lan_transfer_tls.h"
#if defined(ANTIZAPRET_LAN)
#include <functional>
#include <optional>

namespace lan::transfer
{
inline constexpr std::uint32_t MaxSpeedBytesPerDirection = 16 * 1024 * 1024;
struct SpeedResult
{
    std::uint32_t bytesPerDirection;
    std::chrono::nanoseconds upload;   // Measured and reported by the peer.
    std::chrono::nanoseconds download; // Measured by this endpoint.
};
// One test on one verified channel, sequential directions; nullopt = declined.
// No disk I/O. Progress callback may set the channel's cancellation flag.
using SpeedOffer = std::function<bool(std::uint32_t bytesPerDirection)>;
using SpeedProgress = std::function<void(bool sending, std::uint32_t done, std::uint32_t total)>;
std::optional<SpeedResult> RequestSpeed(TlsChannel&, std::uint32_t bytes = MaxSpeedBytesPerDirection,
                                       const SpeedProgress& = {});
std::optional<SpeedResult> AnswerSpeed(TlsChannel&, const SpeedOffer&, const SpeedProgress& = {});
}
#endif
