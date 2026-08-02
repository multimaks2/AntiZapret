#include "app/app_settings.h"

#include "app/settings_document.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <wincrypt.h>

#include <cctype>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#pragma comment(lib, "advapi32.lib")

namespace
{
	std::filesystem::path SettingsPath()
	{
		return std::filesystem::path(ZapretPaths::GetSettingsPath());
	}

	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t");
		return value.substr(start, end - start + 1);
	}

	bool ParseBool(const std::string& value)
	{
		return value == "1" || value == "true" || value == "yes";
	}

	float ParseFloat(const std::string& value, float fallback)
	{
		if (value.empty())
			return fallback;
		return static_cast<float>(std::atof(value.c_str()));
	}

	void ResetScrollMultipliers(std::array<float, AppSettings::kPageScrollCount>& values)
	{
		values.fill(AppSettings::kDefaultScrollMultiplier);
	}
}

float AppSettings::ClampScrollMultiplier(float value)
{
	if (value < kMinScrollMultiplier)
		return kMinScrollMultiplier;
	if (value > kMaxScrollMultiplier)
		return kMaxScrollMultiplier;
	return value;
}

float AppSettings::ClampFontScale(float value)
{
	if (value < kMinFontScale)
		return kMinFontScale;
	if (value > kMaxFontScale)
		return kMaxFontScale;
	return value;
}

