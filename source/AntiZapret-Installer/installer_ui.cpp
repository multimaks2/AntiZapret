#include "AntiZapret-Installer/installer_ui.h"
#include "AntiZapret-Installer/resource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <windowsx.h>
#include <ShObjIdl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	constexpr int kWindowW = 560;
	constexpr int kWindowH = 560;
	constexpr float kTitleBarH = 28.f;
	constexpr float kBtnSize = 14.f;
	constexpr float kBtnGap = 8.f;
	constexpr float kUiRounding = 8.f;
	constexpr float kNavBtnH = 32.f;
	constexpr float kPagePad = 20.f;
	constexpr float kCardPad = 18.f;
	constexpr char kRepoUrl[] = "https://github.com/multimaks2/AntiZapret";
	constexpr char kReleasesLatestUrl[] = "https://github.com/multimaks2/AntiZapret/releases/latest";

	enum class WizardPage
	{
		Welcome = 0,
		Path,
		Install,
		Done,
	};

	struct UiFrame
	{
		HWND hwnd = nullptr;
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swapChain = nullptr;
		ID3D11RenderTargetView* rtv = nullptr;
		bool running = true;
		bool dragging = false;
		POINT dragCursor = {};
		POINT dragOrigin = {};
		float dpiScale = 1.f;
		InstallerUiState* state = nullptr;
		WizardPage page = WizardPage::Welcome;
		char pathBuf[MAX_PATH * 2] = {};
		std::thread installThread;
		bool installThreadJoined = true;
		std::uint64_t cachedRevision = UINT64_MAX;
		std::string cachedStatus;
		std::string cachedError;
		std::vector<std::string> cachedLogs;
		float cachedProgress = 0.f;
		bool cachedFinished = false;
		bool cachedFailed = false;
		bool stickLogToBottom = true;
		size_t lastLogCount = 0;
	};

	UiFrame* g_frame = nullptr;

	void CreateRenderTarget(UiFrame& frame)
	{
		ID3D11Texture2D* backBuffer = nullptr;
		frame.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (backBuffer)
		{
			frame.device->CreateRenderTargetView(backBuffer, nullptr, &frame.rtv);
			backBuffer->Release();
		}
	}

	void CleanupRenderTarget(UiFrame& frame)
	{
		if (frame.rtv)
		{
			frame.rtv->Release();
			frame.rtv = nullptr;
		}
	}

	bool CreateDevice(HWND hwnd, UiFrame& frame)
	{
		DXGI_SWAP_CHAIN_DESC sd = {};
		sd.BufferCount = 2;
		sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.BufferDesc.RefreshRate.Numerator = 60;
		sd.BufferDesc.RefreshRate.Denominator = 1;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.OutputWindow = hwnd;
		sd.SampleDesc.Count = 1;
		sd.Windowed = TRUE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		D3D_FEATURE_LEVEL level;
		const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
		if (FAILED(D3D11CreateDeviceAndSwapChain(
				nullptr,
				D3D_DRIVER_TYPE_HARDWARE,
				nullptr,
				0,
				levels,
				2,
				D3D11_SDK_VERSION,
				&sd,
				&frame.swapChain,
				&frame.device,
				&level,
				&frame.context)))
			return false;

		CreateRenderTarget(frame);
		return frame.rtv != nullptr;
	}

	void CleanupDevice(UiFrame& frame)
	{
		CleanupRenderTarget(frame);
		if (frame.swapChain)
		{
			frame.swapChain->Release();
			frame.swapChain = nullptr;
		}
		if (frame.context)
		{
			frame.context->Release();
			frame.context = nullptr;
		}
		if (frame.device)
		{
			frame.device->Release();
			frame.device = nullptr;
		}
	}

	void LoadCyrillicFont(float dpiScale)
	{
		ImGuiIO& io = ImGui::GetIO();
		const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();
		wchar_t winDir[MAX_PATH] = {};
		if (!GetWindowsDirectoryW(winDir, MAX_PATH))
		{
			io.Fonts->AddFontDefault();
			return;
		}

		const wchar_t* names[] = { L"\\Fonts\\segoeui.ttf", L"\\Fonts\\tahoma.ttf", L"\\Fonts\\arial.ttf" };
		char pathUtf8[MAX_PATH * 2] = {};
		bool loaded = false;
		for (const wchar_t* name : names)
		{
			wchar_t path[MAX_PATH] = {};
			if (wcscpy_s(path, winDir) || wcscat_s(path, name))
				continue;
			if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
				continue;
			if (WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr) <= 0)
				continue;
			if (io.Fonts->AddFontFromFileTTF(pathUtf8, 14.f * dpiScale, nullptr, ranges))
			{
				loaded = true;
				break;
			}
		}
		if (!loaded)
			io.Fonts->AddFontDefault();
	}

	void ApplyAntiZapretStyle(float dpiScale)
	{
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowRounding = 0.f;
		style.ChildRounding = kUiRounding;
		style.FrameRounding = kUiRounding;
		style.PopupRounding = kUiRounding;
		style.ScrollbarRounding = kUiRounding;
		style.GrabRounding = kUiRounding;
		style.TabRounding = kUiRounding;
		style.WindowBorderSize = 0.f;
		style.ChildBorderSize = 1.f;
		style.FrameBorderSize = 0.f;
		style.WindowPadding = { 0.f, 0.f };
		style.FramePadding = { 12.f, 8.f };
		style.ItemSpacing = { 10.f, 10.f };
		style.ScrollbarSize = 10.f;
		style.ScaleAllSizes(dpiScale);

		ImVec4* c = style.Colors;
		const ImVec4 bg = { 0.06275f, 0.06275f, 0.06275f, 1.f };
		const ImVec4 tile = { 0.10f, 0.10f, 0.10f, 1.f };
		const ImVec4 border = { 0.18f, 0.18f, 0.18f, 1.f };
		const ImVec4 text = { 0.92f, 0.92f, 0.92f, 1.f };
		const ImVec4 muted = { 0.55f, 0.55f, 0.55f, 1.f };
		const ImVec4 ok = { 33.f / 255.f, 176.f / 255.f, 77.f / 255.f, 1.f };
		const ImVec4 link = { 0.45f, 0.70f, 1.f, 1.f };

		c[ImGuiCol_WindowBg] = bg;
		c[ImGuiCol_ChildBg] = bg; // layout children invisible; cards set their own bg
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = muted;
		c[ImGuiCol_TextLink] = link;
		c[ImGuiCol_FrameBg] = { 0.078f, 0.078f, 0.078f, 1.f };
		c[ImGuiCol_FrameBgHovered] = { 0.12f, 0.12f, 0.12f, 1.f };
		c[ImGuiCol_FrameBgActive] = { 0.14f, 0.14f, 0.14f, 1.f };
		c[ImGuiCol_Button] = { 0.16f, 0.16f, 0.16f, 1.f };
		c[ImGuiCol_ButtonHovered] = { 0.22f, 0.22f, 0.22f, 1.f };
		c[ImGuiCol_ButtonActive] = { 0.26f, 0.26f, 0.26f, 1.f };
		c[ImGuiCol_CheckMark] = ok;
		c[ImGuiCol_Header] = { 0.16f, 0.16f, 0.16f, 1.f };
		c[ImGuiCol_PlotHistogram] = ok;
		c[ImGuiCol_PlotHistogramHovered] = ok;
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_ScrollbarBg] = bg;
		c[ImGuiCol_ScrollbarGrab] = border;
		c[ImGuiCol_ScrollbarGrabHovered] = { 0.28f, 0.28f, 0.28f, 1.f };
		c[ImGuiCol_ScrollbarGrabActive] = { 0.34f, 0.34f, 0.34f, 1.f };
	}

	void DrawGlow(ImDrawList* drawList, ImVec2 center, ImU32 color, float radius)
	{
		const ImU32 rgb = color & 0x00FFFFFF;
		for (int i = 0; i < 5; ++i)
		{
			const float scale = 2.5f - i * 0.5f;
			const float alpha = 0.02f + i * 0.095f;
			drawList->AddCircleFilled(center, radius * scale, rgb | (ImU32(alpha * 255) << 24), 32);
		}
	}

	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};
		std::string out(static_cast<size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
		return out;
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
			return {};
		const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (size <= 1)
			return {};
		std::wstring out(static_cast<size_t>(size - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
		return out;
	}

	bool BrowseForFolder(HWND owner, std::string& pathUtf8)
	{
		IFileDialog* dialog = nullptr;
		if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
			return false;

		DWORD options = 0;
		dialog->GetOptions(&options);
		dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
		dialog->SetTitle(L"Папка установки AntiZapret");

		const std::wstring current = Utf8ToWide(pathUtf8);
		if (!current.empty())
		{
			IShellItem* folder = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(current.c_str(), nullptr, IID_PPV_ARGS(&folder))))
			{
				dialog->SetFolder(folder);
				folder->Release();
			}
		}

		const bool ok = SUCCEEDED(dialog->Show(owner));
		if (ok)
		{
			IShellItem* result = nullptr;
			if (SUCCEEDED(dialog->GetResult(&result)))
			{
				PWSTR filePath = nullptr;
				if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &filePath)) && filePath)
				{
					pathUtf8 = WideToUtf8(filePath);
					CoTaskMemFree(filePath);
				}
				result->Release();
			}
		}
		dialog->Release();
		return ok;
	}

	void RequestClose(InstallerUiState& state, bool completed, bool launch)
	{
		state.closeRequested = true;
		state.installRequested = completed;
		state.launchRequested = completed && launch;
	}

	float PageInnerWidth()
	{
		return ImGui::GetContentRegionAvail().x;
	}

	void PushBodyWrap()
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + PageInnerWidth());
	}

	bool BottomRightCheckbox(const char* label, bool* value, float dpiScale)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 3.f * dpiScale, 2.f * dpiScale });
		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, { 6.f * dpiScale, 4.f * dpiScale });
		ImGui::SetWindowFontScale(0.92f);

		const float box = ImGui::GetFrameHeight();
		const ImVec2 labelSize = ImGui::CalcTextSize(label);
		const float totalW = box + ImGui::GetStyle().ItemInnerSpacing.x + labelSize.x;
		const float totalH = (std::max)(box, labelSize.y) + 2.f * dpiScale;
		const float availY = ImGui::GetContentRegionAvail().y;
		if (availY > totalH + 1.f)
			ImGui::Dummy({ 0.f, availY - totalH - 1.f });

		ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - totalW);
		const bool changed = ImGui::Checkbox(label, value);

		ImGui::SetWindowFontScale(1.f);
		ImGui::PopStyleVar(2);
		return changed;
	}

	const char* ReleaseLinkUrl(const InstallerUiState& state)
	{
		if (!state.releaseZipUrl.empty())
			return state.releaseZipUrl.c_str();
		return kReleasesLatestUrl;
	}

	void DrawInlineText(const char* text)
	{
		ImGui::TextUnformatted(text);
		ImGui::SameLine(0.f, 0.f);
	}

	void DrawTitleBar(HWND hwnd, InstallerUiState& state, float width, float dpiScale)
	{
		const float barH = kTitleBarH * dpiScale;
		const float btn = kBtnSize * dpiScale;
		const float gap = kBtnGap * dpiScale;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06275f, 0.06275f, 0.06275f, 1.f));
		ImGui::BeginChild("##TitleBar", { width, barH }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 barMin = ImGui::GetWindowPos();
		const ImU32 mutedCol = IM_COL32(140, 140, 140, 255);
		const ImU32 okCol = IM_COL32(33, 176, 77, 255);
		constexpr ImU32 kMinGlow = IM_COL32(38, 191, 84, 255);
		constexpr ImU32 kCloseGlow = IM_COL32(255, 77, 54, 255);

		const float titleY = barMin.y + (barH - ImGui::GetFontSize()) * 0.5f;
		dl->AddText({ barMin.x + 12.f * dpiScale, titleY }, okCol, "AntiZapret");
		const ImVec2 brandSize = ImGui::CalcTextSize("AntiZapret");
		dl->AddText({ barMin.x + 12.f * dpiScale + brandSize.x + 8.f * dpiScale, titleY }, mutedCol, "Installer");

		const float closeX = width - btn - 12.f * dpiScale;
		const float minX = closeX - gap - btn;
		const float btnY = (barH - btn) * 0.5f;

		static bool hoverMin = false;
		static bool hoverClose = false;

		if (hoverMin)
			DrawGlow(dl, { barMin.x + minX + btn * 0.5f, barMin.y + btnY + btn * 0.5f }, kMinGlow, btn * 0.5f);
		if (hoverClose)
			DrawGlow(dl, { barMin.x + closeX + btn * 0.5f, barMin.y + btnY + btn * 0.5f }, kCloseGlow, btn * 0.5f);

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn * 0.5f);

		ImGui::SetCursorPos({ minX, btnY });
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.69f, 0.30f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.75f, 0.33f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.11f, 0.60f, 0.26f, 1.f));
		if (ImGui::Button("##Minimize", { btn, btn }))
			ShowWindow(hwnd, SW_MINIMIZE);
		hoverMin = ImGui::IsItemHovered();
		ImGui::PopStyleColor(3);

		ImGui::SetCursorPos({ closeX, btnY });
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.26f, 0.21f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 0.30f, 0.25f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.22f, 0.18f, 1.f));
		if (ImGui::Button("##Close", { btn, btn }))
			RequestClose(state, false, false);
		hoverClose = ImGui::IsItemHovered();
		ImGui::PopStyleColor(3);

		ImGui::PopStyleVar();

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (g_frame)
			{
				g_frame->dragging = true;
				GetCursorPos(&g_frame->dragCursor);
				RECT wr = {};
				GetWindowRect(hwnd, &wr);
				g_frame->dragOrigin = { wr.left, wr.top };
				SetCapture(hwnd);
			}
		}

		ImGui::EndChild();
		ImGui::PopStyleColor();
	}

	bool NavButton(const char* label, float width, float dpiScale, bool enabled = true)
	{
		if (!enabled)
			ImGui::BeginDisabled();
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f * dpiScale);
		const bool pressed = ImGui::Button(label, { width, kNavBtnH * dpiScale });
		ImGui::PopStyleVar();
		if (!enabled)
			ImGui::EndDisabled();
		return pressed;
	}

	void DrawFooterNav(
		InstallerUiState& state,
		WizardPage page,
		float dpiScale,
		float footerY,
		bool canNext,
		const char* nextLabel,
		bool* outBack,
		bool* outNext,
		bool* outCancel)
	{
		*outBack = false;
		*outNext = false;
		*outCancel = false;
		(void)state;
		(void)outCancel;

		const float btnW = 110.f * dpiScale;
		const float pad = kPagePad * dpiScale;
		const float winW = ImGui::GetWindowSize().x;
		const bool showBack = (page == WizardPage::Path);

		if (showBack)
		{
			ImGui::SetCursorPos({ pad, footerY });
			if (NavButton("Назад", btnW, dpiScale))
				*outBack = true;
		}

		ImGui::SetCursorPos({ winW - pad - btnW, footerY });
		if (NavButton(nextLabel, btnW, dpiScale, canNext))
			*outNext = true;
	}

	void DrawWelcomePage(InstallerUiState& state, float dpiScale)
	{
		const ImVec4 muted = { 0.62f, 0.62f, 0.62f, 1.f };

		ImGui::TextUnformatted("Вас приветствует мастер установки AntiZapret");
		ImGui::Dummy({ 0.f, 10.f * dpiScale });

		PushBodyWrap();
		ImGui::TextWrapped(
			"AntiZapret собрал в себе всё лучшее для обхода блокировок: актуальные стратегии Zapret, "
			"удобный интерфейс, быстрое переключение режимов и автоматические обновления.");
		ImGui::Dummy({ 0.f, 8.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		ImGui::TextWrapped(
			"Отказ от ответственности\n"
			"Автор не несёт ответственности за возможные последствия использования программы. "
			"Вы устанавливаете и используете AntiZapret по собственному решению и на свой страх и риск.");
		ImGui::Dummy({ 0.f, 6.f * dpiScale });
		ImGui::TextWrapped(
			"Обновления\n"
			"После установки приложение может само проверять и загружать обновления с GitHub. "
			"Автообновление не гарантирует стопроцентную надёжность: доступ к GitHub иногда бывает ограничен.");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 10.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.38f, 0.34f, 1.f));
		ImGui::TextWrapped(
			"Внимание\n"
			"Содержимое выбранной папки установки может быть полностью удалено перед копированием файлов "
			"(если там обнаружена предыдущая установка AntiZapret). Это нужно, чтобы исключить конфликты "
			"драйверов WinDivert и остатков старых версий. Не указывайте папку с важными личными файлами.");
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();

		BottomRightCheckbox("Я принимаю условия", &state.acceptedTerms, dpiScale);
	}

	void DrawPathPage(UiFrame& frame, InstallerUiState& state, float dpiScale)
	{
		const ImVec4 muted = { 0.62f, 0.62f, 0.62f, 1.f };
		const float contentW = PageInnerWidth();

		ImGui::TextUnformatted("Папка установки");
		ImGui::Dummy({ 0.f, 6.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		PushBodyWrap();
		ImGui::TextWrapped("Укажите каталог, в который будет установлен AntiZapret.");
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 8.f * dpiScale });

		const float browseW = 96.f * dpiScale;
		const float inputH = ImGui::GetFrameHeight();
		const float pathRowH = inputH + 16.f * dpiScale;
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kUiRounding * dpiScale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 10.f * dpiScale, 8.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.078f, 0.078f, 0.078f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
		if (ImGui::BeginChild("##pathBlock", { contentW, pathRowH }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
		{
			const float pathW = ImGui::GetContentRegionAvail().x - browseW - 8.f * dpiScale;
			ImGui::SetNextItemWidth(pathW);
			ImGui::InputText("##installPath", frame.pathBuf, sizeof(frame.pathBuf));
			ImGui::SameLine();
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f * dpiScale);
			if (ImGui::Button("Обзор...", { browseW, inputH }))
			{
				std::string path = frame.pathBuf;
				if (BrowseForFolder(frame.hwnd, path) && !path.empty())
				{
					strncpy_s(frame.pathBuf, path.c_str(), _TRUNCATE);
					state.installPath = path;
				}
			}
			ImGui::PopStyleVar();
			state.installPath = frame.pathBuf;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);

		ImGui::Dummy({ 0.f, 10.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		DrawInlineText("Установка будет выполнена из репозитория ");
		ImGui::PopStyleColor();
		ImGui::TextLinkOpenURL("multimaks2/AntiZapret", kRepoUrl);

		ImGui::Dummy({ 0.f, 4.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		DrawInlineText("Будет установлен архив ");
		ImGui::PopStyleColor();
		ImGui::TextLinkOpenURL(state.releaseZipName.c_str(), ReleaseLinkUrl(state));
		if (!state.releaseVersion.empty())
		{
			ImGui::SameLine(0.f, 0.f);
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			ImGui::TextUnformatted(" (релиз ");
			ImGui::PopStyleColor();
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextLinkOpenURL(state.releaseVersion.c_str(), ReleaseLinkUrl(state));
			ImGui::SameLine(0.f, 0.f);
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			ImGui::TextUnformatted(").");
			ImGui::PopStyleColor();
		}

		const InstallPathCheck pathCheck = CheckInstallPath(frame.pathBuf);
		ImGui::Dummy({ 0.f, 10.f * dpiScale });
		if (!pathCheck.ok)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.38f, 0.34f, 1.f));
			PushBodyWrap();
			ImGui::TextWrapped("%s", pathCheck.message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		else if (pathCheck.willCleanExisting)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.72f, 0.28f, 1.f));
			PushBodyWrap();
			ImGui::TextWrapped("%s", pathCheck.message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		BottomRightCheckbox("Создать ярлык на рабочем столе", &state.createDesktopShortcut, dpiScale);
	}

	void SyncInstallCache(UiFrame& frame, InstallerUiState& state)
	{
		const std::uint64_t rev = state.revision.load(std::memory_order_relaxed);
		if (rev == frame.cachedRevision)
			return;
		std::lock_guard<std::mutex> lock(state.mutex);
		frame.cachedStatus = state.status;
		frame.cachedError = state.error;
		frame.cachedLogs = state.logs;
		frame.cachedProgress = state.progress;
		frame.cachedFinished = state.installFinished;
		frame.cachedFailed = state.installFailed;
		frame.cachedRevision = state.revision.load(std::memory_order_relaxed);
	}

	void EnsureInstallStarted(UiFrame& frame, InstallerUiState& state)
	{
		if (state.installStarted)
			return;
		state.installStarted = true;
		frame.installThreadJoined = false;
		frame.stickLogToBottom = true;
		frame.lastLogCount = 0;
		frame.cachedRevision = UINT64_MAX;
		frame.installThread = std::thread([&state] { RunInstallWorker(state); });
	}

	void JoinInstallThread(UiFrame& frame)
	{
		if (!frame.installThreadJoined && frame.installThread.joinable())
		{
			frame.installThread.join();
			frame.installThreadJoined = true;
		}
	}

	void DrawInstallPage(UiFrame& frame, InstallerUiState& state, float dpiScale)
	{
		const ImVec4 muted = { 0.62f, 0.62f, 0.62f, 1.f };
		const ImVec4 failCol = { 0.90f, 0.32f, 0.32f, 1.f };
		const float contentW = PageInnerWidth();

		EnsureInstallStarted(frame, state);
		SyncInstallCache(frame, state);

		ImGui::TextUnformatted("Установка");
		ImGui::Dummy({ 0.f, 6.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		ImGui::TextWrapped("%s", frame.cachedStatus.empty() ? " " : frame.cachedStatus.c_str());
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 6.f * dpiScale });

		char overlay[32] = {};
		snprintf(overlay, sizeof(overlay), "%.0f%%", frame.cachedProgress * 100.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f * dpiScale);
		ImGui::ProgressBar(
			frame.cachedProgress <= 0.f ? 0.001f : frame.cachedProgress,
			ImVec2(contentW, 20.f * dpiScale),
			overlay);
		ImGui::PopStyleVar();

		if (frame.cachedFailed && !frame.cachedError.empty())
		{
			ImGui::Dummy({ 0.f, 4.f * dpiScale });
			ImGui::PushStyleColor(ImGuiCol_Text, failCol);
			ImGui::TextWrapped("Ошибка: %s", frame.cachedError.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::Dummy({ 0.f, 8.f * dpiScale });
		ImGui::TextUnformatted("Журнал");
		if (frame.cachedLogs.size() != frame.lastLogCount)
		{
			if (frame.cachedLogs.size() > frame.lastLogCount)
				frame.stickLogToBottom = true;
			frame.lastLogCount = frame.cachedLogs.size();
		}

		const float logH = (std::max)(1.f, ImGui::GetContentRegionAvail().y);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kUiRounding * dpiScale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.f * dpiScale, 8.f * dpiScale });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(26.f / 255.f, 26.f / 255.f, 26.f / 255.f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(46.f / 255.f, 46.f / 255.f, 46.f / 255.f, 1.f));
		if (ImGui::BeginChild("##installLog", { contentW, logH }, ImGuiChildFlags_Borders))
		{
			for (const std::string& line : frame.cachedLogs)
				ImGui::TextUnformatted(line.c_str());
			if (frame.stickLogToBottom)
				ImGui::SetScrollHereY(1.f);
			if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 8.f)
				frame.stickLogToBottom = false;
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}

	void DrawDonePage(InstallerUiState& state, float dpiScale)
	{
		const ImVec4 ok = { 33.f / 255.f, 176.f / 255.f, 77.f / 255.f, 1.f };
		const ImVec4 failCol = { 0.90f, 0.32f, 0.32f, 1.f };
		const ImVec4 muted = { 0.62f, 0.62f, 0.62f, 1.f };

		if (state.installFailed)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, failCol);
			ImGui::TextUnformatted("Установка не завершена");
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 10.f * dpiScale });
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			PushBodyWrap();
			ImGui::TextWrapped("%s", state.error.empty() ? "Произошла ошибка во время установки." : state.error.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ok);
			ImGui::TextUnformatted("Установка завершена");
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 10.f * dpiScale });
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			PushBodyWrap();
			ImGui::TextWrapped(
				"AntiZapret установлен в:\n%s\n\nАрхив: %s",
				state.installPath.c_str(),
				state.releaseZipName.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::Dummy({ 0.f, 6.f * dpiScale });
			ImGui::PushStyleColor(ImGuiCol_Text, muted);
			DrawInlineText("Репозиторий: ");
			ImGui::PopStyleColor();
			ImGui::TextLinkOpenURL("multimaks2/AntiZapret", kRepoUrl);
			BottomRightCheckbox("Запустить AntiZapret", &state.launchAfterInstall, dpiScale);
		}
	}

	void DrawUi(HWND hwnd, InstallerUiState& state, float dpiScale)
	{
		if (!g_frame)
			return;
		UiFrame& frame = *g_frame;

		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::Begin(
			"##installer",
			nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		const float width = ImGui::GetContentRegionAvail().x;
		const float winH = ImGui::GetWindowSize().y;
		DrawTitleBar(hwnd, state, width, dpiScale);

		const float outerPad = kPagePad * dpiScale;
		const float cardPad = kCardPad * dpiScale;
		const float titleH = kTitleBarH * dpiScale;
		const float topGap = 8.f * dpiScale;
		// Fixed footer band so nav buttons always stay visible below the card.
		const float footerBand = (kNavBtnH + 20.f) * dpiScale;
		const float bodyY = titleH + topGap;
		const float bodyH = (std::max)(80.f * dpiScale, winH - bodyY - footerBand);
		const float bodyW = width - outerPad * 2.f;

		ImGui::SetCursorPos({ outerPad, bodyY });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.10f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.18f, 0.18f, 1.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kUiRounding * dpiScale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { cardPad, cardPad });
		ImGui::BeginChild(
			"##body",
			{ bodyW, bodyH },
			ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		switch (frame.page)
		{
		case WizardPage::Welcome:
			DrawWelcomePage(state, dpiScale);
			break;
		case WizardPage::Path:
			DrawPathPage(frame, state, dpiScale);
			break;
		case WizardPage::Install:
			DrawInstallPage(frame, state, dpiScale);
			break;
		case WizardPage::Done:
			DrawDonePage(state, dpiScale);
			break;
		}

		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(2);

		bool back = false;
		bool next = false;
		bool cancel = false;
		const char* nextLabel = "Далее";
		bool canNext = true;
		switch (frame.page)
		{
		case WizardPage::Welcome:
			canNext = state.acceptedTerms;
			break;
		case WizardPage::Path:
			canNext = CheckInstallPath(frame.pathBuf).ok;
			nextLabel = "Установить";
			break;
		case WizardPage::Install:
			SyncInstallCache(frame, state);
			canNext = frame.cachedFinished;
			nextLabel = "Далее";
			break;
		case WizardPage::Done:
			nextLabel = "Готово";
			break;
		}

		const float footerY = winH - footerBand + 6.f * dpiScale;
		DrawFooterNav(state, frame.page, dpiScale, footerY, canNext, nextLabel, &back, &next, &cancel);
		(void)cancel;

		if (back && frame.page == WizardPage::Path)
			frame.page = WizardPage::Welcome;

		if (next)
		{
			if (frame.page == WizardPage::Welcome)
				frame.page = WizardPage::Path;
			else if (frame.page == WizardPage::Path)
			{
				state.installPath = frame.pathBuf;
				if (CheckInstallPath(state.installPath).ok)
					frame.page = WizardPage::Install;
			}
			else if (frame.page == WizardPage::Install)
				frame.page = WizardPage::Done;
			else if (frame.page == WizardPage::Done)
			{
				const bool ok = !state.installFailed;
				RequestClose(state, ok, ok && state.launchAfterInstall);
			}
		}

		ImGui::End();
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
			return true;

		switch (msg)
		{
		case WM_MOUSEMOVE:
			if (g_frame && g_frame->dragging)
			{
				POINT cur = {};
				GetCursorPos(&cur);
				SetWindowPos(
					hwnd,
					nullptr,
					g_frame->dragOrigin.x + (cur.x - g_frame->dragCursor.x),
					g_frame->dragOrigin.y + (cur.y - g_frame->dragCursor.y),
					0,
					0,
					SWP_NOSIZE | SWP_NOZORDER);
				return 0;
			}
			break;
		case WM_LBUTTONUP:
			if (g_frame && g_frame->dragging)
			{
				g_frame->dragging = false;
				ReleaseCapture();
				return 0;
			}
			break;
		case WM_SIZE:
			if (g_frame && g_frame->device && wParam != SIZE_MINIMIZED)
			{
				CleanupRenderTarget(*g_frame);
				g_frame->swapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
				CreateRenderTarget(*g_frame);
			}
			return 0;
		case WM_DESTROY:
			if (g_frame)
				g_frame->running = false;
			PostQuitMessage(0);
			return 0;
		case WM_CLOSE:
			if (g_frame && g_frame->state)
				RequestClose(*g_frame->state, false, false);
			DestroyWindow(hwnd);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

bool RunInstallerUi(InstallerUiState& state)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	ResolveLatestRelease(state);

	ImGui_ImplWin32_EnableDpiAwareness();
	const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.style = CS_CLASSDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"AntiZapretInstaller";
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
	wc.hIconSm = (HICON)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		CoUninitialize();
		return false;
	}

	const int w = int(kWindowW * dpiScale);
	const int h = int(kWindowH * dpiScale);
	const int sw = GetSystemMetrics(SM_CXSCREEN);
	const int sh = GetSystemMetrics(SM_CYSCREEN);

	HWND hwnd = CreateWindowExW(
		WS_EX_APPWINDOW,
		wc.lpszClassName,
		L"AntiZapret Installer",
		WS_POPUP,
		(sw - w) / 2,
		(sh - h) / 2,
		w,
		h,
		nullptr,
		nullptr,
		wc.hInstance,
		nullptr);
	if (!hwnd)
	{
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		CoUninitialize();
		return false;
	}

	SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
	SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

	UiFrame frame;
	frame.hwnd = hwnd;
	frame.state = &state;
	frame.dpiScale = dpiScale;
	strncpy_s(frame.pathBuf, state.installPath.c_str(), _TRUNCATE);
	g_frame = &frame;

	if (!CreateDevice(hwnd, frame))
	{
		MessageBoxW(hwnd, L"Не удалось инициализировать окно установки (D3D11).", L"AntiZapret-Installer", MB_OK | MB_ICONWARNING);
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		g_frame = nullptr;
		CoUninitialize();
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ApplyAntiZapretStyle(dpiScale);
	LoadCyrillicFont(dpiScale);

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(frame.device, frame.context);

	ShowWindow(hwnd, SW_SHOWNORMAL);
	SetForegroundWindow(hwnd);
	UpdateWindow(hwnd);

	bool completed = false;

	while (frame.running)
	{
		MSG msg = {};
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			if (msg.message == WM_QUIT)
				frame.running = false;
		}
		if (!frame.running)
			break;

		if (state.closeRequested)
		{
			completed = state.installRequested;
			frame.running = false;
			break;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		DrawUi(hwnd, state, dpiScale);
		ImGui::Render();

		const float clear[4] = { 0.06275f, 0.06275f, 0.06275f, 1.f };
		frame.context->OMSetRenderTargets(1, &frame.rtv, nullptr);
		frame.context->ClearRenderTargetView(frame.rtv, clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		frame.swapChain->Present(1, 0);
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	JoinInstallThread(frame);
	CleanupDevice(frame);
	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	g_frame = nullptr;
	CoUninitialize();
	return completed;
}
