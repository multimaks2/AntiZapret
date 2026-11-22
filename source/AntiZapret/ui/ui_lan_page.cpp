#include "ui/ui_lan_page.h"

#ifdef ANTIZAPRET_LAN
#define NOMINMAX
#include "imgui.h"
#include "imgui_internal.h"
#include "lan/lan_discovery.h"
#include "lan/lan_session.h"
#include "lan/lan_checked_selection.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "ui/ui_smooth_scroll.h"
#include <algorithm>

#include <Windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <iterator>
#include <string_view>
#include <thread>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
constexpr size_t kMaxPaths = 65536;
constexpr size_t kMaxDepth = 32;
constexpr size_t kMaxPathChars = 4096;

struct Entry
{
    fs::path source;
    std::wstring relative;
    uint64_t size = 0;
    bool directory = false;
    size_t depth = 0;
    size_t end = 0;
    bool checked = true;
    bool expanded = false;
};

struct Selection
{
    std::vector<Entry> entries;
    uint64_t bytes = 0;
    std::string error;
    bool cancelled = false;
};

struct ComApartment
{
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ComApartment() { if (SUCCEEDED(status)) CoUninitialize(); }
};

struct ClipboardLock
{
    bool open = OpenClipboard(nullptr) != FALSE;
    ~ClipboardLock() { if (open) CloseClipboard(); }
};

struct CoMemString
{
    PWSTR value = nullptr;
    ~CoMemString() { CoTaskMemFree(value); }
};

struct Cancelled {};
struct SelectionError
{
    const char* text;
};

void CheckCancelled(const std::atomic_bool& cancel)
{
    if (cancel.load(std::memory_order_relaxed)) throw Cancelled{};
}

std::string FileSizeLabel(uint64_t bytes)
{
    constexpr const char* units[] = {"Б", "КБ", "МБ", "ГБ", "ТБ", "ПБ", "ЭБ"};
    size_t unit = 0;
    uint64_t divisor = 1;
    while (unit + 1 < std::size(units) && bytes / divisor >= 1024)
    {
        divisor *= 1024;
        ++unit;
    }
    if (!unit) return std::to_string(bytes) + " Б";
    auto whole = bytes / divisor;
    auto fraction = ((bytes % divisor) * 10 + divisor / 2) / divisor;
    if (fraction == 10) { ++whole; fraction = 0; }
    if (whole == 1024 && unit + 1 < std::size(units)) { whole = 1; ++unit; }
    return std::to_string(whole) + (fraction ? "," + std::to_string(fraction) : "") + " " + units[unit];
}

std::string Utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
        nullptr, 0, nullptr, nullptr);
    if (!bytes) return "(некорректное имя Unicode)";
    std::string result(static_cast<size_t>(bytes), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
        result.data(), bytes, nullptr, nullptr)) return "(некорректное имя Unicode)";
    return result;
}

DWORD ReadAttributes(const fs::path& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
        throw SelectionError{"Не удалось прочитать свойства одного из объектов."};
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        throw SelectionError{"Выбор содержит ссылку, точку подключения или другой reparse point."};
    return attrs;
}

bool WindowsNameEqual(std::wstring_view a, std::wstring_view b)
{
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

void ValidateComponent(std::wstring_view name)
{
    const SelectionError invalid{"Выбор содержит имя, неподдерживаемое для обмена Windows."};
    if (name.empty() || name.size() > 255 || name.back() == L'.' || name.back() == L' ')
        throw invalid;
    for (const auto c : name)
        if (c < 32 || c == 127 || c == 34 || c == 92 ||
            std::wstring_view(L"<>:/|?*").find(c) != std::wstring_view::npos)
            throw invalid;
    auto stem = name.substr(0, name.find(L'.'));
    while (!stem.empty() && stem.back() == L' ') stem.remove_suffix(1);
    for (const std::wstring_view device : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
        if (WindowsNameEqual(stem, device)) throw invalid;
    if (stem.size() == 4 && (WindowsNameEqual(stem.substr(0, 3), L"COM") ||
         WindowsNameEqual(stem.substr(0, 3), L"LPT")) &&
         std::wstring_view(L"0123456789¹²³").find(stem.back()) != std::wstring_view::npos)
        throw invalid;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(),
        static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr)) throw invalid;
}