void AppSettings::Load()
{
	m_tgProxyHost = "127.0.0.1";
	m_tgProxyPort = 1443;
	m_tgProxySecret.clear();
	m_autoStartTgProxyWithAntiZapret = false;
	m_openTelegramOnProxyStart = false;
	m_themeId = UiThemeId::Dark;
	m_autostartApp = false;
	m_autostartBypass = false;
	m_autostartTelegram = false;
	m_autostartVpn = false;
	m_confirmAdult = false;
	m_discordPresenceEnabled = true;
	m_discordShowTotalUptime = false;
	m_appTotalRuntimeSec = 0;
	m_discordDownloadButtonEnabled = true;
	m_discordDownloadUrl =
		"https://github.com/multimaks2/AntiZapret/releases/latest/download/AntiZapret-Installer.exe";
	m_discordImportAntiZapretEnabled = false;
	m_discordImportVpnEnabled = false;
	m_discordImportTimedEnabled = true;
	m_discordImportDurationMinutes = 5;
	m_discordImportExpiresAt = 0;
	m_discordImportAzExpiresAt = 0;
	m_discordImportVpnExpiresAt = 0;
	m_autoSelectBestStrategy = false;
	m_showExtraStrategies = false;
	m_quickStrategyTest = false;
	m_networkSpeedBits = false;
	m_fontScale = kDefaultFontScale;
	m_customHwid.clear();
	ResetScrollMultipliers(m_pageScrollMultipliers);

	std::ifstream input(SettingsPath(), std::ios::binary);
	if (!input)
		return;

	std::string currentSection;
	std::string line;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			currentSection = line.substr(1, line.size() - 2);
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string key = Trim(line.substr(0, eq));
		const std::string value = Trim(line.substr(eq + 1));
		if (currentSection == "scroll")
		{
			if (key == "home")
				m_pageScrollMultipliers[0] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "antizapret")
				m_pageScrollMultipliers[1] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "tg_ws_proxy")
				m_pageScrollMultipliers[2] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "vpn")
				m_pageScrollMultipliers[3] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "routing")
				m_pageScrollMultipliers[4] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "console")
				m_pageScrollMultipliers[5] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "settings")
				m_pageScrollMultipliers[6] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			else if (key == "about")
				m_pageScrollMultipliers[7] = ClampScrollMultiplier(ParseFloat(value, kDefaultScrollMultiplier));
			continue;
		}

		if (currentSection == "ui")
		{
			if (key == "theme")
				m_themeId = ThemeManager::ThemeFromKey(value.c_str());
			else if (key == "light_theme")
			{
				// Legacy fallback when theme key is absent.
				if (m_themeId == UiThemeId::Dark && ParseBool(value))
					m_themeId = UiThemeId::Light;
			}
			else if (key == "autostart_app")
				m_autostartApp = ParseBool(value);
			else if (key == "autostart_bypass")
				m_autostartBypass = ParseBool(value);
			else if (key == "autostart_telegram")
				m_autostartTelegram = ParseBool(value);
			else if (key == "autostart_vpn")
				m_autostartVpn = ParseBool(value);
			else if (key == "confirm_adult")
				m_confirmAdult = ParseBool(value);
			else if (key == "discord_presence")
				m_discordPresenceEnabled = ParseBool(value);
			else if (key == "discord_total_uptime")
				m_discordShowTotalUptime = ParseBool(value);
			else if (key == "app_total_runtime_sec")
			{
				const std::int64_t sec = static_cast<std::int64_t>(std::atoll(value.c_str()));
				if (sec >= 0)
					m_appTotalRuntimeSec = sec;
			}
			else if (key == "discord_share_button")
			{
				// Legacy: old single "Import" toggle → AntiZapret import.
				m_discordImportAntiZapretEnabled = ParseBool(value);
			}
			else if (key == "discord_download_button")
				m_discordDownloadButtonEnabled = ParseBool(value);
			else if (key == "discord_download_url" && !value.empty())
				m_discordDownloadUrl = value;
			else if (key == "discord_import_az")
				m_discordImportAntiZapretEnabled = ParseBool(value);
			else if (key == "discord_import_vpn")
				m_discordImportVpnEnabled = ParseBool(value);
			else if (key == "discord_import_timed")
				m_discordImportTimedEnabled = ParseBool(value);
			else if (key == "discord_import_minutes")
			{
				const int minutes = std::atoi(value.c_str());
				if (minutes > 0)
					m_discordImportDurationMinutes = minutes > 180 ? 180 : minutes;
			}
			else if (key == "discord_import_expires_at")
				m_discordImportExpiresAt = static_cast<std::int64_t>(std::atoll(value.c_str()));
			else if (key == "discord_import_az_expires_at")
				m_discordImportAzExpiresAt = static_cast<std::int64_t>(std::atoll(value.c_str()));
			else if (key == "discord_import_vpn_expires_at")
				m_discordImportVpnExpiresAt = static_cast<std::int64_t>(std::atoll(value.c_str()));
			else if (key == "network_speed_bits")
				m_networkSpeedBits = ParseBool(value);
			else if (key == "font_scale")
				m_fontScale = ClampFontScale(ParseFloat(value, kDefaultFontScale));
			else if (key == "custom_hwid" && !value.empty())
				m_customHwid = value;
			continue;
		}

		if (currentSection == "vpn")
		{
			// Legacy location (pre-1.3.10) — only if [ui] did not already provide one.
			if (key == "custom_hwid" && m_customHwid.empty() && !value.empty())
				m_customHwid = value;
			continue;
		}

		if (currentSection == "antizapret")
		{
			if (key == "auto_select_best")
				m_autoSelectBestStrategy = ParseBool(value);
			else if (key == "show_extra_strategies")
				m_showExtraStrategies = ParseBool(value);
			else if (key == "quick_strategy_test")
				m_quickStrategyTest = ParseBool(value);
			continue;
		}

		if (currentSection != "tg_proxy" && !currentSection.empty())
			continue;

		if (key == "host")
			m_tgProxyHost = value.empty() ? "127.0.0.1" : value;
		else if (key == "port")
			m_tgProxyPort = value.empty() ? 1443 : std::atoi(value.c_str());
		else if (key == "secret")
			m_tgProxySecret = value;
		else if (key == "auto_start_with_antizapret")
			m_autoStartTgProxyWithAntiZapret = ParseBool(value);
		else if (key == "open_telegram_on_start")
			m_openTelegramOnProxyStart = ParseBool(value);
	}

	// Migrate legacy single expires_at into neither (require fresh tab activation).
	if (m_discordImportExpiresAt > 0
		&& m_discordImportAzExpiresAt == 0
		&& m_discordImportVpnExpiresAt == 0)
	{
		m_discordImportExpiresAt = 0;
	}
}

