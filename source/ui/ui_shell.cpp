#include "ui/ui_shell.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "app/app_version.h"
#include "app/protocol_handler.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "lua/lua_api.h"
#include "ui/ui_common.h"
#include "window/window_manager.h"
#include "vpn/vpn_flag_icons.h"
#include "vpn/vpn_module_update_check.h"
#include "vpn/vpn_rules_updater.h"
#include "zapret/smart_strategy_engine.h"
#include "zapret/strategies.hpp"
#include "zapret/zapret_paths.h"
#include "zapret/zapret_update_apply.h"
#include "zapret/zapret_update_check.h"
#include "imgui.h"

#include <cstdio>
#include <string>

namespace
{
	constexpr float kTitleBarHeight = 28.f;
	constexpr float kTitlePadLeft = 12.f;
	constexpr float kButtonSize = 14.f;
	constexpr float kButtonGap = 8.f;

	void SetWindowTitleUtf8(HWND hwnd, const char* text)
	{
		if (!hwnd || !text)
			return;

		wchar_t wide[256] = {};
		const int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, static_cast<int>(sizeof wide / sizeof wide[0]));
		if (len > 0)
			SetWindowTextW(hwnd, wide);
	}

#ifdef _DEBUG
	std::string BuildDebugTitle(HWND hwnd, const std::string& baseTitle)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const UINT monitorHz = WindowManager::QueryMonitorHzForWindow(hwnd);
		char buffer[192] = {};
		snprintf(
			buffer,
			sizeof buffer,
			"%s | UI %.1f FPS (%.2f ms, monitor %u Hz)",
			baseTitle.c_str(),
			io.Framerate,
			io.DeltaTime * 1000.f,
			monitorHz);
		return buffer;
	}
#endif
}

void UiShell::Render(
	HWND hwnd,
	lua_State* L,
	WindowManager& window,
	ThemeManager& theme,
	FontManager& fonts,
	LuaApi& api)
{
	RECT clientRect = {};
	GetClientRect(hwnd, &clientRect);
	const float width = float(clientRect.right - clientRect.left);
	const float height = float(clientRect.bottom - clientRect.top);

	api.BeginFrame(hwnd);
	TitleBar{}.Draw(hwnd, L, window, theme, api, width);
	api.BeginContent(kTitleBarHeight, width, height - kTitleBarHeight);
	DrawMainLayout(theme, fonts, width, height - kTitleBarHeight);
	api.EndContent();
	api.EndFrame();
}

