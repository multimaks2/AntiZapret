#include "lan_transfer.h"
#if defined(ANTIZAPRET_LAN)
#include <windows.h>
#include <winternl.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "bcrypt.lib")

extern "C" NTSYSAPI NTSTATUS NTAPI NtSetInformationFile(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

namespace lan::transfer
{
namespace
{
constexpr std::size_t EntryLimit = MaxEntries, NameLimit = 720, DepthLimit = 16;
constexpr std::uint64_t ByteLimit = 64ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t FsReserve = 4 * 1024 * 1024;
constexpr std::size_t ManifestBatch = 1024;
constexpr std::size_t Chunk = 64 * 1024;
constexpr std::array<char, 8> Protocol{'A','Z','L','A','N','F','3','\n'};
constexpr unsigned char Accepted = 1, Declined = 2, NoSpace = 3, InvalidDestination = 4,
                        Finished = 5, Saved = 6, Compare = 7;
constexpr auto CompareTimeout = std::chrono::minutes(15);
using Digest = std::array<unsigned char, 32>;

[[noreturn]] void Fail(const char* message, long code = 0)
{
    throw std::runtime_error(std::string(message) + " (" + std::to_string(code) + ")");
}

struct CaseLess
{
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        const int result = CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
            b.data(), static_cast<int>(b.size()), TRUE);
        if (!result) Fail("Compare Windows path names", GetLastError());
        return result == CSTR_LESS_THAN;
    }
};

void ValidateComponent(std::wstring_view name)
{
    if (name.empty() || name.size() > 120 || name.front() == L' ' || name.back() == L' ' || name.back() == L'.')
        Fail("Invalid path component");
    for (wchar_t c : name)
        if (c < 32 || c == 127 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos ||
            (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069)) Fail("Unsafe path character");
    auto stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
    std::wstring upper(stem);
    for (auto& c : upper) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" || upper == L"NUL" ||
        upper == L"CONIN$" || upper == L"CONOUT$" ||
        (upper.size() == 4 && (upper.compare(0, 3, L"COM") == 0 || upper.compare(0, 3, L"LPT") == 0) &&
            std::wstring_view(L"0123456789\u00b9\u00b2\u00b3").find(upper.back()) != std::wstring_view::npos))
        Fail("Reserved device name");
}

std::wstring Decode(const std::string& name)
{
    if (name.empty() || name.size() > NameLimit) Fail("Invalid UTF-8 path size");
    std::wstring result(name.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
                                     result.data(), static_cast<int>(result.size()));
    if (!n) Fail("Invalid UTF-8 path");
    result.resize(n);
    if (result.size() > 220) Fail("Relative path too long");
    std::size_t begin = 0;
    for (std::size_t depth = 1; ; ++depth)
    {
        if (depth > DepthLimit) Fail("Path nesting limit exceeded");
        const auto end = result.find(L'/', begin);
        ValidateComponent(std::wstring_view(result).substr(begin, end - begin));
        if (end == std::wstring::npos) break;
        begin = end + 1;
    }
    return result;
}

struct Path { std::wstring leaf; std::size_t parent; };
constexpr auto Root = SIZE_MAX;
std::vector<Path> ParseManifest(const std::vector<Item>& items, std::uint64_t& bytes)
{
    if (items.empty() || items.size() > EntryLimit) Fail("Invalid manifest count");
    std::map<std::wstring, std::size_t, CaseLess> seen;
    std::vector<Path> paths;
    paths.reserve(items.size());
    bytes = 0;
    for (const auto& item : items)
    {
        const auto name = Decode(item.name);
        const auto slash = name.find_last_of(L'/');
        std::size_t parent = Root;
        if (slash != std::wstring::npos)
        {
            const auto found = seen.find(name.substr(0, slash));
            if (found == seen.end() || !items[found->second].directory ||
                items[found->second].name != item.name.substr(0, item.name.rfind('/')))
                Fail("Missing or inconsistent parent directory");
            parent = found->second;
        }
        if (!seen.emplace(name, paths.size()).second) Fail("Duplicate or colliding path");
        if ((item.directory && item.size) || item.size > ByteLimit - bytes) Fail("Invalid file size or total limit");
        bytes += item.size;
        paths.push_back({name.substr(slash == std::wstring::npos ? 0 : slash + 1), parent});
    }
    return paths;
}