void AppSettings::Save()
{
	SettingsDocument::KeyMap tgProxy;
	tgProxy["host"] = m_tgProxyHost;
	tgProxy["port"] = std::to_string(m_tgProxyPort);
	tgProxy["secret"] = m_tgProxySecret;
	tgProxy["auto_start_with_antizapret"] = m_autoStartTgProxyWithAntiZapret ? "1" : "0";
	tgProxy["open_telegram_on_start"] = m_openTelegramOnProxyStart ? "1" : "0";

	SettingsDocument::KeyMap antizapret;
	antizapret["auto_select_best"] = m_autoSelectBestStrategy ? "1" : "0";
	antizapret["show_extra_strategies"] = m_showExtraStrategies ? "1" : "0";
	antizapret["quick_strategy_test"] = m_quickStrategyTest ? "1" : "0";

	SettingsDocument::KeyMap ui;
	ui["theme"] = ThemeManager::ThemeKey(m_themeId);
	ui["light_theme"] = ThemeManager::Info(m_themeId).isLight ? "1" : "0";
	ui["autostart_app"] = m_autostartApp ? "1" : "0";
	ui["autostart_bypass"] = m_autostartBypass ? "1" : "0";
	ui["autostart_telegram"] = m_autostartTelegram ? "1" : "0";
	ui["autostart_vpn"] = m_autostartVpn ? "1" : "0";
	ui["confirm_adult"] = m_confirmAdult ? "1" : "0";
	ui["discord_presence"] = m_discordPresenceEnabled ? "1" : "0";
	ui["discord_total_uptime"] = m_discordShowTotalUptime ? "1" : "0";
	ui["app_total_runtime_sec"] = std::to_string(static_cast<long long>(m_appTotalRuntimeSec));
	ui["discord_download_button"] = m_discordDownloadButtonEnabled ? "1" : "0";
	ui["discord_download_url"] = m_discordDownloadUrl;
	ui["discord_import_az"] = m_discordImportAntiZapretEnabled ? "1" : "0";
	ui["discord_import_vpn"] = m_discordImportVpnEnabled ? "1" : "0";
	ui["discord_import_timed"] = m_discordImportTimedEnabled ? "1" : "0";
	ui["discord_import_minutes"] = std::to_string(m_discordImportDurationMinutes);
	ui["discord_import_expires_at"] = "0";
	ui["discord_import_az_expires_at"] = std::to_string(static_cast<long long>(m_discordImportAzExpiresAt));
	ui["discord_import_vpn_expires_at"] = std::to_string(static_cast<long long>(m_discordImportVpnExpiresAt));
	ui["network_speed_bits"] = m_networkSpeedBits ? "1" : "0";
	ui["font_scale"] = std::to_string(m_fontScale);
	if (!m_customHwid.empty())
		ui["custom_hwid"] = m_customHwid;

	SettingsDocument::KeyMap scroll;
	scroll["home"] = std::to_string(m_pageScrollMultipliers[0]);
	scroll["antizapret"] = std::to_string(m_pageScrollMultipliers[1]);
	scroll["tg_ws_proxy"] = std::to_string(m_pageScrollMultipliers[2]);
	scroll["vpn"] = std::to_string(m_pageScrollMultipliers[3]);
	scroll["routing"] = std::to_string(m_pageScrollMultipliers[4]);
	scroll["console"] = std::to_string(m_pageScrollMultipliers[5]);
	scroll["settings"] = std::to_string(m_pageScrollMultipliers[6]);
	scroll["about"] = std::to_string(m_pageScrollMultipliers[7]);

	std::lock_guard<std::mutex> lock(SettingsDocument::Mutex());
	SettingsDocument::Doc doc;
	SettingsDocument::Load(doc);
	// Merge into [vpn] — subscription card meta and other VpnStore keys live here too.
	SettingsDocument::KeyMap vpn = SettingsDocument::GetSection(doc, "vpn");
	// HWID lives under [ui] now; drop legacy key so VpnStore saves cannot revive a stale value.
	vpn.erase("custom_hwid");
	SettingsDocument::SetSection(doc, "tg_proxy", tgProxy);
	SettingsDocument::SetSection(doc, "antizapret", antizapret);
	SettingsDocument::SetSection(doc, "ui", ui);
	SettingsDocument::SetSection(doc, "vpn", vpn);
	SettingsDocument::SetSection(doc, "scroll", scroll);
	SettingsDocument::Save(doc);
}

