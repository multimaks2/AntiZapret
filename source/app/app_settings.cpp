#include "app/app_settings.h"

#include "app/settings_document.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <wincrypt.h>

#include <cctype>
#include <cstdlib>
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

void AppSettings::Load()
{
	m_tgProxyHost = "127.0.0.1";
	m_tgProxyPort = 1443;
	m_tgProxySecret.clear();
	m_autoStartTgProxyWithAntiZapret = false;
	m_openTelegramOnProxyStart = false;
	m_lightTheme = false;
	m_autostartApp = false;
	m_autostartBypass = false;
	m_autostartTelegram = false;
	m_autostartVpn = false;
	m_confirmAdult = false;
	m_discordPresenceEnabled = true;
	m_discordShareButtonEnabled = true;
	m_discordDownloadButtonEnabled = true;
	m_discordDownloadUrl = "https://github.com/multimaks2/AntiZapret/releases/latest";
	m_autoSelectBestStrategy = false;
	m_showExtraStrategies = false;
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
			if (key == "light_theme")
				m_lightTheme = ParseBool(value);
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
			else if (key == "discord_share_button")
				m_discordShareButtonEnabled = ParseBool(value);
			else if (key == "discord_download_button")
				m_discordDownloadButtonEnabled = ParseBool(value);
			else if (key == "discord_download_url" && !value.empty())
				m_discordDownloadUrl = value;
			continue;
		}

		if (currentSection == "antizapret")
		{
			if (key == "auto_select_best")
				m_autoSelectBestStrategy = ParseBool(value);
			else if (key == "show_extra_strategies")
				m_showExtraStrategies = ParseBool(value);
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

	SettingsDocument::KeyMap ui;
	ui["light_theme"] = m_lightTheme ? "1" : "0";
	ui["autostart_app"] = m_autostartApp ? "1" : "0";
	ui["autostart_bypass"] = m_autostartBypass ? "1" : "0";
	ui["autostart_telegram"] = m_autostartTelegram ? "1" : "0";
	ui["autostart_vpn"] = m_autostartVpn ? "1" : "0";
	ui["confirm_adult"] = m_confirmAdult ? "1" : "0";
	ui["discord_presence"] = m_discordPresenceEnabled ? "1" : "0";
	ui["discord_share_button"] = m_discordShareButtonEnabled ? "1" : "0";
	ui["discord_download_button"] = m_discordDownloadButtonEnabled ? "1" : "0";
	ui["discord_download_url"] = m_discordDownloadUrl;

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
	SettingsDocument::SetSection(doc, "tg_proxy", tgProxy);
	SettingsDocument::SetSection(doc, "antizapret", antizapret);
	SettingsDocument::SetSection(doc, "ui", ui);
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
	m_lightTheme = value;
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

void AppSettings::SetDiscordPresenceEnabled(bool value)
{
	m_discordPresenceEnabled = value;
	Save();
}

void AppSettings::SetDiscordShareButtonEnabled(bool value)
{
	m_discordShareButtonEnabled = value;
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
		trimmed = "https://github.com/multimaks2/AntiZapret/releases/latest";
	if (trimmed == m_discordDownloadUrl)
		return;
	m_discordDownloadUrl = trimmed;
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
