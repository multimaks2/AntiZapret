#include "app/application.h"

#include "app/app_update_gate.h"
#include "app/process_job.h"
#include "app/protocol_handler.h"
#include "gfx/d3d11_renderer.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "lua/lua_api.h"
#include "lua/lua_runtime.h"
#include "ui/ui_shell.h"
#include "window/window_manager.h"
#include "vpn/vpn_flag_icons.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <Windows.h>

class Application::Components
{
public:
	ThemeManager theme;
	FontManager fonts;
	D3D11Renderer renderer;
	WindowManager window;
	LuaRuntime lua;
	LuaApi luaApi;
	UiShell ui;

	float clearColor[4] = { 0.06275f, 0.06275f, 0.06275f, 1.f };
	bool ready = false;
	bool windowCreated = false;
	bool rendererReady = false;
	bool imguiContextReady = false;
	bool imguiWin32Ready = false;
	bool imguiDx11Ready = false;
};

int RunApplication()
{
	// Entry: AntiZapret.exe → AntiZapret-Updater → AntiZapret.exe --updated
	if (AppUpdateGate::HandOffToUpdaterAndShouldExit())
		return 0;

	if (ProtocolHandler::ForwardToExistingInstanceAndShouldExit())
		return 0;

	ProtocolHandler::RegisterUrlProtocol();

	const std::wstring cmdLine = GetCommandLineW() ? GetCommandLineW() : L"";
	ProtocolHandler::SetStartupFromAutostart(ProtocolHandler::CommandLineHasAutostartFlag(cmdLine));

	if (const ProtocolCommand startup = ProtocolHandler::ParseCommandLine(cmdLine); startup.valid)
		ProtocolHandler::Enqueue(startup);

	Application app;
	return app.Run();
}

int Application::Run()
{
	if (!Initialize())
	{
		Shutdown();
		return 1;
	}

	for (bool done = false; !done; )
	{
		MSG message = {};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageW(&message);
			if (message.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		if (m_components->window.IsMinimized() || m_components->renderer.TestOccluded())
		{
			Sleep(100);
			continue;
		}

		UpdateFrame();
		RenderFrame();
	}

	Shutdown();
	return 0;
}

bool Application::Initialize()
{
	m_components = new Components();

	ProcessJob::EnsureInitialized();
	ProcessJob::CleanupOrphansAtStartup();

	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	ImGui_ImplWin32_EnableDpiAwareness();

	const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(
		MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	int minWidth = int(640 * dpiScale);
	int minHeight = int(480 * dpiScale);
	UiShell::GetMinWindowSize(&minWidth, &minHeight);

	if (!m_components->window.Create(dpiScale, minWidth, minHeight))
		return false;
	m_components->windowCreated = true;

	const HWND hwnd = m_components->window.Handle();
	if (!m_components->renderer.Initialize(hwnd, WindowManager::QueryMonitorHz({ 0, 0 })))
		return false;
	m_components->rendererReady = true;

	ShowWindow(hwnd, SW_SHOWDEFAULT);
	UpdateWindow(hwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	m_components->imguiContextReady = true;
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(dpiScale);
	style.FontScaleDpi = dpiScale;
	style.WindowBorderSize = 0.f;
	style.FrameBorderSize = 0.f;

	m_components->fonts.Initialize();
	ImGui_ImplWin32_Init(hwnd);
	m_components->imguiWin32Ready = true;

	if (!ImGui_ImplDX11_Init(m_components->renderer.Device(), m_components->renderer.Context()))
		return false;
	m_components->imguiDx11Ready = true;
	VpnFlagIcons::Instance().Initialize(m_components->renderer.Device());

	m_components->luaApi.Bind(&m_components->lua, &m_components->fonts, &m_components->theme);
	if (!m_components->lua.Init(hwnd, m_components->luaApi))
		return false;

	m_components->window.SetResizeCallback([this](UINT width, UINT height) {
		if (m_components)
			m_components->renderer.Resize(width, height);
	});
	m_components->window.SetRenderCallback([this] { RenderFrame(); });
	m_components->window.SetInputHandler([this](UINT msg, WPARAM wParam, LPARAM lParam) {
		return m_components && m_components->luaApi.HandleWindowMessage(msg, wParam, lParam);
	});
	m_components->window.EnsureMinimumSize();

	m_components->ready = true;
	m_components->window.SetReady(true);
	return true;
}

void Application::Shutdown()
{
	if (!m_components)
		return;

	m_components->window.SetReady(false);
	m_components->ui.ShutdownDiscord();
	if (m_components->lua.IsReady())
		m_components->lua.Shutdown();

	if (m_components->imguiDx11Ready)
	{
		VpnFlagIcons::Instance().Shutdown();
		ImGui_ImplDX11_Shutdown();
		m_components->imguiDx11Ready = false;
	}
	if (m_components->imguiWin32Ready)
	{
		ImGui_ImplWin32_Shutdown();
		m_components->imguiWin32Ready = false;
	}
	if (m_components->imguiContextReady)
	{
		ImGui::DestroyContext();
		m_components->imguiContextReady = false;
	}

	if (m_components->rendererReady)
	{
		m_components->renderer.Shutdown();
		m_components->rendererReady = false;
	}
	if (m_components->windowCreated)
	{
		m_components->window.Destroy();
		m_components->windowCreated = false;
	}

	delete m_components;
	m_components = nullptr;
	CoUninitialize();
}

void Application::UpdateFrame()
{
	if (!m_components)
		return;

	m_components->theme.Update(ImGui::GetIO().DeltaTime);
	m_components->clearColor[0] = m_components->theme.GetClearColorRGBA(0);
	m_components->clearColor[1] = m_components->theme.GetClearColorRGBA(1);
	m_components->clearColor[2] = m_components->theme.GetClearColorRGBA(2);
	m_components->clearColor[3] = m_components->theme.GetClearColorRGBA(3);
	m_components->theme.ApplySystemTheme(m_components->window.Handle());
	m_components->window.UpdateAnimations();
}

void Application::RenderFrame()
{
	if (!m_components || !m_components->ready)
		return;

	m_components->renderer.Render(
		[this] {
			m_components->ui.Render(
				m_components->window.Handle(),
				m_components->lua.State(),
				m_components->window,
				m_components->theme,
				m_components->fonts,
				m_components->luaApi);
		},
		m_components->clearColor);
}