void ValidateRoot(const fs::path& path)
{
    const auto drive = path.root_name().native();
    // Не открываем UNC, устройства и сетевые диски при подготовке списка.
    if (drive.size() != 2 || drive[1] != L':' || !path.has_root_directory() ||
        !((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')))
        throw SelectionError{"Выберите файлы или папки на локальном диске."};
    const auto root = path.root_path();
    const auto type = GetDriveTypeW(root.c_str());
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_CDROM && type != DRIVE_RAMDISK)
        throw SelectionError{"Выберите файлы или папки на локальном диске."};
    wchar_t driveTarget[4096] = {};
    if (!QueryDosDeviceW(drive.c_str(), driveTarget, static_cast<DWORD>(std::size(driveTarget))) ||
        std::wstring_view(driveTarget).find(LR"(\Device\)") != 0)
        throw SelectionError{"Подставные буквы дисков не поддерживаются."};
    if (path.filename().empty()) throw SelectionError{"Нельзя выбрать диск целиком."};
    fs::path current = root;
    ReadAttributes(current);
    for (const auto& part : path.relative_path())
    {
        ValidateComponent(part.native());
        current /= part;
        ReadAttributes(current);
    }
}

void Collect(Selection& output, const fs::path& source, const fs::path& relative,
    size_t depth, const std::atomic_bool& cancel)
{
    CheckCancelled(cancel);
    if (output.entries.size() >= kMaxPaths)
        throw SelectionError{"В одном выборе допускается до 65 536 файлов и папок. Разделите выбор на части."};
    if (depth >= kMaxDepth)
        throw SelectionError{"Папки вложены слишком глубоко: предел — 32 уровня, включая выбранную папку."};
    if (source.native().size() > kMaxPathChars || relative.native().size() > kMaxPathChars)
        throw SelectionError{"Путь длиннее 4 096 символов. Сократите имена папок или выберите более короткий путь."};

    ValidateComponent(relative.filename().native());
    const auto attrs = ReadAttributes(source);
    const bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    uint64_t size = 0;
    if (!isDir)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &data) ||
            (data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)))
            throw SelectionError{"Не удалось прочитать размер обычного файла."};
        size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        if (size > (std::numeric_limits<uint64_t>::max)() - output.bytes)
            throw SelectionError{"Суммарный размер слишком велик."};
    }
    const size_t index = output.entries.size();
    output.entries.push_back({source, relative.native(), size, isDir, depth, index + 1});
    output.bytes += size;
    if (!isDir) return;

    // Только метаданные: содержимое файлов на этом этапе не открывается.
    for (const auto& child : fs::directory_iterator(source))
        Collect(output, child.path(), relative / child.path().filename(), depth + 1, cancel);
    output.entries[index].end = output.entries.size();
}

std::vector<fs::path> ExplorerClipboard()
{
    const ClipboardLock clipboard;
    if (!clipboard.open) throw SelectionError{"Буфер обмена занят. Повторите вставку."};
    const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (!drop) throw SelectionError{"Скопируйте файлы или папки в Проводнике Windows."};
    const auto count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
    if (!count || count > kMaxPaths) throw SelectionError{"Недопустимое число объектов в буфере обмена."};
    std::vector<fs::path> paths;
    paths.reserve(count);
    for (UINT i = 0; i < count; ++i)
    {
        const auto length = DragQueryFileW(drop, i, nullptr, 0);
        if (!length || length > kMaxPathChars) throw SelectionError{"Недопустимый путь в буфере обмена."};
        std::wstring value(length + 1, L'\0');
        if (DragQueryFileW(drop, i, value.data(), length + 1) != length)
            throw SelectionError{"Не удалось прочитать путь из буфера обмена."};
        value.resize(length);
        paths.emplace_back(std::move(value));
    }
    return paths;
}

std::vector<fs::path> Pick(bool folders, bool multi = true)
{
    const ComApartment apartment;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(apartment.status) || FAILED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.GetAddressOf()))))
        throw SelectionError{"Не удалось открыть системное окно выбора."};
    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options)) || FAILED(dialog->SetOptions(options | FOS_FORCEFILESYSTEM |
        (multi ? FOS_ALLOWMULTISELECT : 0) | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_NODEREFERENCELINKS |
        FOS_DONTADDTORECENT | (folders ? FOS_PICKFOLDERS : 0))))
        throw SelectionError{"Не удалось настроить системное окно выбора."};
    const HRESULT shown = dialog->Show(nullptr);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) throw Cancelled{};
    if (FAILED(shown)) throw SelectionError{"Не удалось показать системное окно выбора."};
    ComPtr<IShellItemArray> items;
    DWORD count{};
    if (FAILED(dialog->GetResults(items.GetAddressOf())) || FAILED(items->GetCount(&count)) ||
        !count || count > kMaxPaths)
        throw SelectionError{"Недопустимое число выбранных объектов."};
    std::vector<fs::path> paths;
    paths.reserve(count);
    for (DWORD i = 0; i < count; ++i)
    {
        ComPtr<IShellItem> item;
        CoMemString text;
        if (FAILED(items->GetItemAt(i, item.GetAddressOf())) ||
            FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &text.value)) || !text.value)
            throw SelectionError{"Не удалось получить локальный путь выбранного объекта."};
        paths.emplace_back(text.value);
    }
    return paths;
}