void UiShell::DrawMainLayout(ThemeManager& theme, FontManager& fonts, float width, float height)
{
	if (!m_zapretPageInitialized)
	{
		ZapretPaths::EnsureDataLayout();
		m_appSettings.Load();
		m_appSettings.SyncWindowsAutostart();
		theme.SetLight(m_appSettings.GetLightTheme());
		theme.SetMix(m_appSettings.GetLightTheme() ? 1.f : 0.f);

		m_tgWsProxyManager.SetSettings(&m_appSettings);
		m_antiZapretPage.SetManager(&m_zapretManager);
		m_antiZapretPage.SetTgProxyManager(&m_tgWsProxyManager);
		m_antiZapretPage.SetAppSettings(&m_appSettings);
		m_zapretManager.SetTgProxyManager(&m_tgWsProxyManager);
		m_zapretManager.SetAppSettings(&m_appSettings);
		m_tgFixPage.SetManager(&m_tgWsProxyManager);
		m_tgFixPage.SetSettings(&m_appSettings);
		m_vpnPage.SetManager(&m_vpnManager);
		m_routingPage.SetVpnManager(&m_vpnManager);
		m_routingPage.SetAppSettings(&m_appSettings);
		m_homePage.SetManagers(
			&m_zapretManager,
			&m_tgWsProxyManager,
			&m_vpnManager,
			&m_vpnPage,
			&m_appSettings,
			&m_trafficMonitor);
		m_pageHost.SetAppSettings(&m_appSettings);
		m_settingsPage.SetAppSettings(&m_appSettings);
		m_settingsPage.SetThemeManager(&theme);
		m_settingsPage.SetVpnManager(&m_vpnManager);
		VpnRulesUpdater::StartBackgroundUpdate();
		VpnFlagIcons::Instance().StartBackgroundDownloadAll();
		// Only check versions in background — download starts from the page button.
		ZapretUpdateCheck::Instance().StartBackgroundCheck();
		VpnModuleUpdateCheck::Instance().StartBackgroundCheck();
		m_zapretPageInitialized = true;
	}

	if (!m_startupActionsDone)
	{
		m_zapretManager.LoadStore();
		// Тумблеры «Автозапуск обхода / TG / VPN» — при любом старте приложения.
		if (m_appSettings.GetAutostartBypass())
		{
			if (m_zapretManager.IsSmartStrategyEnabled()
				&& m_zapretManager.GetStore().GetLastStrategy() == SmartStrategyEngine::kLabel)
			{
				AppLog::Instance().Append(LogSource::Zapret, "Автозапуск: AntiZapret, Умная стратегия");
				if (m_appSettings.GetAutoStartTgProxyWithAntiZapret())
					m_antiZapretPage.SetStartupAutostartBypass(true);
				m_zapretManager.RequestStartSmartStrategy(ZapretStrategies::GameFilterMode::Disabled);
			}
			else
			{
				const int strategyIndex = m_zapretManager.GetPreferredStrategyIndex(
					m_appSettings.GetAutoSelectBestStrategy());
				if (strategyIndex >= 0)
				{
					char msg[128] = {};
					snprintf(
						msg,
						sizeof msg,
						"Автозапуск: AntiZapret, стратегия %s",
						ZapretStrategies::GetStrategyLabel(strategyIndex).data());
					AppLog::Instance().Append(LogSource::Zapret, msg);
					if (m_appSettings.GetAutoStartTgProxyWithAntiZapret())
						m_antiZapretPage.SetStartupAutostartBypass(true);
					m_zapretManager.RequestStart(strategyIndex, ZapretStrategies::GameFilterMode::Disabled);
				}
			}
		}
		if (m_appSettings.GetAutostartTelegram())
		{
			AppLog::Instance().Append(LogSource::Telegram, "Автозапуск: TG WS Proxy");
			m_tgWsProxyManager.RequestStart(false);
		}
		if (m_appSettings.GetAutostartVpn())
		{
			m_vpnPage.UpdateRuntime();
			if (m_vpnPage.HasActiveServer())
			{
				AppLog::Instance().Append(LogSource::VpnRouting, "Автозапуск: VPN");
				m_vpnPage.SetVpnEnabled(true);
				m_vpnPage.UpdateRuntime();
			}
			else
			{
				AppLog::Instance().Append(
					LogSource::VpnRouting,
					"Автозапуск VPN пропущен: нет активного сервера.");
			}
		}
		m_startupActionsDone = true;
	}

	ProcessProtocolCommands();

	if (m_activeTab == UiTab::Console && m_previousTab != UiTab::Console)
		m_consolePage.SetFilterFromTab(m_previousTab);
	m_previousTab = m_activeTab;

	m_tgWsProxyManager.Update(ImGui::GetIO().DeltaTime);
	m_zapretManager.Update(ImGui::GetIO().DeltaTime);
	m_vpnManager.Update(ImGui::GetIO().DeltaTime);
	m_vpnPage.UpdateRuntime();
	m_trafficMonitor.Update(ImGui::GetIO().DeltaTime);
	std::string discordDetails = "бездействует";
	if (m_zapretManager.IsRunning())
	{
		const char* gameFilterLabel = "OFF";
		switch (m_zapretManager.GetActiveGameFilterMode())
		{
		case ZapretStrategies::GameFilterMode::Tcp:
			gameFilterLabel = "TCP";
			break;
		case ZapretStrategies::GameFilterMode::Udp:
			gameFilterLabel = "UDP";
			break;
		case ZapretStrategies::GameFilterMode::All:
			gameFilterLabel = "ALL";
			break;
		case ZapretStrategies::GameFilterMode::Disabled:
		default:
			break;
		}

		std::string strategyName;
		if (m_zapretManager.IsActiveSmartStrategy())
			strategyName = SmartStrategyEngine::kLabel;
		else
		{
			const int activeIndex = m_zapretManager.GetActiveStrategyIndex();
			if (activeIndex >= 0)
				strategyName = m_zapretManager.GetStrategyLabel(activeIndex);
		}
		if (strategyName.empty())
			strategyName = "стратегия";

		char buf[160] = {};
		snprintf(buf, sizeof buf, "%s · GameFilter %s", strategyName.c_str(), gameFilterLabel);
		discordDetails = buf;
		constexpr size_t kMaxDiscordDetails = 120;
		if (discordDetails.size() > kMaxDiscordDetails)
			discordDetails.resize(kMaxDiscordDetails);
	}
	else if (m_vpnManager.IsRunning())
	{
		const std::string serverLabel = m_vpnPage.GetActiveServerPresenceLabel();
		if (!serverLabel.empty())
			discordDetails = serverLabel;
	}
	m_discordPresence.Update(
		m_activeTab,
		m_zapretManager.IsRunning(),
		m_tgWsProxyManager.IsRunning(),
		m_vpnManager.IsRunning(),
		discordDetails,
		m_appSettings.GetDiscordPresenceEnabled(),
		m_appSettings.GetDiscordShareButtonEnabled(),
		m_appSettings.GetDiscordDownloadButtonEnabled(),
		m_appSettings.GetDiscordDownloadUrl(),
		ImGui::GetIO().DeltaTime);

	const UiThemeColors colors = theme.GetColors();
	UiCommon::SyncImGuiStyle(colors);
	constexpr float kPagePad = 12.f;

	m_sidebar.Update(ImGui::GetIO().DeltaTime);

	auto displayVersion = [](const std::string& raw) -> std::string {
		// Sidebar/title: only show digit-based versions (e.g. 1.9.9d), not Unknown/Установлен.
		if (raw.empty() || raw == "—" || raw == "Unknown" || raw == "Установлен")
			return {};
		for (unsigned char ch : raw)
		{
			if (ch >= '0' && ch <= '9')
				return raw;
		}
		return {};
	};

	const auto& updateCheck = ZapretUpdateCheck::Instance();
	const UiSidebarVersionInfo azVersion {
		updateCheck.GetZapretStatus(),
		displayVersion(updateCheck.GetZapretLocalVersion())
	};
	const UiSidebarVersionInfo tgVersion {
		updateCheck.GetTgProxyStatus(),
		displayVersion(updateCheck.GetTgProxyLocalVersion())
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, 0.f });
	const float sidebarWidth = m_sidebar.Draw(m_activeTab, theme, fonts, height, azVersion, tgVersion);

	ImGui::SameLine(0.f, 0.f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.bg);
	ImGui::BeginChild("##MainArea", { width - sidebarWidth, height }, ImGuiChildFlags_None);

	ImGui::SetCursorPos({ kPagePad, kPagePad });
	const float contentWidth = ImGui::GetContentRegionAvail().x - kPagePad;
	const float contentHeight = ImGui::GetContentRegionAvail().y - kPagePad;

	m_pageHost.Draw(
		m_activeTab,
		theme,
		fonts,
		contentWidth,
		contentHeight,
		m_antiZapretPage,
		m_homePage,
		m_tgFixPage,
		m_vpnPage,
		m_routingPage,
		m_consolePage,
		m_settingsPage,
		m_aboutPage);

	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();
}

