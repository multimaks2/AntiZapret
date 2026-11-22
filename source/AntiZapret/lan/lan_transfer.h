#pragma once
#include "lan_transfer_tls.h"
#if defined(ANTIZAPRET_LAN)
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace lan::transfer
{
inline constexpr std::size_t MaxEntries = 16384;

struct Item
{
    std::string name; // Relative UTF-8 path, '/' separators, parent entries first.
    bool directory = false;
    std::uint64_t size = 0;
};
struct Source
{
    Item item;             // Sender ignores size and reads it from the opened source.
    std::wstring localFile; // Absolute local NTFS path; empty for directories.
};
struct Destination
{
    std::wstring folder; // Existing absolute local NTFS directory picked by recipient.
    std::wstring bundle; // One new directory in folder; never replaces anything.
    std::uint64_t maxBytes = UINT64_MAX; // Optional local budget, not sent to peer.
};
enum class Result { Saved, Declined, NoSpace, InvalidDestination, SavedReceiptLost };
using Offer = std::function<std::optional<Destination>(const std::vector<Item>&, std::uint64_t)>;
using Progress = std::function<void(std::uint64_t, std::uint64_t)>;
struct Conflict { std::size_t index; std::string name; bool identical; };
// One replace/skip flag per conflict, in manifest order; nullopt cancels.
using ResolveConflicts = std::function<std::optional<std::vector<bool>>(const std::vector<Conflict>&)>;

// Throws on malformed input, I/O failure or cancellation. After failure destroy
// channel; no retries. On transport failure after sender's final marker, delivery
// may have completed even if the sender did not receive the receipt.
Result Send(TlsChannel&, const std::vector<Source>&, const Progress& = {});
Result Receive(TlsChannel&, const Offer&, const Progress& = {}, const ResolveConflicts& = {});
std::uint64_t ValidateManifest(const std::vector<Item>&);
}
#endif