class Handle
{
    HANDLE value = INVALID_HANDLE_VALUE;
public:
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { Close(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value(std::exchange(h.value, INVALID_HANDLE_VALUE)) {}
    Handle& operator=(Handle&& h) noexcept
    {
        Close(); value = std::exchange(h.value, INVALID_HANDLE_VALUE); return *this;
    }
    void Close() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(std::exchange(value, INVALID_HANDLE_VALUE)); }
    HANDLE Get() const { return value; }
    explicit operator bool() const { return value != INVALID_HANDLE_VALUE; }
};

constexpr ULONG DontReparse = 0x1000; // OBJ_DONT_REPARSE
constexpr ULONG CaseInsensitive = 0x40; // OBJ_CASE_INSENSITIVE
constexpr ACCESS_MASK ReadDirectory = FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE;

void CheckHandle(HANDLE h, bool directory)
{
    FILE_ATTRIBUTE_TAG_INFO info{};
    if (!GetFileInformationByHandleEx(h, FileAttributeTagInfo, &info, sizeof(info)))
        Fail("Read opened object attributes", GetLastError());
    if (GetFileType(h) != FILE_TYPE_DISK || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        ((info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory)
        Fail("Reparse point or wrong object type");
}

Handle OpenChild(HANDLE parent, const std::wstring& name, bool directory, bool create = false,
                 bool wantWrite = false, bool allowMissing = false)
{
    const auto bytes = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    UNICODE_STRING text{bytes, bytes, const_cast<wchar_t*>(name.c_str())};
    OBJECT_ATTRIBUTES attrs{sizeof(attrs), parent, &text, CaseInsensitive | DontReparse, nullptr, nullptr};
    IO_STATUS_BLOCK ios{};
    HANDLE object = INVALID_HANDLE_VALUE;
    ACCESS_MASK access = directory ? ReadDirectory : (GENERIC_READ | SYNCHRONIZE);
    if (wantWrite && directory) access |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
    if (wantWrite && !directory) access |= DELETE;
    if (create) access |= DELETE | (directory ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY : GENERIC_WRITE);
    const auto status = NtCreateFile(&object, access, &attrs, &ios, nullptr,
        directory ? 0 : FILE_ATTRIBUTE_NORMAL, create ? 0 : FILE_SHARE_READ,
        create ? FILE_CREATE : FILE_OPEN, (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE) |
        FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, nullptr, 0);
    if (allowMissing && (static_cast<ULONG>(status) == 0xC0000034u ||
                         static_cast<ULONG>(status) == 0xC000000Fu)) return {};
    if (status < 0) Fail("Open path without following reparse points", status);
    Handle h(object);
    // Every created name is FILE_CREATE under an owned root, never open-or-create.
    if (!create) CheckHandle(h.Get(), directory);
    return h;
}

struct LocalPath
{
    std::wstring drive;
    std::vector<Handle> dirs;
    Handle file;
    HANDLE Folder() const { return dirs.back().Get(); }
};

LocalPath OpenLocal(const std::wstring& path, bool asDirectory)
{
    if (path.size() < 3 || path.size() > 4096 || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')))
        Fail("Expected an absolute local drive path");
    LocalPath result;
    result.drive = path.substr(0, 3);
    if (GetDriveTypeW(result.drive.c_str()) != DRIVE_FIXED) Fail("Only a local fixed drive is permitted");
    Handle root(CreateFileW((L"\\\\?\\" + result.drive).c_str(), ReadDirectory,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!root) Fail("Open drive root", GetLastError());
    CheckHandle(root.Get(), true);
    wchar_t fs[16]{};
    if (!GetVolumeInformationByHandleW(root.Get(), nullptr, 0, nullptr, nullptr, nullptr, fs, 16) ||
        std::wstring_view(fs) != L"NTFS") Fail("Only local NTFS is supported");
    result.dirs.push_back(std::move(root));
    if (path.size() == 3)
    {
        if (!asDirectory) Fail("Expected a file");
        return result;
    }
    for (std::size_t begin = 3; ; )
    {
        const auto end = path.find(L'\\', begin);
        const bool last = end == std::wstring::npos;
        const auto name = path.substr(begin, last ? end : end - begin);
        ValidateComponent(name);
        auto entry = OpenChild(result.Folder(), name, asDirectory || !last, false, asDirectory && last);
        if (last && !asDirectory) result.file = std::move(entry);
        else result.dirs.push_back(std::move(entry));
        if (last) return result;
        begin = end + 1;
    }
}

void RemoveOwned(Handle& file) noexcept
{
    FILE_DISPOSITION_INFO dispose{TRUE};
    if (file) SetFileInformationByHandle(file.Get(), FileDispositionInfo, &dispose, sizeof(dispose));
    file.Close();
}

void RenameOwned(HANDLE file, const std::wstring& leaf)
{
    // A simple leaf with no RootDirectory renames in the source handle's own directory.
    // No overwrite: only a file explicitly moved aside by us can be replaced.
    const auto bytes = leaf.size() * sizeof(wchar_t);
    FILE_RENAME_INFO info{};
    const auto length = (std::max)(sizeof(info), offsetof(FILE_RENAME_INFO, FileName) + bytes);
    std::vector<FILE_RENAME_INFO> storage((length + sizeof(info) - 1) / sizeof(info));
    auto* rename = storage.data();
    rename->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(reinterpret_cast<unsigned char*>(rename) + offsetof(FILE_RENAME_INFO, FileName), leaf.data(), bytes);
    IO_STATUS_BLOCK io{};
    const auto status = NtSetInformationFile(file, &io, rename, static_cast<ULONG>(length),
        static_cast<FILE_INFORMATION_CLASS>(10)); // FileRenameInformation
    if (status < 0) Fail("Rename verified LAN file", status);
}

std::wstring TemporaryLeaf()
{
    std::array<unsigned char, 16> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        Fail("Generate LAN temporary filename");
    std::wstring name = L".AntiZapret-LAN-";
    for (auto byte : random) { name += L"0123456789abcdef"[byte >> 4]; name += L"0123456789abcdef"[byte & 15]; }
    return name + L".tmp";
}

void CheckExistingName(HANDLE file, const std::wstring& expected)
{
    std::wstring path(32768, L'\0');
    const auto size = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!size || size >= path.size()) Fail("Resolve existing LAN path", GetLastError());
    path.resize(size);
    const auto leaf = path.substr(path.find_last_of(L'\\') + 1);
    if (CaseLess{}(leaf, expected) || CaseLess{}(expected, leaf)) Fail("Existing path uses an alias");
}

struct Stage
{
    struct Entry
    {
        Handle file, old;
        HANDLE parent = INVALID_HANDLE_VALUE;
        std::wstring leaf;
        bool owned = false, movedOld = false;
    };
    Handle root;
    std::vector<Entry> entries;
    bool committed = false;
    ~Stage()
    {
        if (!committed)
        {
            for (auto i = entries.rbegin(); i != entries.rend(); ++i)
            {
                if (i->owned) RemoveOwned(i->file);
                else i->file.Close();
                if (i->movedOld)
                {
                    // Keep the old data under its backup name if the path cannot be restored.
                    try { RenameOwned(i->old.Get(), i->leaf); } catch (...) {}
                }
            }
            RemoveOwned(root);
        }
    }
    void Inspect(HANDLE folder, const std::vector<Item>& manifest, const std::vector<Path>& paths,
                 const std::wstring& bundle, bool merge)
    {
        entries.reserve(paths.size());
        if (!bundle.empty()) root = OpenChild(folder, bundle, true, true);
        for (std::size_t i = 0; i < paths.size(); ++i)
        {
            const auto& path = paths[i];
            auto& entry = entries.emplace_back();
            entry.parent = path.parent == Root ? (root ? root.Get() : folder) : entries[path.parent].file.Get();
            entry.leaf = path.leaf;
            if (manifest[i].directory)
            {
                if (merge) entry.file = OpenChild(entry.parent, entry.leaf, true, false, true, true);
                if (entry.file) CheckExistingName(entry.file.Get(), entry.leaf);
                else { entry.file = OpenChild(entry.parent, entry.leaf, true, true); entry.owned = true; }
            }
            else if (merge)
            {
                entry.old = OpenChild(entry.parent, entry.leaf, false, false, true, true);
                if (entry.old)
                {
                    CheckExistingName(entry.old.Get(), entry.leaf);
                    FILE_STANDARD_INFO info{};
                    if (!GetFileInformationByHandleEx(entry.old.Get(), FileStandardInfo, &info, sizeof(info)) ||
                        info.NumberOfLinks != 1) Fail("Cannot safely replace existing linked file");
                }
            }
        }
    }
    void PrepareFiles(const std::vector<Item>& items, const std::vector<unsigned char>& selected, bool merge)
    {
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            auto& entry = entries[i];
            if (items[i].directory || !selected[i]) continue;
            entry.file = OpenChild(entry.parent, merge ? TemporaryLeaf() : entry.leaf, false, true);
            entry.owned = true;
        }
    }
    void Keep(const std::vector<Item>& items, bool merge)
    {
        if (merge)
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                auto& entry = entries[i];
                if (items[i].directory || !entry.file) continue;
                if (entry.old)
                {
                    RenameOwned(entry.old.Get(), TemporaryLeaf());
                    entry.movedOld = true;
                }
                RenameOwned(entry.file.Get(), entry.leaf);
            }
        committed = true;
        for (auto i = entries.rbegin(); i != entries.rend(); ++i)
        {
            if (i->movedOld) RemoveOwned(i->old);
            else i->old.Close();
            i->file.Close();
        }
        root.Close();
    }
};

std::wstring FinalPath(HANDLE folder)
{
    std::wstring path(32768, L'\0');
    const auto n = GetFinalPathNameByHandleW(folder, path.data(), static_cast<DWORD>(path.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_GUID);
    if (!n || n >= path.size()) Fail("Resolve opened volume directory", GetLastError());
    path.resize(n);
    if (path.back() != L'\\') path += L'\\';
    return path;
}

bool HasSpace(const std::wstring& folder, const std::vector<Item>& items, std::uint64_t total, std::uint64_t budget)
{
    ULARGE_INTEGER available{};
    DWORD spc{}, bps{}, ignored1{}, ignored2{};
    // Use the volume GUID of the pinned destination, not a mutable DOS alias.
    const auto volumeEnd = folder.find(L'\\', 4);
    if (volumeEnd == std::wstring::npos ||
        !GetDiskFreeSpaceW(folder.substr(0, volumeEnd + 1).c_str(), &spc, &bps, &ignored1, &ignored2) ||
        !GetDiskFreeSpaceExW(folder.c_str(), &available, nullptr, nullptr))
        Fail("Inspect available destination disk space", GetLastError());
    const auto cluster = static_cast<std::uint64_t>(spc) * bps;
    if (!cluster || cluster > 4 * 1024 * 1024) Fail("Unexpected NTFS cluster size");
    std::uint64_t required = FsReserve;
    for (const auto& item : items) required += item.directory ? cluster : ((item.size + cluster - 1) / cluster) * cluster;
    return total <= budget && required <= available.QuadPart;
}

class Hash
{
    BCRYPT_HASH_HANDLE h{};
public:
    Hash()
    {
        const auto status = BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0);
        if (status) Fail("Create SHA-256", status);
    }
    ~Hash() { BCryptDestroyHash(h); }
    Hash(const Hash&) = delete;
    Hash& operator=(const Hash&) = delete;
    void Add(unsigned char* data, ULONG count)
    {
        const auto status = BCryptHashData(h, data, count, 0);
        if (status) Fail("Hash file content", status);
    }
    std::array<unsigned char, 32> Finish()
    {
        std::array<unsigned char, 32> digest{};
        const auto status = BCryptFinishHash(h, digest.data(), static_cast<ULONG>(digest.size()), 0);
        if (status) Fail("Finish SHA-256", status);
        return digest;
    }
};

void Put(TlsChannel& ch, std::uint64_t value, std::size_t count)
{
    std::array<unsigned char, 8> network{};
    for (std::size_t i = count; i; --i) { network[i - 1] = static_cast<unsigned char>(value); value >>= 8; }
    ch.Write(network.data(), count);
}
std::uint64_t Get(TlsChannel& ch, std::size_t count)
{
    std::array<unsigned char, 8> network{};
    ch.Read(network.data(), count);
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < count; ++i) value = (value << 8) | network[i];
    return value;
}

unsigned char WaitForChoice(TlsChannel& ch)
{
    unsigned char result{};
    ch.ReadUntil(&result, 1, std::chrono::steady_clock::now() + CompareTimeout);
    return result;
}

Digest HashFile(TlsChannel& ch, HANDLE file, std::uint64_t remaining)
{
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) Fail("Seek LAN file", GetLastError());
    std::array<unsigned char, Chunk> data{};
    Hash hash;
    while (remaining)
    {
        ch.CheckCancelled();
        const auto count = static_cast<DWORD>((std::min)(remaining, static_cast<std::uint64_t>(data.size())));
        DWORD read{};
        if (!ReadFile(file, data.data(), count, &read, nullptr) || count != read) Fail("Hash LAN file", GetLastError());
        hash.Add(data.data(), count);
        remaining -= count;
    }
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) Fail("Rewind LAN file", GetLastError());
    return hash.Finish();
}

