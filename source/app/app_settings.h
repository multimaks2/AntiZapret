#pragma once

#include <array>
#include <string>

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

	bool GetLightTheme() const { return m_lightTheme; }
	void SetLightTheme(bool value);

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

	bool GetAutoSelectBestStrategy() const;
	void SetAutoSelectBestStrategy(bool value);

	bool GetShowExtraStrategies() const;
	void SetShowExtraStrategies(bool value);

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
	bool m_lightTheme = false;
	bool m_autostartApp = false;
	bool m_autostartBypass = false;
	bool m_autostartTelegram = false;
	bool m_autostartVpn = false;
	bool m_confirmAdult = false;
	bool m_discordPresenceEnabled = true;
	bool m_autoSelectBestStrategy = false;
	bool m_showExtraStrategies = false;
	std::array<float, kPageScrollCount> m_pageScrollMultipliers {};
};
