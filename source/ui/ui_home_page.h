#pragma once

class FontManager;
class ThemeManager;
class ZapretManager;
class TgWsProxyManager;
class VpnManager;
class UiVpnPage;
class AppSettings;
class TrafficMonitor;

class UiHomePage
{
public:
	void SetManagers(
		ZapretManager* zapret,
		TgWsProxyManager* tgProxy,
		VpnManager* vpn,
		UiVpnPage* vpnPage,
		AppSettings* settings,
		TrafficMonitor* traffic);

	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	ZapretManager* m_zapret = nullptr;
	TgWsProxyManager* m_tgProxy = nullptr;
	VpnManager* m_vpn = nullptr;
	UiVpnPage* m_vpnPage = nullptr;
	AppSettings* m_settings = nullptr;
	TrafficMonitor* m_traffic = nullptr;
	bool m_preferencesLoaded = false;
	int m_selectedStrategy = 0;
	bool m_autoSelect = false;
	bool m_hasSmartStrategy = false;

	float m_cardDownloadBps = 0.f;
	float m_cardUploadBps = 0.f;
	float m_cardSampleTimer = 0.f;
	bool m_cardSampleReady = false;
};