float AppSettings::GetPageScrollMultiplier(int pageIndex) const
{
	if (pageIndex < 0 || pageIndex >= kPageScrollCount)
		return kDefaultScrollMultiplier;
	return m_pageScrollMultipliers[static_cast<size_t>(pageIndex)];
}

void AppSettings::SetPageScrollMultiplier(int pageIndex, float value)
{
	if (pageIndex < 0 || pageIndex >= kPageScrollCount)
		return;
	m_pageScrollMultipliers[static_cast<size_t>(pageIndex)] = ClampScrollMultiplier(value);
}

void AppSettings::SavePageScrollMultipliers()
{
	Save();
}

std::string AppSettings::GenerateSecretHex32() const
{
	unsigned char bytes[16] = {};
	HCRYPTPROV provider = 0;
	if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		CryptGenRandom(provider, static_cast<DWORD>(sizeof bytes), bytes);
		CryptReleaseContext(provider, 0);
	}
	else
	{
		for (int i = 0; i < 16; ++i)
			bytes[i] = static_cast<unsigned char>(rand() & 0xFF);
	}

	static const char* hex = "0123456789abcdef";
	std::string result(32, '0');
	for (int i = 0; i < 16; ++i)
	{
		result[static_cast<size_t>(i * 2)] = hex[(bytes[i] >> 4) & 0xF];
		result[static_cast<size_t>(i * 2 + 1)] = hex[bytes[i] & 0xF];
	}
	return result;
}

std::string AppSettings::EnsureTgProxySecret()
{
	if (m_tgProxySecret.size() == 32)
	{
		bool valid = true;
		for (char ch : m_tgProxySecret)
		{
			if (!std::isxdigit(static_cast<unsigned char>(ch)))
			{
				valid = false;
				break;
			}
		}
		if (valid)
			return m_tgProxySecret;
	}

	m_tgProxySecret = GenerateSecretHex32();
	Save();
	return m_tgProxySecret;
}

void AppSettings::SetAutoStartTgProxyWithAntiZapret(bool value)
{
	m_autoStartTgProxyWithAntiZapret = value;
	if (value)
		m_suppressTgAutoStartWithAntiZapret = false;
	Save();
}

void AppSettings::SetOpenTelegramOnProxyStart(bool value)
{
	m_openTelegramOnProxyStart = value;
	Save();
}

void AppSettings::SetLightTheme(bool value)
{
	SetThemeId(value ? UiThemeId::Light : UiThemeId::Dark);
}

bool AppSettings::GetLightTheme() const
{
	return ThemeManager::Info(m_themeId).isLight;
}

void AppSettings::SetThemeId(UiThemeId value)
{
	if (value < UiThemeId::Dark || value >= UiThemeId::Count)
		value = UiThemeId::Dark;
	m_themeId = value;
	Save();
}

void AppSettings::SetAutostartApp(bool value)
{
	m_autostartApp = value;
	Save();
	SyncWindowsAutostart();
}

void AppSettings::SetAutostartBypass(bool value)
{
	m_autostartBypass = value;
	Save();
}

void AppSettings::SetAutostartTelegram(bool value)
{
	m_autostartTelegram = value;
	Save();
}

