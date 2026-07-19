#include "AntiZapret-Updater/updater_ui.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ui/ui_smooth_scroll.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
	constexpr int kWindowW = 520;
	constexpr int kWindowH = 380;
	constexpr float kTitleBarH = 28.f;
	constexpr float kCloseBtnSize = 14.f;
	constexpr float kUiRounding = 8.f;

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
		UpdaterUiState* state = nullptr;
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
			if (io.Fonts->AddFontFromFileTTF(pathUtf8, 15.f * dpiScale, nullptr, ranges))
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
		style.WindowRounding = 0.f; // host is already DWM-rounded
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

		c[ImGuiCol_WindowBg] = bg;
		c[ImGuiCol_ChildBg] = tile;
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = muted;
		c[ImGuiCol_FrameBg] = { 0.078f, 0.078f, 0.078f, 1.f };
		c[ImGuiCol_FrameBgHovered] = { 0.12f, 0.12f, 0.12f, 1.f };
		c[ImGuiCol_FrameBgActive] = { 0.14f, 0.14f, 0.14f, 1.f };
		c[ImGuiCol_Button] = { 0.16f, 0.16f, 0.16f, 1.f };
		c[ImGuiCol_ButtonHovered] = { 0.22f, 0.22f, 0.22f, 1.f };
		c[ImGuiCol_ButtonActive] = { 0.26f, 0.26f, 0.26f, 1.f };
		c[ImGuiCol_Header] = { 0.16f, 0.16f, 0.16f, 1.f };
		c[ImGuiCol_PlotHistogram] = ok;
		c[ImGuiCol_PlotHistogramHovered] = ok;
		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_ScrollbarBg] = bg;
		c[ImGuiCol_ScrollbarGrab] = border;
		c[ImGuiCol_ScrollbarGrabHovered] = { 0.28f, 0.28f, 0.28f, 1.f };
		c[ImGuiCol_ScrollbarGrabActive] = { 0.34f, 0.34f, 0.34f, 1.f };
	}

	void DrawCloseGlow(ImDrawList* drawList, ImVec2 center, ImU32 color, float radius)
	{
		const ImU32 rgb = color & 0x00FFFFFF;
		for (int i = 0; i < 5; ++i)
		{
			const float scale = 2.5f - i * 0.5f;
			const float alpha = 0.02f + i * 0.095f;
			drawList->AddCircleFilled(center, radius * scale, rgb | (ImU32(alpha * 255) << 24), 32);
		}
	}

	bool DrawTitleBar(HWND hwnd, UpdaterUiState& state, float width, float dpiScale, bool canClose)
	{
		const float barH = kTitleBarH * dpiScale;
		const float btn = kCloseBtnSize * dpiScale;

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06275f, 0.06275f, 0.06275f, 1.f));
		ImGui::BeginChild("##TitleBar", { width, barH }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 barMin = ImGui::GetWindowPos();
		const ImU32 mutedCol = IM_COL32(140, 140, 140, 255);
		const ImU32 okCol = IM_COL32(33, 176, 77, 255);
		constexpr ImU32 kCloseGlow = IM_COL32(255, 77, 54, 255);

		const float titleY = barMin.y + (barH - ImGui::GetFontSize()) * 0.5f;
		dl->AddText({ barMin.x + 12.f * dpiScale, titleY }, okCol, "AntiZapret");
		const ImVec2 brandSize = ImGui::CalcTextSize("AntiZapret");
		dl->AddText({ barMin.x + 12.f * dpiScale + brandSize.x + 8.f * dpiScale, titleY }, mutedCol, "Updater");

		const float closeX = width - btn - 12.f * dpiScale;
		const float closeY = (barH - btn) * 0.5f;
		ImGui::SetCursorPos({ closeX, closeY });

		static bool hoverClose = false;
		if (hoverClose)
			DrawCloseGlow(dl, { barMin.x + closeX + btn * 0.5f, barMin.y + closeY + btn * 0.5f }, kCloseGlow, btn * 0.5f);

		// Same traffic-light close as AntiZapret title bar (always red; click only when finished).
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn * 0.5f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.96f, 0.26f, 0.21f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 0.30f, 0.25f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.22f, 0.18f, 1.f));
		const bool pressed = ImGui::Button("##Close", { btn, btn });
		hoverClose = ImGui::IsItemHovered();
		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();

		if (pressed && canClose)
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			state.launchRequested = false;
			state.closeRequested = true;
		}

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
		return false;
	}

	void DrawUi(HWND hwnd, UpdaterUiState& state, float dpiScale)
	{
		static std::string title;
		static std::string status;
		static std::string error;
		static std::vector<std::string> logs;
		static float progress = 0.f;
		static bool finished = false;
		static bool failed = false;
		static std::uint64_t cachedRevision = UINT64_MAX;
		static UiSmoothScroll logScroll;
		static bool stickLogToBottom = true;
		static size_t lastLogCount = 0;

		const std::uint64_t rev = state.revision.load(std::memory_order_relaxed);
		if (rev != cachedRevision)
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			title = state.title;
			status = state.status;
			error = state.error;
			logs = state.logs;
			progress = state.progress;
			finished = state.finished;
			failed = state.failed;
			cachedRevision = state.revision.load(std::memory_order_relaxed);
		}

		if (logs.size() != lastLogCount)
		{
			if (logs.size() > lastLogCount)
				stickLogToBottom = true;
			lastLogCount = logs.size();
		}

		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::Begin(
			"##updater",
			nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

		const float width = ImGui::GetContentRegionAvail().x;
		const bool canClose = finished;
		DrawTitleBar(hwnd, state, width, dpiScale, canClose);

		ImGui::Dummy({ 0.f, 4.f * dpiScale });
		ImGui::Indent(16.f * dpiScale);

		const ImVec4 ok = { 33.f / 255.f, 176.f / 255.f, 77.f / 255.f, 1.f };
		const ImVec4 failCol = { 0.90f, 0.32f, 0.32f, 1.f };
		const ImVec4 muted = { 0.55f, 0.55f, 0.55f, 1.f };

		ImGui::TextUnformatted(title.c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, muted);
		ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x - 16.f * dpiScale);
		ImGui::TextWrapped("%s", status.empty() ? " " : status.c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::Spacing();
		char overlay[32] = {};
		snprintf(overlay, sizeof(overlay), "%.0f%%", progress * 100.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f * dpiScale);
		ImGui::ProgressBar(progress <= 0.f ? 0.001f : progress, ImVec2(-16.f * dpiScale, 20.f * dpiScale), overlay);
		ImGui::PopStyleVar();

		ImGui::Spacing();
		ImGui::TextUnformatted("Журнал");
		const float footerReserve = failed ? 100.f * dpiScale : (finished ? 48.f * dpiScale : 28.f * dpiScale);
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float logW = avail.x - 16.f * dpiScale;
		const float logH = (std::max)(80.f * dpiScale, avail.y - footerReserve);
		const float padX = 12.f * dpiScale;
		const float padY = 8.f * dpiScale;

		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kUiRounding * dpiScale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { padX, padY });
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(26.f / 255.f, 26.f / 255.f, 26.f / 255.f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(46.f / 255.f, 46.f / 255.f, 46.f / 255.f, 1.f));
		if (ImGui::BeginChild("##logFrame", { logW, logH }, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar))
		{
			logScroll.Draw(
				"##logScroll",
				{ logW - padX * 2.f, logH - padY * 2.f },
				ImGui::GetIO().DeltaTime,
				[&](float /*contentWidth*/) {
					const float wrap = ImGui::GetContentRegionAvail().x;
					ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrap);
					for (const std::string& line : logs)
						ImGui::TextUnformatted(line.c_str());
					ImGui::PopTextWrapPos();
				},
				1.f,
				&stickLogToBottom);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);

		ImGui::Dummy({ 0.f, 8.f * dpiScale });

		if (failed && !error.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, failCol);
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x - 16.f * dpiScale);
			ImGui::TextWrapped("Ошибка: %s", error.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		if (finished)
		{
			if (failed)
			{
				ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f * dpiScale);
				if (ImGui::Button("Запустить как есть", ImVec2(180.f * dpiScale, 0.f)))
				{
					std::lock_guard<std::mutex> lock(state.mutex);
					state.launchRequested = true;
					state.closeRequested = true;
				}
				ImGui::SameLine();
				if (ImGui::Button("Выход", ImVec2(100.f * dpiScale, 0.f)))
				{
					std::lock_guard<std::mutex> lock(state.mutex);
					state.launchRequested = false;
					state.closeRequested = true;
				}
				ImGui::PopStyleVar();
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ok);
				ImGui::TextUnformatted("Готово. Запуск AntiZapret...");
				ImGui::PopStyleColor();
			}
		}

		ImGui::Unindent(16.f * dpiScale);
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
			{
				std::lock_guard<std::mutex> lock(g_frame->state->mutex);
				if (!g_frame->state->finished)
					return 0;
				g_frame->state->closeRequested = true;
			}
			DestroyWindow(hwnd);
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

