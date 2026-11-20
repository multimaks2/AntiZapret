#include "lan/lan_discovery.h"
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

int main()
{
    lan::Discovery browser, visible, hidden;
    bool advertise = true, exchange = false;
    auto tick = [&] {
        browser.Update(true, exchange);
        visible.Update(exchange, advertise);
        hidden.Update(true, false);
    };
    auto until = [&](auto check, auto timeout) {
        const auto deadline = Clock::now() + timeout;
        do {
            tick();
            if (check()) return true;
            std::this_thread::sleep_for(10ms);
        } while (Clock::now() < deadline);
        return false;
    };
    auto require = [](bool success, const char* what) {
        std::printf("%s: %s\n", success ? "PASS" : "FAIL", what);
        return success;
    };
    // Bring up the passive responder before the others send their initial request.
    visible.Update(false, true);
    tick();
    if (!require(browser.Available() && visible.Available() && hidden.Available(), "eligible private adapter and multicast sockets")) return 1;
    if (!require(until([&] { return !browser.Peers().empty() && !hidden.Peers().empty(); }, 850ms),
        "two hidden browsers discover the opted-in client on entry")) return 2;
    const auto first = browser.Peers().front();
    if (!require(browser.Peers().size() == 1 && hidden.Peers().size() == 1 &&
        hidden.Peers().front().id == first.id && visible.Peers().empty() &&
        !first.name.empty() && first.local != 0 && first.remote != 0,
        "hidden clients absent; one named peer with validated on-link addresses")) return 3;
    if (!require(until([&] { return browser.Peers().size() == 1 && browser.Peers().front().expires > first.expires; }, 5600ms),
        "five-second refresh renews the same peer without duplicates")) return 4;
    advertise = false;
    if (!require(until([&] { return browser.Peers().empty() && hidden.Peers().empty(); }, 12000ms),
        "visibility off stops replies and both browsers expire the peer")) return 5;
    advertise = true;
    visible.Update(false, true);
    browser.Update(false, false);
    if (!require(!browser.Available() && until([&] { return browser.Peers().size() == 1; }, 850ms),
        "leaving closes hidden listener; re-entry discovers immediately")) return 6;
    exchange = true;
    if (!require(until([&] { return browser.Routes().size() == 1 && visible.Routes().size() == 1 &&
            browser.Routes().front().challenge != lan::Ticket{} && visible.Routes().front().challenge != lan::Ticket{}; }, 5600ms),
        "two opted-in clients record the observed addresses and interface")) return 7;
    if (!require(hidden.Routes().empty() && !hidden.Invite(browser.Routes().front().peer), "hidden client cannot invite")) return 8;
    auto sent = browser.Invite(browser.Routes().front().peer);
    std::optional<lan::Invitation> received;
    if (!require(sent && until([&] { received = visible.TakeInvitation(); return received.has_value(); }, 850ms) &&
        received->ticket == sent->ticket && hidden.TakeInvitation() == std::nullopt, "ticket targets only the discovered opted-in receiver")) return 9;
    if (!require(browser.RepeatInvite(*sent) && !until([&] { return visible.TakeInvitation().has_value(); }, 200ms),
        "replayed ticket is not re-offered")) return 10;
    browser.Update(true, false);
    if (!require(browser.Routes().empty() && !browser.RepeatInvite(*sent), "visibility-off invalidates outgoing routes immediately")) return 11;
    return 0;
}
