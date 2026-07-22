#include "zapret/zapret_update_check.h"

#include "app/app_config.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#pragma comment(lib, "wininet.lib")

namespace
{
	std::string TrimWhitespace(const std::string& value)
	{
		const size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	bool HasDigit(const std::string& value)
	{
		for (unsigned char ch : value)
		{
			if (ch >= '0' && ch <= '9')
				return true;
		}
		return false;
	}

	std::string NormalizeVersion(std::string value)
	{
		value = TrimWhitespace(value);
		if (!value.empty() && (value[0] == 'v' || value[0] == 'V'))
			value.erase(0, 1);
		return value;
	}

	std::string ReadFirstNonEmptyLine(const std::filesystem::path& path)
	{
		std::ifstream file(path);
		if (!file.is_open())
			return {};

		std::string line;
		while (std::getline(file, line))
		{
			const std::string trimmed = TrimWhitespace(line);
			if (!trimmed.empty())
				return trimmed;
		}
		return {};
	}

	std::string HttpGetText(const char* url, DWORD timeoutMs = 15000)
	{
		std::string result;
		HINTERNET hInternet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!hInternet)
			return {};

		InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		// GitHub API requires a User-Agent header.
		const char headers[] = "User-Agent: AntiZapret\r\nAccept: application/vnd.github+json\r\n";
		HINTERNET hUrl = InternetOpenUrlA(
			hInternet,
			url,
			headers,
			static_cast<DWORD>(sizeof(headers) - 1),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
			0);
		if (!hUrl)
		{
			InternetCloseHandle(hInternet);
			return {};
		}

		char buffer[4096];
		DWORD bytesRead = 0;
		while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
			result.append(buffer, buffer + bytesRead);

		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInternet);
		return result;
	}

	std::string ReadZapretLocalVersion()
	{
		const std::filesystem::path root(ZapretPaths::GetAntiZapretDirectory());
		const std::string versionFromFile = ReadFirstNonEmptyLine(root / L".service" / L"version.txt");
		if (!versionFromFile.empty())
			return versionFromFile;

		std::ifstream serviceBat(root / L"service.bat");
		if (serviceBat.is_open())
		{
			std::string line;
			while (std::getline(serviceBat, line))
			{
				const size_t pos = line.find("LOCAL_VERSION=");
				if (pos == std::string::npos)
					continue;
				const size_t start = line.find('"', pos);
				if (start == std::string::npos)
					continue;
				const size_t end = line.find('"', start + 1);
				if (end == std::string::npos)
					continue;
				return line.substr(start + 1, end - start - 1);
			}
		}
		return {};
	}

	std::string FetchZapretRemoteVersion()
	{
		const std::string body = TrimWhitespace(HttpGetText(AppConfig::kZapretVersionUrl));
		return body.empty() ? "Unknown" : body;
	}

	// proxy/__init__.py → __version__ = "1.8.1"
	std::string ReadTgProxyLocalVersion()
	{
		const std::filesystem::path root(ZapretPaths::GetTgWsProxyDirectory());
		const std::string cached = ReadFirstNonEmptyLine(root / L".service" / L"version.txt");
		if (!cached.empty())
			return NormalizeVersion(cached);

		std::ifstream initFile(root / L"proxy" / L"__init__.py");
		if (!initFile.is_open())
			return {};

		std::string line;
		while (std::getline(initFile, line))
		{
			const size_t key = line.find("__version__");
			if (key == std::string::npos)
				continue;
			const size_t eq = line.find('=', key);
			if (eq == std::string::npos)
				continue;
			const size_t q1 = line.find_first_of("\"'", eq + 1);
			if (q1 == std::string::npos)
				continue;
			const size_t q2 = line.find(line[q1], q1 + 1);
			if (q2 == std::string::npos)
				continue;
			return NormalizeVersion(line.substr(q1 + 1, q2 - q1 - 1));
		}
		return {};
	}

	// GitHub Releases API → tag_name "v1.8.1"
	bool FetchTgProxyRemoteRelease(std::string& outVersion, std::string& outTag)
	{
		outVersion.clear();
		outTag.clear();

		const std::string body = HttpGetText(AppConfig::kTgProxyReleasesLatestApiUrl);
		if (body.empty())
			return false;

		const std::string key = "\"tag_name\"";
		const size_t keyPos = body.find(key);
		if (keyPos == std::string::npos)
			return false;

		const size_t colon = body.find(':', keyPos + key.size());
		if (colon == std::string::npos)
			return false;
		const size_t q1 = body.find('"', colon + 1);
		if (q1 == std::string::npos)
			return false;
		const size_t q2 = body.find('"', q1 + 1);
		if (q2 == std::string::npos)
			return false;

		outTag = body.substr(q1 + 1, q2 - q1 - 1);
		outVersion = NormalizeVersion(outTag);
		return HasDigit(outVersion);
	}

	bool TgProxyFilesPresent(const std::filesystem::path& root)
	{
		std::error_code ec;
		return std::filesystem::exists(root / L"TgWsProxy_windows.exe", ec)
			|| std::filesystem::exists(root / L"proxy" / L"tg_ws_proxy.py", ec)
			|| std::filesystem::exists(root / L"proxy" / L"__init__.py", ec);
	}
}  // namespace