// Рабочий поток не обращается к состоянию интерфейса.
enum class Source { Clipboard, Files, Folders };
Selection Prepare(Source source, const std::atomic_bool& cancel)
{
    try
    {
        auto paths = source == Source::Clipboard ? ExplorerClipboard() : Pick(source == Source::Folders);
        Selection result;
        std::vector<std::wstring> roots;
        for (const auto& path : paths)
        {
            CheckCancelled(cancel);
            ValidateRoot(path);
            const auto name = path.filename().native();
            for (const auto& previous : roots)
                if (WindowsNameEqual(previous, name))
                    throw SelectionError{"В выборе есть объекты с совпадающими именами верхнего уровня."};
            roots.push_back(name);
            Collect(result, path, path.filename(), 0, cancel);
        }
        CheckCancelled(cancel);
        return result;
    }
    catch (const Cancelled&) { Selection result; result.cancelled = true; return result; }
    catch (const SelectionError& e) { Selection result; result.error = e.text; return result; }
    catch (const std::exception&) { Selection result; result.error = "Не удалось подготовить список объектов."; return result; }
}

struct Page
{
    Selection selected;
    std::string status;
    std::shared_ptr<std::atomic_bool> cancel;
    std::future<Selection> pending;

    void Start(Source source)
    {
        if (pending.valid()) return;
        try
        {
            cancel = std::make_shared<std::atomic_bool>(false);
            // Уничтожение future не ждёт закрытия системного окна.
            std::packaged_task<Selection()> job([source, flag = cancel] { return Prepare(source, *flag); });
            auto result = job.get_future();
            std::thread(std::move(job)).detach();
            pending = std::move(result);
            status = source == Source::Clipboard ? "Читается буфер обмена…" :
                "Выберите объекты в системном окне…";
        }
        catch (const std::exception&) { status = "Не удалось запустить подготовку списка."; }
    }

    void Poll()
    {
        if (!pending.valid() || pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto result = pending.get();
        if (result.cancelled || cancel->load(std::memory_order_relaxed)) status = "Подготовка отменена.";
        else if (!result.error.empty()) status = result.error;
        else { selected = std::move(result); status.clear(); }
        cancel.reset();
    }
};

struct LanPageState
{
    lan::Discovery discovery;
    lan::Session session;
    Page selection;
    std::string target, targetId;
    std::future<std::optional<std::wstring>> destinationPick;
    lan::Ticket destinationTicket{};
    const char* networkNotice = nullptr;
    bool restoreFailed = false;
    bool visible = false;
    float visibilityMix = 0.f;
    bool active = false;
};

LanPageState& State()
{
    static LanPageState state;
    return state;
}

void ComputerIcon(float width, float size, const UiThemeColors& colors)
{
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float x = at.x + (width - size) * .5f;
    const float y = at.y;
    const auto point = [=](float u, float v) { return ImVec2(x + size * u, y + size * v); };
    const auto ink = ImGui::GetColorU32(colors.textPrimary);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(point(0.f, 0.f), point(1.f, 1.f), ImGui::GetColorU32(colors.badgeBg), size * .24f);
    draw->AddRectFilled(point(.15f, .20f), point(.85f, .68f), ImGui::GetColorU32(colors.inputBg), size * .06f);
    const float stroke = size * .035f;
    draw->AddRect(point(.15f, .20f), point(.85f, .68f), ink, size * .06f, 0, stroke);
    draw->AddLine(point(.50f, .69f), point(.50f, .80f), ink, stroke);
    draw->AddLine(point(.35f, .80f), point(.65f, .80f), ink, stroke);
    draw->AddCircleFilled(point(.50f, .61f), size * .017f, ink);
    ImGui::Dummy({width, size});
}
}

void UpdateLanPage(bool active)
{
    auto& state = State();
    state.active = active;
    state.discovery.Update(active, state.visible);
    state.session.Update(state.discovery, active && state.visible);
}

