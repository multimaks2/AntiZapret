#pragma once

#include <array>
#include <string>

#include "gfx/theme_manager.h"

class AppSettings
{
public:
	static constexpr int kPageScrollCount = 8;
	static constexpr float kDefaultScrollMultiplier = 2.f;
	static constexpr float kMinScrollMultiplier = 0.5f;
	static constexpr float kMaxScrollMultiplier = 10.f;

	void Load();
	void Save();

	const std::string& GetTgProxyHost() const { return m_tgProxyHost; }
	int GetTgProxyPort() const { return m_tgProxyPort; }
	const std::string& GetTgProxySecret() const { return m_tgProxySecret; }
	std::string EnsureTgProxySecret();

	bool GetAutoStartTgProxyWithAntiZapret() const { return m_autoStartTgProxyWithAntiZapret; }
	void SetAutoStartTgProxyWithAntiZapret(bool value);

	bool IsTgAutoStartSuppressed() const { return m_suppressTgAutoStartWithAntiZapret; }
	void SetTgAutoStartSuppressed(bool value) { m_suppressTgAutoStartWithAntiZapret = value; }

	bool GetOpenTelegramOnProxyStart() const { return m_openTelegramOnProxyStart; }
	void SetOpenTelegramOnProxyStart(bool value);

	bool GetLightTheme() const;
	void SetLightTheme(bool value);

	UiThemeId GetThemeId() const { return m_themeId; }
	void SetThemeId(UiThemeId value);

	bool GetAutostartApp() const { return m_autostartApp; }
	void SetAutostartApp(bool value);

	bool GetAutostartBypass() const { return m_autostartBypass; }
	void SetAutostartBypass(bool value);

	bool GetAutostartTelegram() const { return m_autostartTelegram; }
	void SetAutostartTelegram(bool value);

	bool GetAutostartVpn() const { return m_autostartVpn; }
	void SetAutostartVpn(bool value);

	bool GetConfirmAdult() const { return m_confirmAdult; }
	void SetConfirmAdult(bool value);

	bool GetDiscordPresenceEnabled() const { return m_discordPresenceEnabled; }
	void SetDiscordPresenceEnabled(bool value);

	bool GetDiscordShareButtonEnabled() const { return m_discordShareButtonEnabled; }
	void SetDiscordShareButtonEnabled(bool value);

	bool GetDiscordDownloadButtonEnabled() const { return m_discordDownloadButtonEnabled; }
	void SetDiscordDownloadButtonEnabled(bool value);

	const std::string& GetDiscordDownloadUrl() const { return m_discordDownloadUrl; }
	void SetDiscordDownloadUrl(const std::string& value);

	bool GetAutoSelectBestStrategy() const;
	void SetAutoSelectBestStrategy(bool value);

	bool GetShowExtraStrategies() const;
	void SetShowExtraStrategies(bool value);

	bool GetQuickStrategyTest() const;
	void SetQuickStrategyTest(bool value);

	// false = МБ/с (bytes), true = Мбит/с (bits) — Steam-style network metrics.
	bool GetNetworkSpeedBits() const { return m_networkSpeedBits; }
	void SetNetworkSpeedBits(bool value);

	// Optional override for VPN subscription x-hwid header. Empty = auto (MachineGuid).
	const std::string& GetCustomHwid() const { return m_customHwid; }
	void SetCustomHwid(const std::string& value);

	float GetPageScrollMultiplier(int pageIndex) const;
	void SetPageScrollMultiplier(int pageIndex, float value);
	void SavePageScrollMultipliers();
	void SyncWindowsAutostart() const;

private:
	std::string GenerateSecretHex32() const;
	static float ClampScrollMultiplier(float value);
	void ApplyWindowsAutostart(bool enabled) const;

	std::string m_tgProxyHost = "127.0.0.1";
	int m_tgProxyPort = 1443;
	std::string m_tgProxySecret;
	bool m_autoStartTgProxyWithAntiZapret = false;
	bool m_suppressTgAutoStartWithAntiZapret = false;
	bool m_openTelegramOnProxyStart = false;
	UiThemeId m_themeId = UiThemeId::Dark;
	bool m_autostartApp = false;
	bool m_autostartBypass = false;
	bool m_autostartTelegram = false;
	bool m_autostartVpn = false;
	bool m_confirmAdult = false;
	bool m_discordPresenceEnabled = true;
	bool m_discordShareButtonEnabled = true;
	bool m_discordDownloadButtonEnabled = true;
	std::string m_discordDownloadUrl = "https://github.com/multimaks2/AntiZapret/releases/latest";
	bool m_autoSelectBestStrategy = false;
	bool m_showExtraStrategies = false;
	bool m_quickStrategyTest = false;
	bool m_networkSpeedBits = false;
	std::string m_customHwid;
	std::array<float, kPageScrollCount> m_pageScrollMultipliers {};
};
