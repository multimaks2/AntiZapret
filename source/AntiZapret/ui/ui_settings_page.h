#pragma once

class AppSettings;
class FontManager;
class ThemeManager;
class VpnManager;

class UiSettingsPage
{
public:
	void SetAppSettings(AppSettings* settings);
	void SetThemeManager(ThemeManager* theme) { m_theme = theme; }
	void SetVpnManager(VpnManager* manager) { m_vpnManager = manager; }
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	AppSettings* m_appSettings = nullptr;
	ThemeManager* m_theme = nullptr;
	float m_autostartAppMix = 0.f;
	float m_autostartBypassMix = 0.f;
	float m_autostartTelegramMix = 0.f;
	float m_autostartVpnMix = 0.f;
	float m_confirmAdultMix = 0.f;
	float m_discordPresenceMix = 1.f;
	float m_discordShareButtonMix = 1.f;
	float m_discordDownloadButtonMix = 1.f;
	char m_discordDownloadUrl[512] = {};
	bool m_loadedFromSettings = false;
	VpnManager* m_vpnManager = nullptr;
};