void UiShell::ProcessProtocolCommands()
{
	ProtocolCommand cmd;
	while (ProtocolHandler::TryDequeue(cmd))
	{
		if (!cmd.valid)
			continue;

		auto setTab = [this](const std::string& tab) {
			if (tab == "home" || tab == "главная")
				m_activeTab = UiTab::Home;
			else if (tab == "vpn")
				m_activeTab = UiTab::Vpn;
			else if (tab == "antizapret" || tab == "zapret" || tab == "bypass")
				m_activeTab = UiTab::AntiZapret;
			else if (tab == "tg" || tab == "telegram" || tab == "tgws" || tab == "tgfix")
				m_activeTab = UiTab::TgWsProxy;
			else if (tab == "routing" || tab == "route")
				m_activeTab = UiTab::Routing;
			else if (tab == "console" || tab == "log" || tab == "logs")
				m_activeTab = UiTab::Console;
			else if (tab == "settings" || tab == "options")
				m_activeTab = UiTab::Settings;
			else if (tab == "about")
				m_activeTab = UiTab::About;
		};

		if (cmd.action == "import")
		{
			m_activeTab = UiTab::Vpn;
			if (!cmd.importUrl.empty())
				m_vpnPage.ImportSubscriptionUrl(cmd.importUrl);
			else
				AppLog::Instance().Append(LogSource::VpnRouting, "Protocol import: empty url");
			continue;
		}

		if (cmd.action == "open")
		{
			setTab(cmd.openTab);
			AppLog::Instance().Append(LogSource::VpnRouting, "Protocol open: " + cmd.openTab);
			continue;
		}

		if (cmd.action == "control")
		{
			// Foundation for future remote control: target/action + query params are parsed and logged.
			// Handlers can be added per target (vpn, zapret, tg, app) without changing the URI scheme.
			std::string detail = "Protocol control: " + cmd.target;
			if (!cmd.controlAction.empty())
				detail += "/" + cmd.controlAction;
			for (const auto& p : cmd.params)
				detail += " " + p.first + "=" + p.second;
			AppLog::Instance().Append(LogSource::VpnRouting, detail);

			if (cmd.target == "vpn")
			{
				m_activeTab = UiTab::Vpn;
				if (cmd.controlAction == "import" || cmd.controlAction == "add")
				{
					std::string url = cmd.importUrl;
					if (url.empty())
					{
						for (const auto& p : cmd.params)
						{
							if (p.first == "url")
							{
								url = p.second;
								break;
							}
						}
					}
					if (!url.empty())
						m_vpnPage.ImportSubscriptionUrl(url);
				}
				else if (cmd.controlAction == "enable" || cmd.controlAction == "on" || cmd.controlAction == "start")
				{
					m_vpnPage.UpdateRuntime();
					m_vpnPage.SetVpnEnabled(true);
					m_vpnPage.UpdateRuntime();
				}
				else if (cmd.controlAction == "disable" || cmd.controlAction == "off" || cmd.controlAction == "stop")
				{
					m_vpnPage.SetVpnEnabled(false);
					m_vpnPage.UpdateRuntime();
				}
			}
			else if (cmd.target == "app" && (cmd.controlAction == "open" || cmd.controlAction == "focus"))
			{
				setTab(cmd.openTab.empty() ? "home" : cmd.openTab);
			}
			continue;
		}

		AppLog::Instance().Append(LogSource::VpnRouting, "Protocol unknown: " + cmd.raw);
	}
}

