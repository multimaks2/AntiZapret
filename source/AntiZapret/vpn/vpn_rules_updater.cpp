#include "vpn/vpn_rules_updater.h"

#include "app/app_log.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace
{
	constexpr const char* kSrsUrlTemplate =
		"https://raw.githubusercontent.com/runetfreedom/russia-v2ray-rules-dat/release/sing-box/rule-set-%s/%s-%s.srs";

	constexpr const char* kGeoipMetadbUrl =
		"https://github.com/MetaCubeX/meta-rules-dat/releases/download/latest/geoip.metadb";

	constexpr DWORD kDownloadTimeoutMs = 60000;

	const char* kGeoSiteRuleNames[] = {
		"category-ads-all",
		"private",
		"ru-blocked",
		"google",
		"ru-available-only-inside",
	};

	const char* kGeoIpRuleNames[] = {
		"private",
		"ru-blocked",
		"ru",
		"google",
	};

	std::atomic<bool> g_updateStarted { false };
	std::atomic<bool> g_updateInProgress { false };
	std::mutex g_statusMutex;
	std::string g_statusMessage = "Правила маршрутизации не проверялись.";

	void SetStatus(const std::string& message)
	{
		std::lock_guard<std::mutex> lock(g_statusMutex);
		g_statusMessage = message;
		if (!message.empty())
			AppLog::Instance().Append(LogSource::VpnRouting, message);
	}

	std::string BuildSrsUrl(const char* type, const char* name)
	{
		char buffer[512] = {};
		snprintf(buffer, sizeof buffer, kSrsUrlTemplate, type, type, name);
		return buffer;
	}

	bool DownloadToFile(const std::string& url, const std::filesystem::path& targetPath, std::string& outError)
	{
		outError.clear();

		std::error_code ec;
		std::filesystem::create_directories(targetPath.parent_path(), ec);

		const std::filesystem::path tempPath =
			targetPath.parent_path() / (targetPath.filename().wstring() + L".download");

		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "InternetOpen failed.";
			return false;
		}

		DWORD timeoutMs = kDownloadTimeoutMs;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
			0);

		if (!request)
		{
			InternetCloseHandle(internet);
			outError = "Не удалось скачать: " + url;
			return false;
		}

		std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
		if (!output)
		{
			InternetCloseHandle(request);
			InternetCloseHandle(internet);
			outError = "Не удалось создать временный файл.";
			return false;
		}

		char buffer[8192];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			output.write(buffer, read);

		output.close();
		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (!output.good())
		{
			std::filesystem::remove(tempPath, ec);
			outError = "Ошибка записи файла.";
			return false;
		}

		const auto fileSize = std::filesystem::file_size(tempPath, ec);
		if (ec || fileSize == 0)
		{
			std::filesystem::remove(tempPath, ec);
			outError = "Пустой ответ.";
			return false;
		}

		std::filesystem::rename(tempPath, targetPath, ec);
		if (ec)
		{
			std::filesystem::copy_file(tempPath, targetPath, std::filesystem::copy_options::overwrite_existing, ec);
			std::filesystem::remove(tempPath, ec);
		}

		return std::filesystem::exists(targetPath);
	}

	void WriteLastUpdateTimestamp(const std::filesystem::path& cacheFile)
	{
		std::error_code ec;
		std::filesystem::create_directories(cacheFile.parent_path(), ec);

		std::ofstream output(cacheFile, std::ios::binary | std::ios::trunc);
		if (!output)
			return;

		const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		output << "updated=" << now << "\r\n";
	}

	void CleanupGeneratedFiles(const std::filesystem::path& cacheDirectory, const std::filesystem::path& srssDirectory)
	{
		static const wchar_t* kStaleCacheFiles[] = {
			L"GeoIP.dat",
			L"Country.mmdb",
			L"config-test.yaml",
			L"test-all.yaml",
			L"test-noads.yaml",
			L"test-proxy.yaml",
			L"test-trojan.yaml",
			L"test-tun.yaml",
			L"test-tun2.yaml",
			L"test-vmess.yaml",
			L"test-vmess2.yaml",
			L"test-yt.yaml",
		};

		std::error_code ec;
		if (std::filesystem::exists(srssDirectory, ec))
		{
			for (const auto& entry : std::filesystem::directory_iterator(srssDirectory, ec))
			{
				if (!entry.is_regular_file())
					continue;

				const std::wstring fileName = entry.path().filename().wstring();
				if (fileName.size() > 9 && fileName.substr(fileName.size() - 9) == L".download")
				{
					std::filesystem::remove(entry.path(), ec);
					continue;
				}

				const bool isGeosite = fileName.size() > 11 && fileName.rfind(L"geosite-", 0) == 0 && fileName.substr(fileName.size() - 4) == L".srs";
				const bool isGeoip = fileName.size() > 9 && fileName.rfind(L"geoip-", 0) == 0 && fileName.substr(fileName.size() - 4) == L".srs";
				if (!isGeosite && !isGeoip)
					std::filesystem::remove(entry.path(), ec);
			}
		}

		for (const wchar_t* name : kStaleCacheFiles)
			std::filesystem::remove(cacheDirectory / name, ec);

		// Leftovers from older layouts under vpn/
		const std::filesystem::path vpnDirectory = std::filesystem::path(ZapretPaths::GetVpnDirectory());
		for (const wchar_t* name : kStaleCacheFiles)
			std::filesystem::remove(vpnDirectory / name, ec);
	}

	void RunUpdate()
	{
		g_updateInProgress = true;
		SetStatus("Обновление правил маршрутизации...");

		const std::filesystem::path cacheDirectory = std::filesystem::path(ZapretPaths::GetCacheDirectory());
		const std::filesystem::path srssDirectory = cacheDirectory / L"srss";
		std::error_code ec;
		std::filesystem::create_directories(srssDirectory, ec);

		int downloaded = 0;
		int failed = 0;
		int kept = 0;

		auto refreshFile = [&](const std::string& url, const std::filesystem::path& targetPath, bool optional) {
			std::string error;
			if (DownloadToFile(url, targetPath, error))
			{
				++downloaded;
				return;
			}

			if (std::filesystem::exists(targetPath))
				++kept;
			else if (optional)
				++kept;
			else
				++failed;
		};

		for (const char* name : kGeoSiteRuleNames)
		{
			const std::string fileName = std::string("geosite-") + name + ".srs";
			const bool optional = std::string(name) == "ru-available-only-inside";
			refreshFile(
				BuildSrsUrl("geosite", name),
				srssDirectory / fileName,
				optional);
		}

		for (const char* name : kGeoIpRuleNames)
		{
			const std::string fileName = std::string("geoip-") + name + ".srs";
			refreshFile(
				BuildSrsUrl("geoip", name),
				srssDirectory / fileName,
				false);
		}

		refreshFile(kGeoipMetadbUrl, cacheDirectory / L"geoip.metadb", true);

		CleanupGeneratedFiles(cacheDirectory, srssDirectory);

		WriteLastUpdateTimestamp(cacheDirectory / L"rules-update.ini");

		char summary[256] = {};
		snprintf(
			summary,
			sizeof summary,
			"Правила обновлены: скачано %d, оставлено прежними %d, ошибок %d.",
			downloaded,
			kept,
			failed);
		SetStatus(summary);
		g_updateInProgress = false;
	}
	bool GeositeFileNeedsDownload(const std::filesystem::path& targetPath)
	{
		std::error_code ec;
		if (!std::filesystem::exists(targetPath, ec))
			return true;

		const auto size = std::filesystem::file_size(targetPath, ec);
		return ec || size < 64;
	}
}