ZapretUpdateCheck& ZapretUpdateCheck::Instance()
{
	static ZapretUpdateCheck instance;
	return instance;
}

void ZapretUpdateCheck::SetOnZapretUpdateAvailable(std::function<void()> callback)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_onZapretUpdateAvailable = std::move(callback);
}

void ZapretUpdateCheck::SetOnTgUpdateAvailable(std::function<void()> callback)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_onTgUpdateAvailable = std::move(callback);
}

void ZapretUpdateCheck::SeedLocalVersions()
{
	const std::string zapretLocal = ReadZapretLocalVersion();
	const std::string tgLocal = ReadTgProxyLocalVersion();

	std::lock_guard<std::mutex> lock(m_mutex);
	m_zapretLocalVersion = zapretLocal;
	m_zapretStatus = HasDigit(zapretLocal)
		? ComponentUpdateStatus::Checking
		: ComponentUpdateStatus::Unknown;
	m_tgProxyLocalVersion = tgLocal;
	m_tgProxyStatus = HasDigit(tgLocal) || TgProxyFilesPresent(ZapretPaths::GetTgWsProxyDirectory())
		? ComponentUpdateStatus::Checking
		: ComponentUpdateStatus::Unknown;
}

void ZapretUpdateCheck::NotifyZapretUpdateIfNeeded(ComponentUpdateStatus status)
{
	if (status != ComponentUpdateStatus::UpdateAvailable)
		return;
	if (m_zapretAutoUpdateNotified.exchange(true))
		return;

	std::function<void()> callback;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		callback = m_onZapretUpdateAvailable;
	}
	if (callback)
		callback();
}

void ZapretUpdateCheck::NotifyTgUpdateIfNeeded(ComponentUpdateStatus status)
{
	if (status != ComponentUpdateStatus::UpdateAvailable)
		return;
	if (m_tgAutoUpdateNotified.exchange(true))
		return;

	std::function<void()> callback;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		callback = m_onTgUpdateAvailable;
	}
	if (callback)
		callback();
}

void ZapretUpdateCheck::StartBackgroundCheck()
{
	if (m_checkStarted.exchange(true))
		return;

	SeedLocalVersions();
	std::thread([this]()
	{
		RunCheck();
		NotifyZapretUpdateIfNeeded(GetZapretStatus());
		NotifyTgUpdateIfNeeded(GetTgProxyStatus());
	}).detach();
}

