#include "zapret/zapret_update_apply.h"

#include "app/app_config.h"
#include "app/app_log.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "zapret/zapret_manager.h"
#include "zapret/zapret_paths.h"
#include "zapret/zapret_update_check.h"

#include <Windows.h>
#include <WinInet.h>

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace
{
	namespace fs = std::filesystem;

	bool DownloadFileToPath(const char* url, const fs::path& destPath)
	{
		HINTERNET hInternet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!hInternet)
			return false;

		DWORD timeout = 60000;
		InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));

		HINTERNET hUrl = InternetOpenUrlA(
			hInternet,
			url,
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
			0);
		if (!hUrl)
		{
			InternetCloseHandle(hInternet);
			return false;
		}

		std::ofstream out(destPath, std::ios::binary);
		if (!out)
		{
			InternetCloseHandle(hUrl);
			InternetCloseHandle(hInternet);
			return false;
		}

		char buf[65536];
		DWORD read = 0;
		while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
			out.write(buf, static_cast<std::streamsize>(read));

		out.close();
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInternet);
		return true;
	}

	bool ExtractZipWithPowerShell(const fs::path& zipPath, const fs::path& destDir)
	{
		const std::wstring zipW = zipPath.wstring();
		const std::wstring destW = destDir.wstring();
		std::wstring cmd =
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"$ErrorActionPreference='Continue'; "
			L"Expand-Archive -Path \\\"" + zipW + L"\\\" -DestinationPath \\\"" + destW + L"\\\" -Force\"";

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::wstring fullCmd = L"cmd /c " + cmd;
		std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
		cmdBuf.push_back(L'\0');

		if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
			return false;

		WaitForSingleObject(pi.hProcess, 120000);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return exitCode == 0;
	}

	void CopyDirectoryContents(const fs::path& src, const fs::path& dest)
	{
		if (!fs::exists(src) || !fs::is_directory(src))
			return;

		std::error_code ec;
		fs::create_directories(dest, ec);
		for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
				break;
			try
			{
				const fs::path rel = fs::relative(entry.path(), src);
				const fs::path target = dest / rel;
				if (entry.is_directory())
					fs::create_directories(target, ec);
				else if (entry.is_regular_file())
				{
					fs::create_directories(target.parent_path(), ec);
					fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
				}
			}
			catch (...)
			{
			}
		}
	}

	void StopWinDivertServices()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		const wchar_t* names[] = { L"WinDivert", L"WinDivert14" };
		for (const wchar_t* name : names)
		{
			SC_HANDLE service = OpenServiceW(manager, name, SERVICE_STOP | SERVICE_QUERY_STATUS);
			if (!service)
				continue;
			SERVICE_STATUS status = {};
			ControlService(service, SERVICE_CONTROL_STOP, &status);
			CloseServiceHandle(service);
		}
		CloseServiceHandle(manager);
	}

	void StartWinDivertServices()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		const wchar_t* names[] = { L"WinDivert", L"WinDivert14" };
		for (const wchar_t* name : names)
		{
			SC_HANDLE service = OpenServiceW(manager, name, SERVICE_START | SERVICE_QUERY_STATUS);
			if (service)
			{
				StartServiceW(service, 0, nullptr);
				CloseServiceHandle(service);
			}
		}
		CloseServiceHandle(manager);
	}
}

ZapretUpdateApply& ZapretUpdateApply::Instance()
{
	static ZapretUpdateApply instance;
	return instance;
}

void ZapretUpdateApply::RequestApply(ZapretManager* zapret, TgWsProxyManager* tgProxy)
{
	if (m_applying.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_statusMessage = "Обновление Антизапрета...";
	}

	std::thread([this, zapret, tgProxy]() { RunApply(zapret, tgProxy); }).detach();
}

std::string ZapretUpdateApply::GetStatusMessage() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_statusMessage;
}

void ZapretUpdateApply::RunApply(ZapretManager* zapret, TgWsProxyManager* tgProxy)
{
	const bool wasZapretRunning = zapret && zapret->IsRunning();
	const bool wasSmart = zapret && zapret->IsActiveSmartStrategy();
	const int restoreStrategy = zapret ? zapret->GetActiveStrategyIndex() : -1;
	const auto restoreFilter = zapret
		? zapret->GetActiveGameFilterMode()
		: ZapretStrategies::GameFilterMode::Disabled;
	const bool wasTgRunning = tgProxy && tgProxy->IsRunning();

	if (zapret)
		zapret->RequestStop();
	if (tgProxy && wasTgRunning)
		tgProxy->RequestStop();

	StopWinDivertServices();
	Sleep(1500);

	const fs::path root(ZapretPaths::GetAntiZapretDirectory());
	const fs::path zipPath = root / L"_update.zip";
	const fs::path extractDir = root / L"_update_tmp";

	std::error_code ec;
	fs::create_directories(root, ec);

	AppLog::Instance().Append(LogSource::Zapret, "Скачивание обновления Антизапрета...");
	if (!DownloadFileToPath(AppConfig::kZapretUpdateArchiveUrl, zipPath))
	{
		StartWinDivertServices();
		const std::string msg = "Не удалось скачать обновление.";
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_statusMessage = msg;
		}
		AppLog::Instance().Append(LogSource::Zapret, msg);
		m_applying.store(false);
		return;
	}

	fs::remove_all(extractDir, ec);
	fs::create_directories(extractDir, ec);
	if (!ExtractZipWithPowerShell(zipPath, extractDir))
	{
		StartWinDivertServices();
		const std::string msg = "Не удалось распаковать обновление.";
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_statusMessage = msg;
		}
		AppLog::Instance().Append(LogSource::Zapret, msg);
		m_applying.store(false);
		return;
	}

	const fs::path extractedFolder = extractDir / AppConfig::kZapretRepoExtractedFolderName;
	if (fs::exists(extractedFolder))
		CopyDirectoryContents(extractedFolder, root);

	try
	{
		fs::remove_all(extractDir);
		fs::remove(zipPath);
	}
	catch (...)
	{
	}

	if (zapret)
		zapret->ReloadRuntimeStrategies();

	StartWinDivertServices();

	if (wasZapretRunning && zapret)
	{
		if (wasSmart)
			zapret->RequestStartSmartStrategy(restoreFilter);
		else if (restoreStrategy >= 0)
			zapret->RequestStart(restoreStrategy, restoreFilter);
	}
	if (wasTgRunning && tgProxy)
		tgProxy->RequestStart(false);

	ZapretUpdateCheck::Instance().RequestCheck();

	const std::string doneMsg = "Обновление Антизапрета завершено.";
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_statusMessage = doneMsg;
	}
	AppLog::Instance().Append(LogSource::Zapret, doneMsg);
	m_applying.store(false);
}