bool RunUpdaterUi(UpdaterUiState& state)
{
	ImGui_ImplWin32_EnableDpiAwareness();
	const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.style = CS_CLASSDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"AntiZapretUpdater";
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return true;

	const int w = int(kWindowW * dpiScale);
	const int h = int(kWindowH * dpiScale);
	const int sw = GetSystemMetrics(SM_CXSCREEN);
	const int sh = GetSystemMetrics(SM_CYSCREEN);

	HWND hwnd = CreateWindowExW(
		WS_EX_APPWINDOW,
		wc.lpszClassName,
		L"AntiZapret Updater",
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
		return true;

	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
	const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
	DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

	UiFrame frame;
	frame.hwnd = hwnd;
	frame.state = &state;
	frame.dpiScale = dpiScale;
	g_frame = &frame;

	if (!CreateDevice(hwnd, frame))
	{
		MessageBoxW(hwnd, L"Не удалось инициализировать окно обновления (D3D11).\nОбновление продолжится без UI.", L"AntiZapret-Updater", MB_OK | MB_ICONWARNING);
		DestroyWindow(hwnd);
		UnregisterClassW(wc.lpszClassName, wc.hInstance);
		g_frame = nullptr;
		return true;
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

	bool shouldLaunch = true;
	ULONGLONG successCloseAt = 0;

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

		{
			std::lock_guard<std::mutex> lock(state.mutex);
			if (state.closeRequested)
			{
				shouldLaunch = !state.failed || state.launchRequested;
				frame.running = false;
				break;
			}
			if (state.finished && !state.failed)
			{
				if (successCloseAt == 0)
					successCloseAt = GetTickCount64() + 500;
				else if (GetTickCount64() >= successCloseAt)
				{
					shouldLaunch = true;
					frame.running = false;
					break;
				}
			}
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
	CleanupDevice(frame);
	if (IsWindow(hwnd))
		DestroyWindow(hwnd);
	UnregisterClassW(wc.lpszClassName, wc.hInstance);
	g_frame = nullptr;
	return shouldLaunch;
}