void ZapretUpdateCheck::RequestCheck()
{
	m_checkStarted.store(true);
	m_zapretAutoUpdateNotified.store(false);
	m_tgAutoUpdateNotified.store(false);
	SeedLocalVersions();
	std::thread([this]()
	{
		RunCheck();
		NotifyZapretUpdateIfNeeded(GetZapretStatus());
		NotifyTgUpdateIfNeeded(GetTgProxyStatus());
	}).detach();
}

void ZapretUpdateCheck::RunCheck()
{
	if (m_checkInProgress.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_zapretStatus = ComponentUpdateStatus::Checking;
		m_tgProxyStatus = ComponentUpdateStatus::Checking;
	}

	const std::string zapretLocal = ReadZapretLocalVersion();
	const std::string zapretRemote = FetchZapretRemoteVersion();

	ComponentUpdateStatus zapretStatus;
	if (!HasDigit(zapretLocal))
		zapretStatus = ComponentUpdateStatus::Unknown;
	else if (zapretRemote == "Unknown" || zapretRemote.empty())
		zapretStatus = ComponentUpdateStatus::UpToDate;
	else if (NormalizeVersion(zapretLocal) == NormalizeVersion(zapretRemote))
		zapretStatus = ComponentUpdateStatus::UpToDate;
	else
		zapretStatus = ComponentUpdateStatus::UpdateAvailable;

	const std::string tgLocal = ReadTgProxyLocalVersion();
	std::string tgRemote;
	std::string tgTag;
	const bool tgRemoteOk = FetchTgProxyRemoteRelease(tgRemote, tgTag);

	ComponentUpdateStatus tgStatus;
	if (!HasDigit(tgLocal))
	{
		tgStatus = TgProxyFilesPresent(ZapretPaths::GetTgWsProxyDirectory())
			? (tgRemoteOk ? ComponentUpdateStatus::UpdateAvailable : ComponentUpdateStatus::Unknown)
			: ComponentUpdateStatus::Error;
	}
	else if (!tgRemoteOk)
		tgStatus = ComponentUpdateStatus::UpToDate;
	else if (NormalizeVersion(tgLocal) == NormalizeVersion(tgRemote))
		tgStatus = ComponentUpdateStatus::UpToDate;
	else
		tgStatus = ComponentUpdateStatus::UpdateAvailable;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_zapretStatus = zapretStatus;
		m_zapretLocalVersion = zapretLocal;
		m_zapretRemoteVersion = zapretRemote;
		m_tgProxyStatus = tgStatus;
		m_tgProxyLocalVersion = tgLocal;
		m_tgProxyRemoteVersion = tgRemote;
		m_tgProxyRemoteTag = tgTag;
	}

	m_checkInProgress.store(false);
}

ComponentUpdateStatus ZapretUpdateCheck::GetZapretStatus() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_zapretStatus;
}

std::string ZapretUpdateCheck::GetZapretLocalVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_zapretLocalVersion;
}

std::string ZapretUpdateCheck::GetZapretRemoteVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_zapretRemoteVersion;
}

ComponentUpdateStatus ZapretUpdateCheck::GetTgProxyStatus() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_tgProxyStatus;
}

std::string ZapretUpdateCheck::GetTgProxyLocalVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_tgProxyLocalVersion;
}

std::string ZapretUpdateCheck::GetTgProxyRemoteVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_tgProxyRemoteVersion;
}

std::string ZapretUpdateCheck::GetTgProxyRemoteTag() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_tgProxyRemoteTag;
}

const char* ZapretUpdateCheck::GetStatusMessage(ComponentUpdateStatus status)
{
	switch (status)
	{
	case ComponentUpdateStatus::Checking:
		return "Проверка версии...";
	case ComponentUpdateStatus::UpToDate:
		return "Актуально";
	case ComponentUpdateStatus::UpdateAvailable:
		return "Доступно обновление";
	case ComponentUpdateStatus::Error:
		return "Ошибка проверки версии";
	case ComponentUpdateStatus::Unknown:
	default:
		return "Версия неизвестна";
	}
}