void SendManifest(TlsChannel& ch, const std::vector<Item>& items)
{
    ch.Write(Protocol.data(), Protocol.size());
    Put(ch, items.size(), 2);
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (i % ManifestBatch == 0) Put(ch, (std::min)(ManifestBatch, items.size() - i), 2);
        const auto& item = items[i];
        Put(ch, item.directory ? 1 : 0, 1);
        Put(ch, item.size, 8);
        Put(ch, item.name.size(), 2);
        ch.Write(item.name.data(), item.name.size());
    }
}
std::vector<Item> ReadManifest(TlsChannel& ch)
{
    decltype(Protocol)::value_type magic[Protocol.size()]{};
    ch.Read(magic, sizeof(magic));
    if (!std::equal(std::begin(magic), std::end(magic), Protocol.begin())) Fail("Unknown transfer protocol");
    const auto count = static_cast<std::size_t>(Get(ch, 2));
    if (!count || count > EntryLimit) Fail("Invalid wire entry count");
    std::vector<Item> items;
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (i % ManifestBatch == 0 && Get(ch, 2) != (std::min)(ManifestBatch, count - i))
            Fail("Invalid manifest batch count");
        const auto type = Get(ch, 1), size = Get(ch, 8);
        const auto length = static_cast<std::size_t>(Get(ch, 2));
        if (type > 1 || !length || length > NameLimit) Fail("Invalid wire entry header");
        Item item{std::string(length, '\0'), type != 0, size};
        ch.Read(item.name.data(), length);
        items.push_back(std::move(item));
    }
    return items;
}
}
std::uint64_t ValidateManifest(const std::vector<Item>& items)
{
    std::uint64_t bytes;
    ParseManifest(items, bytes);
    return bytes;
}

