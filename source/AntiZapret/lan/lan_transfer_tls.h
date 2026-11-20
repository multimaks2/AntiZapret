#pragma once
#if defined(ANTIZAPRET_LAN)
#include <winsock2.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string_view>
#include <memory>
#include <functional>
#include <string>

namespace lan::transfer
{
// Standalone building block: callers must restrict the connected socket to a
// currently visible, on-link private peer. This class never listens or connects.
// It owns the socket, and neither it nor the socket may be used on another thread.
// Set cancel from the UI thread; callbacks must also be able to return on cancel.
class TlsChannel
{
public:
    TlsChannel(SOCKET socket, bool server, const std::atomic_bool& cancel, std::function<bool()> permitted = {});
    ~TlsChannel();
    TlsChannel(const TlsChannel&) = delete;
    TlsChannel& operator=(const TlsChannel&) = delete;

    // The callback must display the WHOLE code locally and explicitly confirm
    // equality with the other PC's code. Never send the code over this socket.
    // Both confirmations are required before Read/Write will work.
    using Verify = bool (*)(void*, const std::string&);
    bool Confirm(Verify verify, void* context);
#ifdef ANTIZAPRET_LAN_TEST
    bool ConfirmForTest(Verify, void*, std::string_view version, std::uint32_t revision);
#endif
    using Deadline = std::chrono::steady_clock::time_point;
    void ReadUntil(void*, std::size_t, Deadline);
    void WriteUntil(const void*, std::size_t, Deadline);
    void Read(void* data, std::size_t size);
    void Write(const void* data, std::size_t size);
    void CheckCancelled() const;
    void BeginTransfer(bool receiver, bool speed = false);

private:
    bool ConfirmImpl(Verify, void*, std::string_view, std::uint32_t);
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
#endif