void UiShell::ShutdownDiscord()
{
	m_discordPresence.Shutdown();
}

float UiShell::TitleBarHeight()
{
	return kTitleBarHeight;
}

void UiShell::GetMinWindowSize(int* minWidth, int* minHeight)
{
	if (minWidth)
		*minWidth = 720;
	if (minHeight)
		*minHeight = 520;
}

namespace UiLayout
{
	float TitleBarHeight()
	{
		return UiShell::TitleBarHeight();
	}

	void GetMinWindowSize(int* minWidth, int* minHeight)
	{
		UiShell::GetMinWindowSize(minWidth, minHeight);
	}
}

void UiShell::TitleBar::Draw(HWND hwnd, lua_State* L, WindowManager& window, ThemeManager& theme, LuaApi& api, float width)
{
	const UiThemeColors colors = theme.GetColors();
	std::string title = L ? api.GetTitleBarText(L) : "AntiZapret";
#ifdef _DEBUG
	title = BuildDebugTitle(hwnd, title);
	SetWindowTitleUtf8(hwnd, title.c_str());
#endif

	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.bg);
	ImGui::BeginChild("##TitleBar", { width, kTitleBarHeight }, false, ImGuiWindowFlags_NoScrollbar);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 barMin = ImGui::GetWindowPos();
	DrawBrand(drawList, barMin, kTitleBarHeight, title.c_str(), theme);
	DrawButtons(hwnd, window, width, kTitleBarHeight);
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