Result Send(TlsChannel& ch, const std::vector<Source>& sources, const Progress& progress)
{
    ch.BeginTransfer(false);
    if (sources.empty() || sources.size() > EntryLimit) Fail("Invalid source count");
    std::vector<Item> items;
    items.reserve(sources.size());
    for (const auto& source : sources) items.push_back({source.item.name, source.item.directory, 0});
    ValidateManifest(items);
    std::vector<LocalPath> opened(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        ch.CheckCancelled();
        if (items[i].directory) continue;
        opened[i] = OpenLocal(sources[i].localFile, false);
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(opened[i].file.Get(), &size) || size.QuadPart < 0) Fail("Inspect source size", GetLastError());
        items[i].size = static_cast<std::uint64_t>(size.QuadPart);
    }
    ValidateManifest(items);
    SendManifest(ch, items);
    auto response = WaitForChoice(ch);
    std::vector<unsigned char> selected(items.size(), 1), compare(items.size());
    if (response == Compare)
    {
        ch.Read(compare.data(), compare.size());
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (compare[i] > 1 || (compare[i] && items[i].directory)) Fail("Invalid comparison list");
            if (compare[i])
            {
                const auto digest = HashFile(ch, opened[i].file.Get(), items[i].size);
                ch.Write(digest.data(), digest.size());
            }
        }
        response = WaitForChoice(ch);
    }
    switch (response)
    {
    case Declined: return Result::Declined;
    case NoSpace: return Result::NoSpace;
    case InvalidDestination: return Result::InvalidDestination;
    case Accepted: break;
    default: Fail("Unknown offer response");
    }
    ch.Read(selected.data(), selected.size());
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (selected[i] > 1 || (items[i].directory && !selected[i])) Fail("Invalid transfer selection");
        if (selected[i]) total += items[i].size;
    }
    std::array<unsigned char, Chunk> buffer{};
    std::uint64_t transferred = 0;
    if (progress) progress(0, total);
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].directory || !selected[i]) continue;
        auto left = items[i].size;
        Hash hash;
        while (left)
        {
            ch.CheckCancelled();
            const auto bytes = static_cast<DWORD>((std::min)(left, static_cast<std::uint64_t>(buffer.size())));
            DWORD actual{};
            if (!ReadFile(opened[i].file.Get(), buffer.data(), bytes, &actual, nullptr) || actual != bytes)
                Fail("Read full source chunk", GetLastError());
            hash.Add(buffer.data(), bytes);
            ch.Write(buffer.data(), bytes);
            left -= bytes;
            transferred += bytes;
            if (progress) progress(transferred, total);
        }
        const auto digest = hash.Finish();
        ch.Write(digest.data(), digest.size());
    }
    Put(ch, Finished, 1);
    try
    {
        if (Get(ch, 1) != Saved) Fail("Invalid completion receipt");
    }
    catch (...) { Fail("No completion receipt; delivery is unknown"); }
    return Result::Saved;
}