void AppSettings::SetAutostartVpn(bool value)
{
	m_autostartVpn = value;
	Save();
}

void AppSettings::SetConfirmAdult(bool value)
{
	m_confirmAdult = value;
	Save();
}

void AppSettings::SetNetworkSpeedBits(bool value)
{
	if (m_networkSpeedBits == value)
		return;
	m_networkSpeedBits = value;
	Save();
}

void AppSettings::SetFontScale(float value)
{
	m_fontScale = ClampFontScale(value);
}

void AppSettings::SaveFontScale()
{
	Save();
}

void AppSettings::SetCustomHwid(const std::string& value)
{
	if (m_customHwid == value)
		return;
	m_customHwid = value;
	Save();
}

void AppSettings::SetDiscordPresenceEnabled(bool value)
{
	m_discordPresenceEnabled = value;
	Save();
}

void AppSettings::SetDiscordShowTotalUptime(bool value)
{
	if (m_discordShowTotalUptime == value)
		return;
	m_discordShowTotalUptime = value;
	Save();
}

void AppSettings::AddAppTotalRuntimeSec(std::int64_t deltaSec)
{
	if (deltaSec <= 0)
		return;
	m_appTotalRuntimeSec += deltaSec;
	Save();
}

void AppSettings::SetDiscordDownloadButtonEnabled(bool value)
{
	m_discordDownloadButtonEnabled = value;
	Save();
}

void AppSettings::SetDiscordDownloadUrl(const std::string& value)
{
	std::string trimmed = value;
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n'))
		trimmed.pop_back();
	size_t start = 0;
	while (start < trimmed.size() && (trimmed[start] == ' ' || trimmed[start] == '\t'))
		++start;
	if (start > 0)
		trimmed = trimmed.substr(start);

	if (trimmed.empty())
		trimmed = "https://github.com/multimaks2/AntiZapret/releases/latest/download/AntiZapret-Installer.exe";
	if (trimmed == m_discordDownloadUrl)
		return;
	m_discordDownloadUrl = trimmed;
	Save();
}

void AppSettings::RestartDiscordImportTimer()
{
	// Kept for callers that expect a full refresh of armed timers.
	const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
	const int minutes = m_discordImportDurationMinutes > 0 ? m_discordImportDurationMinutes : 5;
	const std::int64_t expires =
		now + static_cast<std::int64_t>(minutes) * 60;

	bool changed = false;
	if (!m_discordImportTimedEnabled)
	{
		if (m_discordImportAzExpiresAt != 0 || m_discordImportVpnExpiresAt != 0)
		{
			m_discordImportAzExpiresAt = 0;
			m_discordImportVpnExpiresAt = 0;
			m_discordImportExpiresAt = 0;
			changed = true;
		}
	}
	else
	{
		if (m_discordImportAzExpiresAt > now)
		{
			m_discordImportAzExpiresAt = expires;
			changed = true;
		}
		if (m_discordImportVpnExpiresAt > now)
		{
			m_discordImportVpnExpiresAt = expires;
			changed = true;
		}
	}

	if (changed)
		Save();
}

void AppSettings::ActivateDiscordImportForTab(bool vpnTab)
{
	if (!m_discordImportTimedEnabled)
		return;

	const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
	const int minutes = m_discordImportDurationMinutes > 0 ? m_discordImportDurationMinutes : 5;
	const std::int64_t expires = now + static_cast<std::int64_t>(minutes) * 60;

	if (vpnTab)
	{
		if (!m_discordImportVpnEnabled)
			return;
		if (m_discordImportVpnExpiresAt > now)
			return; // already counting for VPN
		m_discordImportVpnExpiresAt = expires;
		Save();
		return;
	}

	if (!m_discordImportAntiZapretEnabled)
		return;
	if (m_discordImportAzExpiresAt > now)
		return;
	m_discordImportAzExpiresAt = expires;
	Save();
}

