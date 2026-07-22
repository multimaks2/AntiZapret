#pragma once

#include <Windows.h>
#include "imgui.h"

#include "gfx/theme_manager.h"
#include "ui/ui_antizapret_page.h"
#include "ui/ui_home_page.h"
#include "ui/ui_types.h"
#include "ui/ui_page_host.h"
#include "ui/ui_routing_page.h"
#include "ui/ui_about_page.h"
#include "ui/ui_settings_page.h"
#include "ui/ui_console_page.h"
#include "ui/ui_sidebar.h"
#include "ui/ui_tgfix_page.h"
#include "ui/ui_vpn_page.h"
#include "app/app_settings.h"
#include "discord/discord_presence.h"
#include "net/traffic_monitor.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "zapret/zapret_manager.h"
#include "vpn/vpn_manager.h"
#include "window/window_manager.h"

struct lua_State;

class LuaApi;
class FontManager;
class ThemeManager;

class UiShell
{
public:
	void Render(
		HWND hwnd,
		lua_State* L,
		WindowManager& window,
		ThemeManager& theme,
		FontManager& fonts,
		LuaApi& api);

	void SetActiveTab(UiTab tab) { m_activeTab = tab; }
	TrayMenuState GetTrayMenuState();
	void HandleTrayCommand(TrayCommand command, int param = 0);
	void UpdateBackground(float deltaTime);
	void ShutdownDiscord();

	static float TitleBarHeight();
	static void GetMinWindowSize(int* minWidth, int* minHeight);

private:
	class TitleBar
	{
	public:
		void Draw(HWND hwnd, lua_State* L, WindowManager& window, ThemeManager& theme, LuaApi& api, float width);

	private:
		struct TrafficLightButton
		{
			const char* id = nullptr;
			ImU32 glow = 0;
			ImVec4 normal = {};
			ImVec4 hovered = {};
			ImVec4 active = {};
			void (*action)(HWND, WindowManager&) = nullptr;
		};

		void DrawBrand(ImDrawList* drawList, ImVec2 barMin, float height, const char* title, const ThemeManager& theme) const;
		void DrawButtons(HWND hwnd, WindowManager& window, float width, float height);
		bool DrawButton(const TrafficLightButton& button, ImVec2 position, float size, bool& hovered) const;
		void DrawGlow(ImDrawList* drawList, ImVec2 center, ImU32 color, float radius) const;
	};

	void DrawMainLayout(ThemeManager& theme, FontManager& fonts, float width, float height);
	void ProcessProtocolCommands();

	UiSidebar m_sidebar;
	UiPageHost m_pageHost;
	UiHomePage m_homePage;
	UiAntiZapretPage m_antiZapretPage;
	UiTgFixPage m_tgFixPage;
	UiVpnPage m_vpnPage;
	UiRoutingPage m_routingPage;
	UiConsolePage m_consolePage;
	UiSettingsPage m_settingsPage;
	UiAboutPage m_aboutPage;
	UiTab m_activeTab = UiTab::Home;
	UiTab m_previousTab = UiTab::Home;
	ZapretManager m_zapretManager;
	TgWsProxyManager m_tgWsProxyManager;
	VpnManager m_vpnManager;
	TrafficMonitor m_trafficMonitor;
	AppSettings m_appSettings;
	AppRichPresence m_discordPresence;
	bool m_zapretPageInitialized = false;
	bool m_startupActionsDone = false;
};