void DrawLanSession(LanPageState& state, ThemeManager& theme)
{
    auto& session = state.session;
    if (state.destinationPick.valid() && state.destinationPick.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        try { session.ChooseDestination(state.destinationTicket, state.destinationPick.get()); }
        catch (...) { session.ChooseDestination(state.destinationTicket, {}); }
    }
    using Phase = lan::Session::Phase;
    const auto status = session.Read();
    const bool speed = status.operation == lan::Operation::Speed;
    const bool show = status.phase != Phase::Idle && status.phase != Phase::Connecting &&
        !(speed && status.phase == Phase::Measuring);
    constexpr auto popup = "LAN-сеанс##lan_session";
    static Phase lastPhase = Phase::Idle;
    static lan::Ticket lastTicket{};
    static UiSmoothScroll offerScroll;
    static std::size_t lastConflict = 0;
    static bool applyConflictToAll = false;
    if (status.phase == Phase::FileConflict &&
        (lastPhase != status.phase || lastTicket != status.ticket || lastConflict != status.conflict.index))
        applyConflictToAll = false;
    lastConflict = status.conflict.index;
    if (lastTicket != status.ticket) offerScroll = {};
    if (show && (lastPhase != status.phase || lastTicket != status.ticket)) ImGui::OpenPopup(popup);
    lastPhase = status.phase;
    lastTicket = status.ticket;
    if (!ImGui::IsPopupOpen(popup)) return;
    const auto colors = theme.GetColors();
    const auto accents = theme.GetAccents();
    const bool light = UiCommon::IsLightTheme(colors);
    const auto bg = light ? ImVec4(.90f, .90f, .92f, .98f) : UiCommon::WithAlpha(colors.tileBg, .98f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, UiCommon::WithAlpha(colors.tileBorder, light ? .55f : .40f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.f, 16.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10.f, 8.f});
    const auto popStyle = [] { ImGui::PopStyleVar(5); ImGui::PopStyleColor(2); };
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(480.f, viewport->WorkSize.x * .94f), 0.f});
    if (!ImGui::BeginPopupModal(popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
    {
        popStyle();
        return;
    }
    if (!show)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        popStyle();
        return;
    }
    constexpr ImVec2 buttonSize{0.f, UiMetrics::kSmallBtnHeight};
    ImGui::TextColored(colors.textPrimary, "LAN-сеанс");
    ImGui::Dummy({0.f, 4.f});
    switch (status.phase)
    {
    case Phase::Idle:
    case Phase::Connecting: break;
    case Phase::Pair:
        if (status.code.empty())
        {
            ImGui::TextUnformatted("Ожидаем подтверждения второго компьютера…");
            break;
        }
        ImGui::TextWrapped("Сравните весь код с кодом на втором компьютере. Подтвердите на обоих ПК только при полном совпадении.");
        ImGui::TextUnformatted(status.code.c_str());
        if (UiCommon::AccentButton("Коды совпадают##lan_pair", buttonSize, accents.ok, colors)) session.Decide(Phase::Pair, true);
        ImGui::SameLine();
        if (UiCommon::SecondaryButton("Не совпадают##lan_pair", buttonSize, colors)) session.Decide(Phase::Pair, false);
        break;
    case Phase::Consent:
        ImGui::TextWrapped("Второй компьютер предлагает проверить скорость. Будет передано до %.1f МиБ в каждом направлении (до 10 секунд на направление). Файлы не создаются.",
            static_cast<double>(status.total) / (1024.0 * 1024.0));
        if (UiCommon::AccentButton("Согласиться на замер##lan_speed", buttonSize, accents.ok, colors)) session.Decide(Phase::Consent, true);
        ImGui::SameLine();
        if (UiCommon::SecondaryButton("Отклонить##lan_speed", buttonSize, colors)) session.Decide(Phase::Consent, false);
        break;
    case Phase::FileOffer:
    {
        ImGui::TextWrapped("Второй компьютер предлагает %zu объектов (%s). После выбора папки будет проверено место.",
            status.itemCount, FileSizeLabel(status.total).c_str());
        const float row = ImGui::GetTextLineHeightWithSpacing();
        const float height = std::min(row * 8.f, viewport->WorkSize.y * .35f);
        if (row * status.sample.size() > height)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.inputBg);
            offerScroll.Draw("##incoming_files", {ImGui::GetContentRegionAvail().x, height}, ImGui::GetIO().DeltaTime,
                [&](float listWidth) {
                    ImGuiListClipper clip;
                    clip.Begin(static_cast<int>(status.sample.size()), row);
                    while (clip.Step())
                        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                        {
                            const auto& name = status.sample[i];
                            ImGui::TextUnformatted(name.c_str());
                            if (ImGui::IsItemHovered() && ImGui::CalcTextSize(name.c_str()).x > listWidth)
                                ImGui::SetTooltip("%s", name.c_str());
                        }
                }, 1.f, nullptr, true, true);
            ImGui::PopStyleColor();
        }
        else
            for (const auto& name : status.sample) ImGui::TextWrapped("%s", name.c_str());
        ImGui::TextWrapped("Файлы сохранятся в выбранную папку. Существующие файлы не будут заменены.");
        ImGui::BeginDisabled(state.destinationPick.valid());
        if (UiCommon::AccentButton("Принять и выбрать папку…##lan_files", buttonSize, accents.ok, colors))
        {
            try
            {
                std::packaged_task<std::optional<std::wstring>()> task([]() -> std::optional<std::wstring> {
                    try { auto folders = Pick(true, false); if (folders.size() == 1) return folders.front().native(); }
                    catch (...) {}
                    return {};
                });
                auto next = task.get_future();
                std::thread(std::move(task)).detach();
                state.destinationPick = std::move(next);
                state.destinationTicket = status.ticket;
            }
            catch (...) { session.ChooseDestination(status.ticket, {}); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UiCommon::SecondaryButton("Отклонить##lan_files", buttonSize, colors)) session.ChooseDestination(status.ticket, {});
        if (state.destinationPick.valid()) ImGui::TextWrapped("Выберите папку в системном окне или закройте его. Ожидание ограничено.");
        break;
    }
    case Phase::CheckingFiles:
        ImGui::TextWrapped("Проверяем файлы в выбранной папке…");
        if (UiCommon::SecondaryButton("Отменить передачу##lan_check", buttonSize, colors)) session.Cancel();
        break;
    case Phase::FileConflict:
    {
        ImGui::TextColored(colors.textPrimary, "Файл уже есть в папке");
        ImGui::TextWrapped("%s", status.conflict.name.c_str());
        ImGui::TextWrapped("%s", status.conflict.identical ? "Содержимое совпадает." : "Содержимое отличается.");
        ImGui::Text("Осталось в этой группе: %u", static_cast<unsigned>(status.conflictsOfKind));
        ImGui::Checkbox("Применить ко всем файлам этой группы##lan_conflict", &applyConflictToAll);
        if (UiCommon::SecondaryButton("Заменить##lan_conflict", buttonSize, colors))
            session.ChooseConflict(status.ticket, status.conflict.index, true, applyConflictToAll);
        ImGui::SameLine();
        if (UiCommon::SecondaryButton("Пропустить##lan_conflict", buttonSize, colors))
            session.ChooseConflict(status.ticket, status.conflict.index, false, applyConflictToAll);
        ImGui::SameLine();
        if (UiCommon::SecondaryButton("Отменить передачу##lan_conflict", buttonSize, colors)) session.Cancel();
        break;
    }
    case Phase::Transferring:
        ImGui::Text("%s: %s / %s", status.sending ? "Отправка" : "Приём", FileSizeLabel(status.done).c_str(), FileSizeLabel(status.total).c_str());
        ImGui::ProgressBar(status.total ? static_cast<float>(status.done) / static_cast<float>(status.total) : 0.f);
        break;
    case Phase::Measuring:
        ImGui::TextUnformatted(status.sending ? "Замер: отправка" : "Замер: приём");
        ImGui::ProgressBar(status.total ? static_cast<float>(status.done) / static_cast<float>(status.total) : 0.f);
        break;
    case Phase::Finished:
        if (status.operation == lan::Operation::Files)
        {
            if (!status.error.empty()) ImGui::TextWrapped("%s", status.error.c_str());
            else if (!status.sending) ImGui::TextUnformatted("Файлы сохранены в выбранную папку.");
            else ImGui::TextUnformatted("Получатель подтвердил сохранение файлов.");
        }
        else if (status.speed)
        {
            using Seconds = std::chrono::duration<double>;
            const double megabits = status.speed->bytesPerDirection * 8.0 / 1000000.0;
            ImGui::TextWrapped("Скорость: отправка %.1f Мбит/с, приём %.1f Мбит/с.",
                megabits / Seconds(status.speed->upload).count(), megabits / Seconds(status.speed->download).count());
        }
        break;
    case Phase::Declined: ImGui::TextWrapped("Соединение или предложение не подтверждены."); break;
    case Phase::Cancelled: ImGui::TextWrapped("Сеанс отменён или видимый LAN-маршрут потерян."); break;
    case Phase::Failed:
        if (status.error.find("version mismatch") != std::string::npos || status.error.find("revision mismatch") != std::string::npos ||
            status.error.find("compatibility protocol") != std::string::npos)
            ImGui::TextWrapped("На двух ПК нужны одинаковые версии AntiZapret и LAN-протокола.");
        else ImGui::TextWrapped("LAN-сеанс не завершён: %s", status.error.c_str());
        break;
    }
    if (!speed && status.sending && status.phase == Phase::Transferring)
        if (UiCommon::AccentButton("Отменить передачу", buttonSize, accents.fail, colors)) session.Cancel();
    if (status.phase == Phase::Finished || status.phase == Phase::Declined ||
        status.phase == Phase::Cancelled || status.phase == Phase::Failed)
        if (UiCommon::SecondaryButton("Закрыть", {112.f, buttonSize.y}, colors)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    popStyle();
}

