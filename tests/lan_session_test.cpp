#include "lan/lan_session.h"
#include "lan/lan_checked_selection.h"
#include <bcrypt.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <windows.h>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono;
using namespace std::chrono_literals;
using lan::Session;
using Phase = Session::Phase;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Eventually(const std::function<bool()>& fn)
{
    const auto deadline = steady_clock::now() + 8s;
    do { if (fn()) return true; std::this_thread::sleep_for(10ms); } while (steady_clock::now() < deadline);
    return false;
}
void Run(bool consent, bool revoke, bool wrongTicket, bool waitForLease = false)
{
    Session sender, receiver;
    lan::Ticket ticket{};
    Check(BCryptGenRandom(nullptr, ticket.data(), static_cast<ULONG>(ticket.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0, "Ticket RNG");
    auto otherTicket = ticket;
    if (wrongTicket) otherTicket[0] ^= 1; // same derived port, test the actual protected ticket gate
    receiver.StartLoopbackForTest(true, otherTicket);
    sender.StartLoopbackForTest(false, ticket);
    auto tick = [&] {
        sender.PumpLoopbackForTest(true); receiver.PumpLoopbackForTest(true);
    };
    const auto pair = Eventually([&] { tick(); return sender.Read().phase == Phase::Pair && receiver.Read().phase == Phase::Pair; });
    if (!pair) { puts(sender.Read().error.c_str()); puts(receiver.Read().error.c_str()); }
    Check(pair, "Both endpoints reach code comparison");
    Check(sender.Read().code.size() == 29 && sender.Read().code == receiver.Read().code, "Real TLS comparison code");
    sender.Decide(Phase::Consent, true); // wrong phase cannot approve anything
    sender.Decide(Phase::Pair, true);
    receiver.Decide(Phase::Pair, true);
    if (wrongTicket)
    {
        Check(Eventually([&] { tick(); return sender.Read().phase == Phase::Failed && receiver.Read().phase == Phase::Failed; }), "Wrong ticket rejected at both ends");
        Check(!sender.Read().speed && !receiver.Read().speed, "Rejected ticket cannot run speed test");
        return;
    }
    Check(Eventually([&] { tick(); return receiver.Read().phase == Phase::Consent; }), "Separate verified recipient consent");
    Check(receiver.Read().total == lan::transfer::MaxSpeedBytesPerDirection, "Consent states exact byte budget");
    Check(!sender.Read().speed && !receiver.Read().speed, "No speed result before consent");
    if (revoke)
    {
        if (!waitForLease) receiver.PumpLoopbackForTest(false);
        Check(Eventually([&] { sender.PumpLoopbackForTest(true); return receiver.Read().phase == Phase::Cancelled; }), "Lost visibility or stale UI lease cancels worker");
        Check(!receiver.Read().speed && !sender.Read().speed, "No result after cancellation");
        return;
    }
    receiver.Decide(Phase::Consent, consent);
    const auto expected = consent ? Phase::Finished : Phase::Declined;
    Check(Eventually([&] { tick(); return sender.Read().phase == expected && receiver.Read().phase == expected; }), "Both workers settle the speed decision");
    if (consent)
    {
        const auto up = sender.Read().speed, down = receiver.Read().speed;
        Check(up && down && up->upload == down->download && up->download == down->upload, "Two UI-ready measurements");
    }
}
void Selection()
{
    struct Entry { std::size_t depth, end; bool directory, checked; };
    std::vector<Entry> tree{{0, 6, true, false}, {1, 2, false, false}, {1, 5, true, false},
        {2, 4, false, true}, {2, 5, false, false}, {1, 6, true, false}, {0, 7, false, false}};
    Check(lan::CheckedSelection(tree) == std::vector<std::size_t>{0, 2, 3}, "Partial selection includes unchecked ancestors only");
    tree[5].checked = true; tree[3].checked = false;
    Check(lan::CheckedSelection(tree) == std::vector<std::size_t>{0, 5}, "Checked empty directory without unchecked siblings");
    tree[5].checked = false;
    Check(lan::CheckedSelection(tree).empty(), "Nothing checked");
    tree[4].depth = 3;
    bool invalid = false;
    try { lan::CheckedSelection(tree); } catch (const std::runtime_error&) { invalid = true; }
    Check(invalid, "Malformed tree is rejected");
}
void Files(const std::filesystem::path& base)
{
    namespace fs = std::filesystem;
    const auto root = fs::absolute(base).make_preferred() / (L"session-files-" + std::to_wstring(GetCurrentProcessId()));
    Check(fs::create_directory(root), "New file fixture");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    const auto selected = root / L"selected.txt", other = root / L"hidden.txt", dest = root / L"receive";
    Check(fs::create_directory(dest), "Destination fixture");
    std::ofstream(selected, std::ios::binary) << "chosen bytes";
    std::ofstream(other, std::ios::binary) << "not chosen";
    struct Entry { std::size_t depth, end; bool directory, checked; std::string relative; fs::path local; };
    const std::vector<Entry> tree{{0, 4, true, false, "root", {}}, {1, 2, false, false, "root/hidden.txt", other},
        {1, 4, true, false, "root/child", {}}, {2, 4, false, true, "root/child/selected.txt", selected}};
    std::vector<lan::transfer::Source> sources;
    for (const auto index : lan::CheckedSelection(tree))
    {
        const auto& e = tree[index];
        sources.push_back({{e.relative, e.directory}, e.local.native()});
    }
    for (int scenario : {0, 1, 2, 3})
    {
        Session sender, receiver;
        lan::Ticket ticket{};
        Check(!BCryptGenRandom(nullptr, ticket.data(), static_cast<ULONG>(ticket.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG), "File ticket random");
        const auto receiverOp = scenario == 3 ? lan::Operation::Speed : lan::Operation::Files;
        receiver.StartLoopbackForTest(true, ticket, receiverOp);
        sender.StartLoopbackForTest(false, ticket, lan::Operation::Files, sources);
        auto tick = [&] { sender.PumpLoopbackForTest(true); receiver.PumpLoopbackForTest(true); };
        Check(Eventually([&] { tick(); return sender.Read().phase == Phase::Pair && receiver.Read().phase == Phase::Pair; }), "File TLS pairing");
        sender.Decide(Phase::Pair, true); receiver.Decide(Phase::Pair, true);
        if (scenario == 3)
        {
            Check(Eventually([&] { tick(); return sender.Read().phase == Phase::Failed && receiver.Read().phase == Phase::Failed; }), "Wrong operation rejected before metadata");
            Check(receiver.Read().sample.empty(), "No file metadata on wrong operation");
            continue;
        }
        Check(Eventually([&] { tick(); return receiver.Read().phase == Phase::FileOffer; }), "File consent shown");
        auto offer = receiver.Read();
        Check(offer.itemCount == 3 && offer.total == 12 && offer.sample.size() == 3 && offer.sample[2] == "root/child/selected.txt", "Only selected file and needed ancestors offered");
        Check(fs::is_empty(dest), "Before consent no destination files");
        auto wrong = ticket; wrong[0] ^= 1;
        receiver.ChooseDestination(wrong, dest.native());
        Check(receiver.Read().phase == Phase::FileOffer && fs::is_empty(dest), "Stale destination decision ignored");
        if (scenario == 2)
        {
            sender.Cancel();
            Check(Eventually([&] { tick(); return sender.Read().phase == Phase::Cancelled; }), "Sender can cancel before content");
            receiver.Cancel();
            Check(fs::is_empty(dest), "Cancelled file proposal leaves nothing");
            continue;
        }
        receiver.ChooseDestination(ticket, scenario == 1 ? std::optional<std::wstring>{dest.native()} : std::nullopt);
        const auto expected = scenario == 1 ? Phase::Finished : Phase::Declined;
        Check(Eventually([&] { tick(); return sender.Read().phase == expected && receiver.Read().phase == expected; }), "File decision settled both ways");
        if (scenario == 1)
        {
            const auto bundle = dest / offer.bundle / L"root";
            std::ifstream stream(bundle / L"child" / L"selected.txt", std::ios::binary);
            const std::string data((std::istreambuf_iterator<char>(stream)), {});
            Check(data == "chosen bytes" && !fs::exists(bundle / L"hidden.txt"), "Checked file only, exact bytes and no sibling");
            Check(sender.Read().done == 12 && receiver.Read().done == 12, "Both file progress counters");
            stream.close();
            fs::remove_all(dest / offer.bundle);
        }
        else Check(fs::is_empty(dest), "Declined file proposal leaves nothing");
    }
    puts("PASS: file worker consent/refusal, stale choice, partial tree, bytes, cancellation and wrong operation");
}

int wmain(int argc, wchar_t** argv)
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 1;
    int exit = 0;
    try
    {
        Check(!lan::IsPrivateDirect({"test", 0x7f000001, 0x7f000001, 0, steady_clock::now() + 1s}), "Real policy rejects loopback");
        Run(true, false, false); puts("PASS: TCP worker/TLS/SAS/consent, speed both ways");
        Run(false, false, false); puts("PASS: receiver refusal");
        Run(false, true, false); puts("PASS: visibility revocation cancels consent");
        Run(false, true, false, true); puts("PASS: expired UI lease cancels consent");
        Run(false, false, true); puts("PASS: wrong invitation stops both sides");
        Check(argc == 2, "Pass an existing scratch directory on local NTFS");
        Files(argv[1]);
        Selection(); puts("PASS: partial picker selection, ancestors and malformed tree");
    }
    catch (const std::exception& e) { puts(e.what()); exit = 1; }
    WSACleanup();
    return exit;
}