Result Receive(TlsChannel& ch, const Offer& offer, const Progress& progress, const ResolveConflicts& resolve)
{
    ch.BeginTransfer(true);
    const auto items = ReadManifest(ch);
    std::uint64_t total;
    const auto paths = ParseManifest(items, total);
    const auto destination = offer ? offer(items, total) : std::nullopt;
    ch.CheckCancelled();
    if (!destination)
    {
        Put(ch, Declined, 1);
        return Result::Declined;
    }
    LocalPath target; // Must outlive the stage so all destination ancestors stay pinned.
    Stage stage;
    const bool merge = resolve && destination->bundle.empty();
    try
    {
        if (!destination->bundle.empty()) ValidateComponent(destination->bundle);
        target = OpenLocal(destination->folder, true);
        stage.Inspect(target.Folder(), items, paths, destination->bundle, merge);
    }
    catch (const std::runtime_error&)
    {
        Put(ch, InvalidDestination, 1);
        return Result::InvalidDestination;
    }
    std::vector<unsigned char> selected(items.size(), 1), compare(items.size());
    std::vector<Digest> offeredHashes(items.size());
    std::vector<Conflict> conflicts;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (!stage.entries[i].old) continue;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(stage.entries[i].old.Get(), &size) || size.QuadPart < 0)
            Fail("Read existing file size", GetLastError());
        compare[i] = static_cast<std::uint64_t>(size.QuadPart) == items[i].size;
        conflicts.push_back({i, items[i].name, false});
    }
    if (!conflicts.empty())
    {
        Put(ch, Compare, 1);
        ch.Write(compare.data(), compare.size());
        const auto deadline = std::chrono::steady_clock::now() + CompareTimeout;
        for (const auto& conflict : conflicts)
        {
            const auto i = conflict.index;
            if (compare[i]) ch.ReadUntil(offeredHashes[i].data(), offeredHashes[i].size(), deadline);
        }
        for (auto& conflict : conflicts)
        {
            const auto i = conflict.index;
            conflict.identical = compare[i] && offeredHashes[i] == HashFile(ch, stage.entries[i].old.Get(), items[i].size);
        }
        const auto choices = resolve(conflicts);
        ch.CheckCancelled();
        if (!choices) { Put(ch, Declined, 1); return Result::Declined; }
        if (choices->size() != conflicts.size()) Fail("Invalid local conflict choices");
        for (std::size_t i = 0; i < conflicts.size(); ++i) selected[conflicts[i].index] = (*choices)[i];
    }
    total = 0;
    std::vector<Item> planned;
    planned.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (!selected[i]) continue;
        total += items[i].size;
        if (!items[i].directory || stage.entries[i].owned) planned.push_back(items[i]);
    }
    if (!planned.empty() && !HasSpace(FinalPath(target.Folder()), planned, total, destination->maxBytes))
    {
        Put(ch, NoSpace, 1);
        return Result::NoSpace;
    }
    try { stage.PrepareFiles(items, selected, merge); }
    catch (const std::runtime_error&)
    {
        Put(ch, InvalidDestination, 1);
        return Result::InvalidDestination;
    }
    ch.CheckCancelled();
    Put(ch, Accepted, 1);
    ch.Write(selected.data(), selected.size());
    if (progress) progress(0, total);
    std::array<unsigned char, Chunk> buffer{};
    std::uint64_t transferred = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].directory || !selected[i]) continue;
        auto left = items[i].size;
        const auto file = stage.entries[i].file.Get();
        Hash hash;
        while (left)
        {
            const auto bytes = static_cast<DWORD>((std::min)(left, static_cast<std::uint64_t>(buffer.size())));
            ch.Read(buffer.data(), bytes);
            hash.Add(buffer.data(), bytes);
            ch.CheckCancelled();
            DWORD actual{};
            if (!WriteFile(file, buffer.data(), bytes, &actual, nullptr) || actual != bytes)
                Fail("Write full destination chunk", GetLastError());
            left -= bytes;
            transferred += bytes;
            if (progress) progress(transferred, total);
        }
        auto digest = hash.Finish();
        decltype(digest) expected{};
        ch.Read(expected.data(), expected.size());
        if (digest != expected || (compare[i] && digest != offeredHashes[i])) Fail("File SHA-256 mismatch");
        if (!FlushFileBuffers(file)) Fail("Flush destination file", GetLastError());
    }
    if (Get(ch, 1) != Finished) Fail("Invalid transfer completion marker");
    ch.CheckCancelled();
    stage.Keep(items, merge);
    try { Put(ch, Saved, 1); }
    catch (...) { return Result::SavedReceiptLost; }
    return Result::Saved;
}
}
#endif