void DrawLanPage(ThemeManager& theme, FontManager&)
{
    auto& state = State();
    auto& page = state.selection;
    page.Poll();
    constexpr float toggleWidth = 40.f;
    constexpr float toggleHeight = 22.f;
    const char* visibility = "Видимость";
    const auto& style = ImGui::GetStyle();
    const auto origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float rowHeight = std::max(toggleHeight, ImGui::GetFrameHeight());
    const float labelY = origin.y + (rowHeight - ImGui::GetTextLineHeight()) * .5f;
    ImGui::SetCursorScreenPos({origin.x, labelY});
    ImGui::TextUnformatted("Локальная сеть");
    const float switchX = origin.x + width - toggleWidth;
    ImGui::SetCursorScreenPos({switchX - style.ItemInnerSpacing.x - ImGui::CalcTextSize(visibility).x, labelY});
    ImGui::TextUnformatted(visibility);
    ImGui::SetCursorScreenPos({switchX, origin.y + (rowHeight - toggleHeight) * .5f});
    state.visibilityMix = UiCommon::AnimateMix(state.visibilityMix, state.visible, ImGui::GetIO().DeltaTime, 10.f);
    ImGui::BeginDisabled(!state.active);
    if (UiCommon::ToggleSwitch("##lan_visibility", state.visibilityMix, theme.GetColors()))
    {
        state.visible = !state.visible;
        state.networkNotice = nullptr;
        state.discovery.Update(state.active, state.visible);
        if (state.visible && !state.restoreFailed)
        {
            bool tracked = false;
            if (const auto* uri = state.discovery.PrepareNetworkSettings(tracked))
            {
                const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", uri,
                    nullptr, nullptr, SW_SHOWNORMAL));
                if (result <= 32)
                {
                    if (tracked) state.discovery.RestoreNetworkProfile();
                    state.networkNotice = "Не удалось открыть параметры Windows. Для LAN нужна доверенная частная сеть.";
                }
                else
                    state.networkNotice = tracked
                        ? "Выберите «Частная сеть» только для доверенной сети. При выключении видимости вернётся «Общедоступная сеть»."
                        : "Найдено несколько общедоступных подключений. Выберите доверенную сеть в Windows; её профиль нужно будет вернуть вручную.";
            }
        }
        if (!state.visible)
        {
            state.session.Cancel();
            state.restoreFailed = !state.discovery.RestoreNetworkProfile();
            state.discovery.Update(state.active, false);
        }
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos({origin.x, origin.y + rowHeight + style.ItemSpacing.y});
    if (state.restoreFailed)
    {
        ImGui::TextWrapped("Windows не вернула общедоступный профиль. Повторите попытку или верните его в параметрах сети.");
        if (!state.visible && ImGui::Button("Восстановить профиль сети"))
            state.restoreFailed = !state.discovery.RestoreNetworkProfile();
    }
    if (state.networkNotice && !state.discovery.Available()) ImGui::TextWrapped("%s", state.networkNotice);
    ImGui::TextWrapped("Другие клиенты увидят имя компьютера только при включённой видимости. Для передачи и замера оставьте вкладку LAN открытой на обоих ПК.");
    DrawLanSession(state, theme);
    const auto& peers = state.discovery.Peers();
    const auto sessionStatus = state.session.Read();
    using Phase = lan::Session::Phase;
    const bool testingSpeed = sessionStatus.operation == lan::Operation::Speed &&
        (sessionStatus.phase == Phase::Connecting || sessionStatus.phase == Phase::Pair ||
         sessionStatus.phase == Phase::Consent || sessionStatus.phase == Phase::Measuring);

    const bool busy = page.pending.valid();
    bool openPicker = false;
    const float frame = ImGui::GetFrameHeight();
    const auto colors = theme.GetColors();
    const float spacing = frame * .55f;
    const float padding = frame * .62f;
    const float buttonHeight = frame * 1.20f;
    if (!peers.empty()) ImGui::Dummy({0.f, frame * .55f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {spacing, spacing});
    if (ImGui::BeginChild("##lan_peers", {0, 0}))
    {
        const float gridWidth = ImGui::GetContentRegionAvail().x;
        const float preferredSide = std::max(frame * 9.4f, ImGui::CalcTextSize("Проверить скорость").x + padding * 3.f);
        const float side = std::min(preferredSide, gridWidth);
        const int columns = std::max(1, static_cast<int>((gridWidth + spacing) / (side + spacing)));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, frame * .70f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {padding, padding});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, buttonHeight * .37f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
        ImGui::PushStyleColor(ImGuiCol_Border, colors.tileBorder);
        for (size_t index = 0; index < peers.size(); ++index)
        {
            const auto& peer = peers[index];
            if (index % columns != 0) ImGui::SameLine();
            ImGui::PushID(peer.id.c_str());
            if (ImGui::BeginChild("##peer", {side, side}, ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                const auto cardOrigin = ImGui::GetCursorScreenPos();
                const float content = ImGui::GetContentRegionAvail().x;
                const float height = ImGui::GetContentRegionAvail().y;
                const float line = ImGui::GetTextLineHeight();
                const float iconSize = std::min(frame * 3.15f, content);
                const float buttonGap = frame * .34f;
                const float textGap = frame * .18f;
                const float free = height - iconSize - 2.f * line - textGap - 2.f * buttonHeight - buttonGap;
                const float gap = std::max(0.f, free / 3.f);
                const auto place = [&](float x, float y) {
                    ImGui::SetCursorScreenPos({cardOrigin.x + x, cardOrigin.y + y});
                };
                const auto centerText = [&](const char* text, float y, const ImVec4& color) {
                    const float textWidth = ImGui::CalcTextSize(text).x;
                    place(std::max(0.f, (content - textWidth) * .5f), y);
                    ImGui::PushClipRect({cardOrigin.x, cardOrigin.y + y}, {cardOrigin.x + content, cardOrigin.y + y + line}, true);
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(text);
                    ImGui::PopStyleColor();
                    ImGui::PopClipRect();
                    return textWidth > content;
                };
                place(0.f, gap * .5f);
                ComputerIcon(content, iconSize, colors);
                const float nameY = iconSize + gap * 1.5f;
                if (centerText(peer.name.c_str(), nameY, colors.textPrimary) && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", peer.name.c_str());
                char latency[64];
                const char* label = "Задержка: —";
                if (peer.latencyMs && *peer.latencyMs == 0) label = "Задержка: <1 мс";
                else if (peer.latencyMs)
                {
                    sprintf_s(latency, sizeof(latency), "Задержка: %u мс", static_cast<unsigned>(*peer.latencyMs));
                    label = latency;
                }
                centerText(label, nameY + line + textGap, colors.textMuted);
                const float buttonY = nameY + 2.f * line + textGap + gap;
                place(0.f, buttonY);
                const bool cancelTest = testingSpeed && sessionStatus.peer == peer.id;
                const bool speedEnabled = cancelTest || (state.active && state.visible && state.session.CanStart(state.discovery, peer.id));
                ImGui::BeginDisabled(!speedEnabled);
                if (ImGui::Button(cancelTest ? "Отменить тест###speed" : "Проверить скорость###speed", {content, buttonHeight}))
                {
                    if (cancelTest) state.session.Cancel();
                    else state.session.Start(state.discovery, peer.id);
                }
                ImGui::EndDisabled();
                if (!speedEnabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("Откройте вкладку LAN и включите видимость на обоих ПК. Нужен свободный сеанс и один однозначный маршрут.");
                place(0.f, buttonY + buttonHeight + buttonGap);
                ImGui::BeginDisabled(busy || !state.active);
                if (ImGui::Button("Передать файл(ы)", {content, buttonHeight}))
                {
                    state.target = peer.name;
                    state.targetId = peer.id;
                    page.selected = {};
                    page.status.clear();
                    openPicker = true;
                }
                ImGui::EndDisabled();
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    const auto accents = theme.GetAccents();
    if (openPicker) ImGui::OpenPopup("Подготовка файлов##lan");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({std::min(frame * 39.f, ImGui::GetIO().DisplaySize.x * .94f),
        std::min(frame * 24.f, ImGui::GetIO().DisplaySize.y * .90f)}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {frame * .85f, frame * .75f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, frame * .70f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, frame * .45f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {frame * .45f, frame * .45f});
    ImGui::PushStyleColor(ImGuiCol_PopupBg, colors.tileBg);
    ImGui::PushStyleColor(ImGuiCol_Border, colors.tileBorder);
    if (!ImGui::BeginPopupModal("Подготовка файлов##lan", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);
        return;
    }
    const float closeWidth = ImGui::CalcTextSize("Закрыть").x + frame * 1.2f;
    const float headerRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Подготовка файлов");
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), headerRight - closeWidth));
    ImGui::BeginDisabled(busy);
    if (UiCommon::AccentButton("Закрыть", {closeWidth, buttonHeight}, accents.fail, colors)) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::Text("Компьютер: %s", state.target.c_str());
    ImGui::Dummy({0.f, frame * .15f});

    const auto fitNext = [](const char* label) {
        const auto& s = ImGui::GetStyle();
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + s.ItemSpacing.x + ImGui::CalcTextSize(label).x + 2 * s.FramePadding.x <= right)
            ImGui::SameLine();
    };
    ImGui::BeginDisabled(busy);
    if (UiCommon::SecondaryButton("Выбрать файлы…", {0, buttonHeight}, colors)) page.Start(Source::Files);
    fitNext("Выбрать папки…");
    if (UiCommon::SecondaryButton("Выбрать папки…", {0, buttonHeight}, colors)) page.Start(Source::Folders);
    fitNext("Вставить (Ctrl+V)");
    if (UiCommon::SecondaryButton("Вставить (Ctrl+V)", {0, buttonHeight}, colors) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V))
        page.Start(Source::Clipboard);
    fitNext("Очистить список");
    ImGui::BeginDisabled(page.selected.entries.empty());
    if (UiCommon::SecondaryButton("Очистить список", {0, buttonHeight}, colors)) { page.selected = {}; page.status.clear(); }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (busy && UiCommon::SecondaryButton("Отменить подготовку", {0, buttonHeight}, colors))
    {
        page.cancel->store(true, std::memory_order_relaxed);
        page.status = "Запрошена отмена. Если открыто системное окно, закройте его.";
    }
    if (!page.status.empty()) ImGui::TextWrapped("%s", page.status.c_str());
    ImGui::Dummy({0.f, frame * .15f});
    auto& entries = page.selected.entries;
    std::vector<size_t> checkedPrefix(entries.size() + 1);
    uint64_t checkedBytes = 0;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        checkedPrefix[i + 1] = checkedPrefix[i] + entries[i].checked;
        if (entries[i].checked) checkedBytes += entries[i].size;
    }
    ImGui::Text("Отмечено: %zu из %zu · %s", checkedPrefix.back(), entries.size(), FileSizeLabel(checkedBytes).c_str());
    ImGui::BeginDisabled(busy || !checkedPrefix.back());
    if (UiCommon::AccentButton("Отправить файл(ы)", {0, buttonHeight}, accents.ok, colors))
    {
        try
        {
            const auto checked = lan::CheckedSelection(entries);
            if (checked.size() > lan::transfer::MaxEntries)
                page.status = "Выбрано " + std::to_string(checked.size()) + " объектов. За один раз можно передать до " +
                    std::to_string(lan::transfer::MaxEntries) + " файлов и папок суммарно. Снимите часть отметок.";
            else
            {
                std::vector<lan::transfer::Source> sources;
                sources.reserve(checked.size());
                for (auto i : checked)
                {
                    const auto& entry = entries[i];
                    sources.push_back({{Utf8(fs::path(entry.relative).generic_wstring()), entry.directory, entry.size},
                        entry.directory ? std::wstring{} : entry.source.native()});
                }
                if (!sources.empty() && state.session.Start(state.discovery, state.targetId, std::move(sources)))
                    ImGui::CloseCurrentPopup();
                else
                    page.status = "Не удалось начать передачу. Проверьте видимость обоих ПК и завершите другой LAN-сеанс, если он открыт.";
            }
        }
        catch (const std::exception&) { page.status = "Не удалось подготовить выбранные файлы к передаче."; }
    }
    ImGui::EndDisabled();
    if (ImGui::BeginChild("##lan_selection", {0.f, 0.f}, ImGuiChildFlags_Borders,
            ImGuiWindowFlags_HorizontalScrollbar))
    {
        if (entries.empty()) ImGui::TextDisabled("Список пока пуст");
        std::vector<size_t> rows;
        for (size_t i = 0; i < entries.size();)
        {
            rows.push_back(i);
            i = entries[i].directory && !entries[i].expanded ? entries[i].end : i + 1;
        }
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(rows.size()));
        while (clip.Step())
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
            {
                const size_t i = rows[row];
                auto& item = entries[i];
                const float indent = static_cast<float>(item.depth) * frame * .78f;
                ImGui::PushID(static_cast<int>(i));
                ImGui::Indent(indent + 1.f);
                if (item.end > i + 1)
                {
                    if (ImGui::ArrowButton("##expand", item.expanded ? ImGuiDir_Down : ImGuiDir_Right))
                        item.expanded = !item.expanded;
                }
                else ImGui::Dummy({frame, frame});
                ImGui::SameLine();
                const size_t marked = checkedPrefix[item.end] - checkedPrefix[i];
                bool all = marked == item.end - i;
                const bool mixed = marked != 0 && !all;
                ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed);
                if (ImGui::Checkbox("##checked", &all))
                    for (size_t j = i; j < item.end; ++j) entries[j].checked = all;
                ImGui::PopItemFlag();
                ImGui::SameLine();
                const auto p = ImGui::GetCursorScreenPos();
                const float s = frame * .68f;
                const float y = p.y + (frame - s) * .5f;
                auto* draw = ImGui::GetWindowDrawList();
                const auto fg = ImGui::GetColorU32(item.directory ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled);
                if (item.directory)
                {
                    draw->AddRectFilled({p.x, y + s * .09f}, {p.x + s * .46f, y + s * .42f}, fg, 2.f);
                    draw->AddRectFilled({p.x, y + s * .30f}, {p.x + s, y + s * .89f}, fg, 2.f);
                }
                else draw->AddRect({p.x + s * .16f, y}, {p.x + s * .84f, y + s}, fg, 2.f);
                ImGui::Dummy({s, frame});
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                const auto name = Utf8(item.source.filename().native());
                if (item.directory) ImGui::TextUnformatted(name.c_str());
                else ImGui::Text("%s · %s", name.c_str(), FileSizeLabel(item.size).c_str());
                ImGui::Unindent(indent + 1.f);
                ImGui::PopID();
            }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}
#endif
