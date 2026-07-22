#pragma once

#include "ui/ui_smooth_scroll.h"
#include "ui/ui_types.h"

class UiAntiZapretPage;
class UiHomePage;
class UiTgFixPage;
class UiVpnPage;
class UiRoutingPage;
class UiConsolePage;
class UiSettingsPage;
class UiAboutPage;
class FontManager;
class ThemeManager;
class AppSettings;

class UiPageHost
{
public:
	void SetAppSettings(AppSettings* settings) { m_appSettings = settings; }
	void Update(float deltaTime, UiTab activeTab);
	void Draw(
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
		UiAboutPage& aboutPage);

private:
	static int TabIndex(UiTab tab);
	void DrawPage(
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
		float deltaTime);

	UiTab m_tab = UiTab::Home;
	UiTab m_fromTab = UiTab::Home;
	UiTab m_toTab = UiTab::Home;
	float m_elapsed = 0.f;
	bool m_animActive = false;
	UiSmoothScroll m_scroll[8];
	AppSettings* m_appSettings = nullptr;

	static constexpr float kPageAnimSec = 0.28f;
};