void VpnRulesUpdater::EnsureGeositeFiles(const std::vector<std::string>& geositeNames)
{
	if (geositeNames.empty())
		return;

	const std::filesystem::path cacheDirectory = std::filesystem::path(ZapretPaths::GetCacheDirectory());
	const std::filesystem::path srssDirectory = cacheDirectory / L"srss";
	std::error_code ec;
	std::filesystem::create_directories(srssDirectory, ec);

	for (const std::string& name : geositeNames)
	{
		if (name.empty())
			continue;

		const std::string fileName = "geosite-" + name + ".srs";
		const std::filesystem::path targetPath = srssDirectory / fileName;
		if (!GeositeFileNeedsDownload(targetPath))
			continue;

		std::string error;
		DownloadToFile(BuildSrsUrl("geosite", name.c_str()), targetPath, error);
	}
}

void VpnRulesUpdater::StartBackgroundUpdate()
{
	if (g_updateStarted.exchange(true))
		return;

	std::thread([]() { RunUpdate(); }).detach();
}

bool VpnRulesUpdater::IsUpdateInProgress()
{
	return g_updateInProgress.load();
}

bool VpnRulesUpdater::AreCoreRulesReady()
{
	const std::filesystem::path srss =
		std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"srss";

	// Required for all RUv1 presets (always referenced in rule-providers).
	static const wchar_t* kRequired[] = {
		L"geosite-ru-blocked.srs",
		L"geoip-ru-blocked.srs",
		L"geosite-private.srs",
		L"geoip-private.srs",
		L"geosite-category-ads-all.srs",
		L"geoip-ru.srs",
	};

	std::error_code ec;
	for (const wchar_t* name : kRequired)
	{
		const std::filesystem::path path = srss / name;
		if (!std::filesystem::exists(path, ec))
			return false;
		const auto size = std::filesystem::file_size(path, ec);
		if (ec || size < 64)
			return false;
	}
	return true;
}

std::string VpnRulesUpdater::GetStatusMessage()
{
	std::lock_guard<std::mutex> lock(g_statusMutex);
	return g_statusMessage;
}