void ZapretUpdateApply::RequestApplyTg(TgWsProxyManager* tgProxy)
{
	if (m_applyingTg.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tgStatusMessage = "Обновление TG WS Proxy...";
	}

	std::thread([this, tgProxy]() { RunApplyTg(tgProxy); }).detach();
}

std::string ZapretUpdateApply::GetTgStatusMessage() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_tgStatusMessage;
}

void ZapretUpdateApply::RunApplyTg(TgWsProxyManager* tgProxy)
{
	const bool wasRunning = tgProxy && tgProxy->IsRunning();
	if (wasRunning)
		tgProxy->RequestStop();

	Sleep(800);

	const fs::path root(ZapretPaths::GetTgWsProxyDirectory());
	const fs::path zipPath = root / L"_update.zip";
	const fs::path extractDir = root / L"_update_tmp";

	std::error_code ec;
	fs::create_directories(root, ec);

	std::string tag = ZapretUpdateCheck::Instance().GetTgProxyRemoteTag();
	std::string remoteVersion = ZapretUpdateCheck::Instance().GetTgProxyRemoteVersion();
	if (tag.empty() && !remoteVersion.empty())
		tag = "v" + remoteVersion;

	std::string zipUrl;
	std::string extractedFolderName;
	if (!tag.empty())
	{
		zipUrl = std::string(AppConfig::kTgProxyTagArchiveUrlPrefix) + tag + ".zip";
		// GitHub tags/v1.8.1.zip → tg-ws-proxy-1.8.1
		extractedFolderName = "tg-ws-proxy-" + remoteVersion;
	}
	else
	{
		zipUrl = AppConfig::kTgProxyMainArchiveUrl;
		extractedFolderName = AppConfig::kTgProxyMainExtractedFolderName;
	}

	AppLog::Instance().Append(LogSource::Telegram, "Скачивание обновления TG WS Proxy...");
	if (!DownloadFileToPath(zipUrl.c_str(), zipPath))
	{
		const std::string msg = "Не удалось скачать обновление TG WS Proxy.";
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_tgStatusMessage = msg;
		}
		AppLog::Instance().Append(LogSource::Telegram, msg);
		m_applyingTg.store(false);
		return;
	}

	fs::remove_all(extractDir, ec);
	fs::create_directories(extractDir, ec);
	if (!ExtractZipWithPowerShell(zipPath, extractDir))
	{
		const std::string msg = "Не удалось распаковать обновление TG WS Proxy.";
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_tgStatusMessage = msg;
		}
		AppLog::Instance().Append(LogSource::Telegram, msg);
		m_applyingTg.store(false);
		return;
	}

	fs::path extractedFolder = extractDir / extractedFolderName;
	if (!fs::exists(extractedFolder))
	{
		// zipball may use Flowseal-tg-ws-proxy-<hash> — take first subdirectory.
		for (const auto& entry : fs::directory_iterator(extractDir, ec))
		{
			if (entry.is_directory())
			{
				extractedFolder = entry.path();
				break;
			}
		}
	}

	if (fs::exists(extractedFolder))
		CopyDirectoryContents(extractedFolder, root);

	// Cache version for UI (same as Flowseal .service/version.txt pattern).
	if (!remoteVersion.empty())
	{
		const fs::path serviceDir = root / L".service";
		fs::create_directories(serviceDir, ec);
		std::ofstream versionFile(serviceDir / L"version.txt", std::ios::binary | std::ios::trunc);
		if (versionFile)
			versionFile << remoteVersion << "\n";
	}

	try
	{
		fs::remove_all(extractDir);
		fs::remove(zipPath);
	}
	catch (...)
	{
	}

	if (wasRunning && tgProxy)
		tgProxy->RequestStart(false);

	ZapretUpdateCheck::Instance().RequestCheck();

	const std::string doneMsg = "Обновление TG WS Proxy завершено.";
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tgStatusMessage = doneMsg;
	}
	AppLog::Instance().Append(LogSource::Telegram, doneMsg);
	m_applyingTg.store(false);
}
