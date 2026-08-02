#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "gfx/theme_manager.h"

class AppSettings
{
public:
	static constexpr int kPageScrollCount = 8;
	static constexpr float kDefaultScrollMultiplier = 2.f;
	static constexpr float kMinScrollMultiplier = 0.5f;
	static constexpr float kMaxScrollMultiplier = 10.f;

	static constexpr float kDefaultFontScale = 1.f;
	static constexpr float kMinFontScale = 0.75f;
	static constexpr float kMaxFontScale = 1.75f;

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

	// Discord elapsed clock: total across launches vs current session only.
	bool GetDiscordShowTotalUptime() const { return m_discordShowTotalUptime; }
	void SetDiscordShowTotalUptime(bool value);

	std::int64_t GetAppTotalRuntimeSec() const { return m_appTotalRuntimeSec; }
	void AddAppTotalRuntimeSec(std::int64_t deltaSec);

	bool GetDiscordDownloadButtonEnabled() const { return m_discordDownloadButtonEnabled; }
	void SetDiscordDownloadButtonEnabled(bool value);

	const std::string& GetDiscordDownloadUrl() const { return m_discordDownloadUrl; }
	void SetDiscordDownloadUrl(const std::string& value);

	bool GetDiscordImportAntiZapretEnabled() const { return m_discordImportAntiZapretEnabled; }
	void SetDiscordImportAntiZapretEnabled(bool value);

	bool GetDiscordImportVpnEnabled() const { return m_discordImportVpnEnabled; }
	void SetDiscordImportVpnEnabled(bool value);

	bool GetDiscordImportTimedEnabled() const { return m_discordImportTimedEnabled; }
	void SetDiscordImportTimedEnabled(bool value);

	int GetDiscordImportDurationMinutes() const { return m_discordImportDurationMinutes; }
	void SetDiscordImportDurationMinutes(int minutes);

	std::int64_t GetDiscordImportExpiresAt() const { return m_discordImportExpiresAt; }
	std::int64_t GetDiscordImportAzExpiresAt() const { return m_discordImportAzExpiresAt; }
	std::int64_t GetDiscordImportVpnExpiresAt() const { return m_discordImportVpnExpiresAt; }

	// Start per-tab countdown when user opens AntiZapret / VPN with that import armed.
	void ActivateDiscordImportForTab(bool vpnTab);
	void RestartDiscordImportTimer();
	void ClearDiscordImportTimer();
	void TickDiscordImportExpiry();
	int GetDiscordImportRemainingSeconds() const;
	int GetDiscordImportRemainingSecondsAz() const;
	int GetDiscordImportRemainingSecondsVpn() const;
	bool IsDiscordImportAntiZapretActive() const;
	bool IsDiscordImportVpnActive() const;
	bool IsDiscordImportButtonAvailable() const;

	bool GetAutoSelectBestStrategy() const;
	void SetAutoSelectBestStrategy(bool value);

	bool GetShowExtraStrategies() const;
	void SetShowExtraStrategies(bool value);

	bool GetQuickStrategyTest() const;
	void SetQuickStrategyTest(bool value);

	// false = МБ/с (bytes), true = Мбит/с (bits) — Steam-style network metrics.
	bool GetNetworkSpeedBits() const { return m_networkSpeedBits; }
	void SetNetworkSpeedBits(bool value);

	// UI font scale coefficient (ImGui FontScaleMain). 1.0 = default.
	float GetFontScale() const { return m_fontScale; }
	void SetFontScale(float value);
	void SaveFontScale();

	// Optional override for VPN subscription x-hwid header. Empty = auto (MachineGuid).
	// Stored under [ui] custom_hwid (legacy [vpn] custom_hwid is still read once).
	const std::string& GetCustomHwid() const { return m_customHwid; }
	void SetCustomHwid(const std::string& value);

	float GetPageScrollMultiplier(int pageIndex) const;
	void SetPageScrollMultiplier(int pageIndex, float value);
	void SavePageScrollMultipliers();
	void SyncWindowsAutostart() const;

private:
	std::string GenerateSecretHex32() const;
	static float ClampScrollMultiplier(float value);
	static float ClampFontScale(float value);
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
	bool m_discordShowTotalUptime = false;
	std::int64_t m_appTotalRuntimeSec = 0;
	bool m_discordDownloadButtonEnabled = true;
	std::string m_discordDownloadUrl =
		"https://github.com/multimaks2/AntiZapret/releases/latest/download/AntiZapret-Installer.exe";
	bool m_discordImportAntiZapretEnabled = false;
	bool m_discordImportVpnEnabled = false;
	bool m_discordImportTimedEnabled = true;
	int m_discordImportDurationMinutes = 5;
	std::int64_t m_discordImportExpiresAt = 0; // legacy single timer (migrated on load)
	std::int64_t m_discordImportAzExpiresAt = 0;
	std::int64_t m_discordImportVpnExpiresAt = 0;
	bool m_autoSelectBestStrategy = false;
	bool m_showExtraStrategies = false;
	bool m_quickStrategyTest = false;
	bool m_networkSpeedBits = false;
	float m_fontScale = kDefaultFontScale;
	std::string m_customHwid;
	std::array<float, kPageScrollCount> m_pageScrollMultipliers {};
};