void AppSettings::ClearDiscordImportTimer()
{
	if (m_discordImportExpiresAt == 0
		&& m_discordImportAzExpiresAt == 0
		&& m_discordImportVpnExpiresAt == 0)
		return;
	m_discordImportExpiresAt = 0;
	m_discordImportAzExpiresAt = 0;
	m_discordImportVpnExpiresAt = 0;
	Save();
}

void AppSettings::TickDiscordImportExpiry()
{
	if (!m_discordImportTimedEnabled
		&& m_discordImportAzExpiresAt == 0
		&& m_discordImportVpnExpiresAt == 0)
		return;

	const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
	bool changed = false;

	if (m_discordImportAzExpiresAt > 0 && now >= m_discordImportAzExpiresAt)
	{
		m_discordImportAzExpiresAt = 0;
		m_discordImportAntiZapretEnabled = false;
		changed = true;
	}
	if (m_discordImportVpnExpiresAt > 0 && now >= m_discordImportVpnExpiresAt)
	{
		m_discordImportVpnExpiresAt = 0;
		m_discordImportVpnEnabled = false;
		changed = true;
	}

	if (m_discordImportTimedEnabled
		&& !m_discordImportAntiZapretEnabled
		&& !m_discordImportVpnEnabled
		&& m_discordImportAzExpiresAt == 0
		&& m_discordImportVpnExpiresAt == 0)
	{
		// Keep master toggle on so user can re-arm subtypes; only auto-off if both
		// subtypes were cleared by expiry while master stays — actually user asked
		// timer per tab; master can stay. Don't force master off.
	}

	if (changed)
		Save();
}

namespace
{
	int RemainingSecondsFromExpires(std::int64_t expiresAt, bool armed)
	{
		if (!armed || expiresAt <= 0)
			return -1;
		const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
		if (now >= expiresAt)
			return 0;
		return static_cast<int>(expiresAt - now);
	}
}

int AppSettings::GetDiscordImportRemainingSeconds() const
{
	const int az = GetDiscordImportRemainingSecondsAz();
	const int vpn = GetDiscordImportRemainingSecondsVpn();
	if (az < 0)
		return vpn;
	if (vpn < 0)
		return az;
	return (std::max)(az, vpn);
}

int AppSettings::GetDiscordImportRemainingSecondsAz() const
{
	return RemainingSecondsFromExpires(
		m_discordImportAzExpiresAt,
		m_discordImportTimedEnabled && m_discordImportAntiZapretEnabled);
}

int AppSettings::GetDiscordImportRemainingSecondsVpn() const
{
	return RemainingSecondsFromExpires(
		m_discordImportVpnExpiresAt,
		m_discordImportTimedEnabled && m_discordImportVpnEnabled);
}

bool AppSettings::IsDiscordImportAntiZapretActive() const
{
	if (!m_discordImportTimedEnabled || !m_discordImportAntiZapretEnabled)
		return false;
	if (m_discordImportAzExpiresAt <= 0)
		return false;
	return static_cast<std::int64_t>(std::time(nullptr)) < m_discordImportAzExpiresAt;
}

bool AppSettings::IsDiscordImportVpnActive() const
{
	if (!m_discordImportTimedEnabled || !m_discordImportVpnEnabled)
		return false;
	if (m_discordImportVpnExpiresAt <= 0)
		return false;
	return static_cast<std::int64_t>(std::time(nullptr)) < m_discordImportVpnExpiresAt;
}

bool AppSettings::IsDiscordImportButtonAvailable() const
{
	return IsDiscordImportAntiZapretActive() || IsDiscordImportVpnActive();
}

void AppSettings::SetDiscordImportAntiZapretEnabled(bool value)
{
	if (m_discordImportAntiZapretEnabled == value)
		return;
	m_discordImportAntiZapretEnabled = value;
	if (!value)
		m_discordImportAzExpiresAt = 0;
	Save();
}

void AppSettings::SetDiscordImportVpnEnabled(bool value)
{
	if (m_discordImportVpnEnabled == value)
		return;
	m_discordImportVpnEnabled = value;
	if (!value)
		m_discordImportVpnExpiresAt = 0;
	Save();
}

