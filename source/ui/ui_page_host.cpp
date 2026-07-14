#include "ui/ui_page_host.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_about_page.h"
#include "ui/ui_home_page.h"
#include "ui/ui_antizapret_page.h"
#include "ui/ui_console_page.h"
#include "ui/ui_routing_page.h"
#include "ui/ui_settings_page.h"
#include "ui/ui_tgfix_page.h"
#include "ui/ui_vpn_page.h"
#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace
{
	float Clamp(float value, float minValue, float maxValue)
	{
		if (value < minValue)
			return minValue;
		if (value > maxValue)
			return maxValue;
		return value;
	}

	float EaseOutCubic(float t)
	{
		const float u = 1.f - t;
		return 1.f - u * u * u;
	}
}

int UiPageHost::TabIndex(UiTab tab)
{
	switch (tab)
	{
	case UiTab::Home: return 0;
	case UiTab::AntiZapret: return 1;
	case UiTab::TgWsProxy: return 2;
	case UiTab::Vpn: return 3;
	case UiTab::Routing: return 4;
	case UiTab::Console: return 5;
	case UiTab::Settings: return 6;
	case UiTab::About: return 7;
	}
	return 0;
}

void UiPageHost::Update(float deltaTime, UiTab activeTab)
{
	if (activeTab != m_tab)
	{
		m_fromTab = m_tab;
		m_toTab = activeTab;
		m_tab = activeTab;
		m_elapsed = 0.f;
		m_animActive = true;
	}

	if (!m_animActive)
		return;

	m_elapsed += deltaTime;
	if (m_elapsed >= kPageAnimSec)
	{
		m_elapsed = kPageAnimSec;
		m_animActive = false;
	}
}

void UiPageHost::Draw(
	UiTab activeTab,
	ThemeManager& theme,
	FontManager& fonts,
	float width,
	float height,
	UiAntiZapretPage& antiZapretPage,
	UiHomePage& homePage,
	UiTgFixPage& tgFixPage,
	UiVpnPage& vpnPage,
	UiRoutingPage& routingPage,
	UiConsolePage& consolePage,
	UiSettingsPage& settingsPage,
	UiAboutPage& aboutPage)
{
	(void)activeTab;
	const float deltaTime = ImGui::GetIO().DeltaTime;
	Update(deltaTime, activeTab);

	const ImVec2 hostPos = ImGui::GetCursorScreenPos();
	const ImVec2 hostMax = { hostPos.x + width, hostPos.y + height };
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(hostPos, hostMax, true);

	ImGui::BeginChild("##PageHostViewport", { width, height }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

	if (!m_animActive)
	{
		DrawPage(
			m_tab,
			0.f,
			width,
			height,
			theme,
			fonts,
			antiZapretPage,
			homePage,
			tgFixPage,
			vpnPage,
			routingPage,
			consolePage,
			settingsPage,
			aboutPage,
			deltaTime);
	}
	else
	{
		const float t = EaseOutCubic(Clamp(m_elapsed / kPageAnimSec, 0.f, 1.f));
		const int direction = TabIndex(m_toTab) - TabIndex(m_fromTab);
		const float fromY = direction > 0 ? -t * height : t * height;
		const float toY = direction > 0 ? (1.f - t) * height : (t - 1.f) * height;

		DrawPage(
			m_fromTab,
			fromY,
			width,
			height,
			theme,
			fonts,
			antiZapretPage,
			homePage,
			tgFixPage,
			vpnPage,
			routingPage,
			consolePage,
			settingsPage,
			aboutPage,
			deltaTime);
		DrawPage(
			m_toTab,
			toY,
			width,
			height,
			theme,
			fonts,
			antiZapretPage,
			homePage,
			tgFixPage,
			vpnPage,
			routingPage,
			consolePage,
			settingsPage,
			aboutPage,
			deltaTime);
	}

	ImGui::EndChild();
	drawList->PopClipRect();
}

void UiPageHost::DrawPage(
	UiTab tab,
	float offsetY,
	float width,
	float height,
	ThemeManager& theme,
	FontManager& fonts,
	UiAntiZapretPage& antiZapretPage,
	UiHomePage& homePage,
	UiTgFixPage& tgFixPage,
	UiVpnPage& vpnPage,
	UiRoutingPage& routingPage,
	UiConsolePage& consolePage,
	UiSettingsPage& settingsPage,
	UiAboutPage& aboutPage,
	float deltaTime)
{
	ImGui::SetCursorPos({ 0.f, offsetY });
	ImGui::PushID(TabIndex(tab));
	const int scrollIndex = TabIndex(tab);
	const float wheelMultiplier = m_appSettings
		? m_appSettings->GetPageScrollMultiplier(scrollIndex)
		: AppSettings::kDefaultScrollMultiplier;
	char scrollId[32] = {};
	snprintf(scrollId, sizeof scrollId, "##page_scroll_%d", scrollIndex);
	m_scroll[scrollIndex].Draw(
		scrollId,
		{ width, height },
		deltaTime,
		[&](float contentWidth) {
			switch (tab)
			{
			case UiTab::Home:
				homePage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::AntiZapret:
				antiZapretPage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::TgWsProxy:
				tgFixPage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::Vpn:
				vpnPage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::Routing:
				routingPage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::Console:
				consolePage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::Settings:
				settingsPage.DrawContent(theme, fonts, contentWidth);
				break;
			case UiTab::About:
				aboutPage.DrawContent(theme, fonts, contentWidth);
				break;
			}
		},
		wheelMultiplier);
	ImGui::PopID();
}