void UiShell::TitleBar::DrawBrand(ImDrawList* drawList, ImVec2 barMin, float height, const char* title, const ThemeManager& theme) const
{
	ImFont* titleFont = ImGui::GetFont();
	const float titleFontSize = ImGui::GetFontSize();
	const ImVec2 titleSize = titleFont->CalcTextSizeA(titleFontSize, 1e4f, 0.f, title);
	const float titleY = barMin.y + (height - titleSize.y) * 0.5f;
	const float titleX = barMin.x + kTitlePadLeft;
	drawList->AddText(
		titleFont,
		titleFontSize,
		{ titleX, titleY },
		ImGui::GetColorU32(theme.GetColors().textPrimary),
		title);

	const std::string versionStr = AppVersion::ReadLocal();
	const char* version = versionStr.c_str();
	const float versionFontSize = titleFontSize * 0.82f;
	const ImVec2 versionSize = titleFont->CalcTextSizeA(versionFontSize, 1e4f, 0.f, version);
	const float padX = 5.f;
	const float padY = 1.f;
	const float badgeH = versionSize.y + padY * 2.f;
	const float badgeW = versionSize.x + padX * 2.f;
	const float badgeX = titleX + titleSize.x + 8.f;
	const float badgeY = barMin.y + (height - badgeH) * 0.5f;
	const ImVec2 badgeMin = { badgeX, badgeY };
	const ImVec2 badgeMax = { badgeX + badgeW, badgeY + badgeH };
	const ImU32 border = ImGui::GetColorU32(theme.GetAccents().ok);
	const ImU32 fill = IM_COL32(33, 176, 77, 28);
	drawList->AddRectFilled(badgeMin, badgeMax, fill, 3.f);
	drawList->AddRect(badgeMin, badgeMax, border, 3.f, 0, 1.25f);
	drawList->AddText(
		titleFont,
		versionFontSize,
		{ badgeX + padX, badgeY + padY },
		border,
		version);
}

void UiShell::TitleBar::DrawButtons(HWND hwnd, WindowManager& window, float width, float height)
{
	static bool hoverMinimize = false;
	static bool hoverMaximize = false;
	static bool hoverClose = false;

	static const TrafficLightButton kButtons[] = {
		{
			"##Minimize",
			IM_COL32(38, 191, 84, 255),
			{ 0.13f, 0.69f, 0.30f, 1.f },
			{ 0.15f, 0.75f, 0.33f, 1.f },
			{ 0.11f, 0.60f, 0.26f, 1.f },
			[](HWND target, WindowManager&) { ShowWindow(target, SW_MINIMIZE); },
		},
		{
			"##Maximize",
			IM_COL32(255, 204, 26, 255),
			{ 0.95f, 0.76f, 0.06f, 1.f },
			{ 1.0f, 0.80f, 0.10f, 1.f },
			{ 0.85f, 0.65f, 0.05f, 1.f },
			[](HWND, WindowManager& targetWindow) { targetWindow.ToggleMaximize(); },
		},
		{
			"##Close",
			IM_COL32(255, 77, 54, 255),
			{ 0.96f, 0.26f, 0.21f, 1.f },
			{ 1.0f, 0.30f, 0.25f, 1.f },
			{ 0.85f, 0.22f, 0.18f, 1.f },
			[](HWND target, WindowManager&) { PostMessageW(target, WM_CLOSE, 0, 0); },
		},
	};
	bool* hovers[] = { &hoverMinimize, &hoverMaximize, &hoverClose };

	const float y = (height - kButtonSize) * 0.5f;
	float x = width - (kButtonSize * 3 + kButtonGap * 2) - 8.f;
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kButtonSize * 0.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { kButtonGap, 0 });
	for (int i = 0; i < 3; ++i)
	{
		if (DrawButton(kButtons[i], { x, y }, kButtonSize, *hovers[i]))
			kButtons[i].action(hwnd, window);
		x += kButtonSize + kButtonGap;
	}
	ImGui::PopStyleVar(2);
}

bool UiShell::TitleBar::DrawButton(const TrafficLightButton& button, ImVec2 position, float size, bool& hovered) const
{
	if (hovered)
		DrawGlow(ImGui::GetWindowDrawList(), { position.x + size * 0.5f, position.y + size * 0.5f }, button.glow, size * 0.5f);

	ImGui::SetCursorPos(position);
	ImGui::PushStyleColor(ImGuiCol_Button, button.normal);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button.hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, button.active);
	const bool pressed = ImGui::Button(button.id, { size, size });
	hovered = ImGui::IsItemHovered();
	ImGui::PopStyleColor(3);
	return pressed;
}

void UiShell::TitleBar::DrawGlow(ImDrawList* drawList, ImVec2 center, ImU32 color, float radius) const
{
	const ImU32 rgb = color & 0x00FFFFFF;
	for (int i = 0; i < 5; ++i)
	{
		const float scale = 2.5f - i * 0.5f;
		const float alpha = 0.02f + i * 0.095f;
		drawList->AddCircleFilled(center, radius * scale, rgb | (ImU32(alpha * 255) << 24), 32);
	}
}