void AppSettings::SetDiscordImportTimedEnabled(bool value)
{
	if (m_discordImportTimedEnabled == value)
		return;
	m_discordImportTimedEnabled = value;
	if (!value)
	{
		m_discordImportAntiZapretEnabled = false;
		m_discordImportVpnEnabled = false;
		m_discordImportExpiresAt = 0;
		m_discordImportAzExpiresAt = 0;
		m_discordImportVpnExpiresAt = 0;
	}
	Save();
}

void AppSettings::SetDiscordImportDurationMinutes(int minutes)
{
	if (minutes < 1)
		minutes = 1;
	if (minutes > 180)
		minutes = 180;
	if (m_discordImportDurationMinutes == minutes)
		return;
	m_discordImportDurationMinutes = minutes;
	const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
	if (m_discordImportAzExpiresAt > now || m_discordImportVpnExpiresAt > now)
		RestartDiscordImportTimer();
	else
		Save();
}

bool AppSettings::GetAutoSelectBestStrategy() const
{
	return m_autoSelectBestStrategy;
}

void AppSettings::SetAutoSelectBestStrategy(bool value)
{
	m_autoSelectBestStrategy = value;
	Save();
}

bool AppSettings::GetShowExtraStrategies() const
{
	return m_showExtraStrategies;
}

void AppSettings::SetShowExtraStrategies(bool value)
{
	m_showExtraStrategies = value;
	Save();
}

bool AppSettings::GetQuickStrategyTest() const
{
	return m_quickStrategyTest;
}

void AppSettings::SetQuickStrategyTest(bool value)
{
	m_quickStrategyTest = value;
	Save();
}

void AppSettings::SyncWindowsAutostart() const
{
	ApplyWindowsAutostart(m_autostartApp);
}

void AppSettings::ApplyWindowsAutostart(bool enabled) const
{
	wchar_t exePathW[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return;

	const std::wstring runCommand = L"\"" + std::wstring(exePathW) + L"\" --autostart";
	constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	constexpr wchar_t kValueName[] = L"AntiZapret";
	constexpr char kTaskName[] = "AntiZapret_Autostart";

	auto runHidden = [](const std::string& commandLine, DWORD timeoutMs) -> bool {
		STARTUPINFOA si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::string mutableCmd = commandLine;
		if (!CreateProcessA(
				nullptr,
				mutableCmd.data(),
				nullptr,
				nullptr,
				FALSE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&si,
				&pi))
			return false;
		const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
		DWORD exitCode = 1;
		if (wait == WAIT_OBJECT_0)
			GetExitCodeProcess(pi.hProcess, &exitCode);
		else
			TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return wait == WAIT_OBJECT_0 && exitCode == 0;
	};

	char exePathA[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exePathA, MAX_PATH);

	if (enabled)
	{
		// ONLOGON + HIGHEST needs admin; often fails → fall back to HKCU Run.
		bool taskOk = false;
		const std::string trArg = std::string("\\\"") + exePathA + "\\\" --autostart";
		const std::string createHighest =
			std::string("cmd /c schtasks /Create /F /SC ONLOGON /TN \"") +
			kTaskName + "\" /TR \"" + trArg + "\" /RL HIGHEST";
		taskOk = runHidden(createHighest, 8000);

		if (taskOk)
		{
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
			{
				RegDeleteValueW(key, kValueName);
				RegCloseKey(key);
			}
			return;
		}

		HKEY key = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(
				key,
				kValueName,
				0,
				REG_SZ,
				reinterpret_cast<const BYTE*>(runCommand.c_str()),
				static_cast<DWORD>((runCommand.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(key);
		}
		return;
	}

	runHidden(std::string("cmd /c schtasks /Delete /F /TN \"") + kTaskName + "\"", 8000);
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
	{
		RegDeleteValueW(key, kValueName);
		RegCloseKey(key);
	}
}
