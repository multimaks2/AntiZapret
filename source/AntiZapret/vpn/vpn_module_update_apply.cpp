#include "vpn/vpn_module_update_apply.h"

#include "app/app_config.h"
#include "app/app_log.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_module_update_check.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace
{
	namespace fs = std::filesystem;

	void SetInternetTimeouts(HINTERNET handle, DWORD timeoutMs)
	{
		InternetSetOptionA(handle, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(handle, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(handle, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
	}

	bool DownloadFileWinInet(const char* url, const fs::path& destPath, std::string& outError)
	{
		outError.clear();
		HINTERNET hInternet = InternetOpenA(
			"AntiZapret/1.0",
			INTERNET_OPEN_TYPE_PRECONFIG,
			nullptr,
			nullptr,
			0);
		if (!hInternet)
		{
			outError = "InternetOpen failed (" + std::to_string(GetLastError()) + ")";
			return false;
		}

		constexpr DWORD kTimeoutMs = 300000; // large mihomo zips (~17MB) need >20s on slow links
		SetInternetTimeouts(hInternet, kTimeoutMs);

		const char headers[] =
			"User-Agent: AntiZapret/1.0\r\n"
			"Accept: application/octet-stream,*/*\r\n";
		HINTERNET hUrl = InternetOpenUrlA(
			hInternet,
			url,
			headers,
			static_cast<DWORD>(sizeof(headers) - 1),
			INTERNET_FLAG_RELOAD
				| INTERNET_FLAG_NO_CACHE_WRITE
				| INTERNET_FLAG_KEEP_CONNECTION
				| INTERNET_FLAG_SECURE,
			0);
		if (!hUrl)
		{
			outError = "InternetOpenUrl failed (" + std::to_string(GetLastError()) + ")";
			InternetCloseHandle(hInternet);
			return false;
		}
		SetInternetTimeouts(hUrl, kTimeoutMs);

		DWORD status = 0;
		DWORD statusSize = sizeof(status);
		if (HttpQueryInfoA(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, nullptr))
		{
			if (status < 200 || status >= 300)
			{
				outError = "HTTP " + std::to_string(status);
				InternetCloseHandle(hUrl);
				InternetCloseHandle(hInternet);
				return false;
			}
		}

		std::error_code ec;
		fs::create_directories(destPath.parent_path(), ec);
		const fs::path tempPath = destPath.wstring() + L".part";
		fs::remove(tempPath, ec);

		std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			outError = "cannot create temp file";
			InternetCloseHandle(hUrl);
			InternetCloseHandle(hInternet);
			return false;
		}

		char buf[65536];
		DWORD read = 0;
		uint64_t total = 0;
		while (InternetReadFile(hUrl, buf, sizeof(buf), &read))
		{
			if (read == 0)
				break;
			out.write(buf, static_cast<std::streamsize>(read));
			if (!out)
			{
				outError = "write failed";
				out.close();
				InternetCloseHandle(hUrl);
				InternetCloseHandle(hInternet);
				fs::remove(tempPath, ec);
				return false;
			}
			total += read;
		}
		const DWORD readErr = GetLastError();
		out.close();
		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInternet);

		if (total == 0)
		{
			outError = readErr ? ("read failed (" + std::to_string(readErr) + ")") : "empty download";
			fs::remove(tempPath, ec);
			return false;
		}

		fs::remove(destPath, ec);
		fs::rename(tempPath, destPath, ec);
		if (ec)
		{
			fs::copy_file(tempPath, destPath, fs::copy_options::overwrite_existing, ec);
			fs::remove(tempPath, ec);
		}
		return fs::exists(destPath) && fs::file_size(destPath, ec) > 1024;
	}

	bool DownloadFilePowerShell(const char* url, const fs::path& destPath, std::string& outError)
	{
		outError.clear();
		std::error_code ec;
		fs::create_directories(destPath.parent_path(), ec);
		fs::remove(destPath, ec);

		const std::wstring urlW(url, url + strlen(url));
		const std::wstring destW = destPath.wstring();
		std::wstring cmd =
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
			L"Invoke-WebRequest -Uri \\\"" + urlW + L"\\\" -OutFile \\\"" + destW
			+ L"\\\" -UserAgent 'AntiZapret/1.0' -TimeoutSec 300\"";

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::wstring fullCmd = L"cmd /c " + cmd;
		std::vector<wchar_t> cmdBuf(fullCmd.begin(), fullCmd.end());
		cmdBuf.push_back(L'\0');

		if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			outError = "PowerShell spawn failed (" + std::to_string(GetLastError()) + ")";
			return false;
		}

		WaitForSingleObject(pi.hProcess, 320000);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (exitCode != 0 || !fs::exists(destPath) || fs::file_size(destPath, ec) < 1024)
		{
			outError = "PowerShell download failed (exit " + std::to_string(exitCode) + ")";
			return false;
		}
		return true;
	}

	bool DownloadFileToPath(const char* url, const fs::path& destPath, std::string& outError)
	{
		if (DownloadFileWinInet(url, destPath, outError))
			return true;

		const std::string winInetError = outError;
		if (DownloadFilePowerShell(url, destPath, outError))
			return true;

		outError = winInetError + "; fallback: " + outError;
		return false;
	}

	bool DownloadFileToPath(const char* url, const fs::path& destPath)
	{
		std::string error;
		return DownloadFileToPath(url, destPath, error);
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

		WaitForSingleObject(pi.hProcess, 180000);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return exitCode == 0;
	}

	fs::path FindFileRecursive(const fs::path& root, const std::wstring& fileName)
	{
		std::error_code ec;
		if (!fs::exists(root, ec))
			return {};

		for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
				break;
			try
			{
				if (entry.is_regular_file() && entry.path().filename() == fileName)
					return entry.path();
			}
			catch (...)
			{
			}
		}
		return {};
	}

	// Prefer amd64/wintun.dll inside the official zip layout.
	fs::path FindWintunDll(const fs::path& extractRoot)
	{
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(extractRoot, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
				break;
			try
			{
				if (!entry.is_regular_file())
					continue;
				if (entry.path().filename() != L"wintun.dll")
					continue;
				const std::wstring pathLower = entry.path().wstring();
				if (pathLower.find(L"amd64") != std::wstring::npos
					|| pathLower.find(L"AMD64") != std::wstring::npos)
					return entry.path();
			}
			catch (...)
			{
			}
		}
		return FindFileRecursive(extractRoot, L"wintun.dll");
	}

	fs::path FindMihomoExe(const fs::path& extractRoot)
	{
		if (const fs::path direct = FindFileRecursive(extractRoot, L"mihomo.exe"); !direct.empty())
			return direct;

		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(extractRoot, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
				break;
			try
			{
				if (!entry.is_regular_file())
					continue;
				const std::wstring name = entry.path().filename().wstring();
				if (name.find(L"mihomo") != std::wstring::npos
					&& (name.find(L".exe") != std::wstring::npos || entry.path().extension().empty()))
					return entry.path();
			}
			catch (...)
			{
			}
		}
		return {};
	}

	void WaitVpnStopped(VpnManager* vpn, int timeoutMs)
	{
		if (!vpn)
			return;
		const int step = 200;
		for (int elapsed = 0; elapsed < timeoutMs; elapsed += step)
		{
			if (!vpn->IsRunning() && vpn->GetRunStatus() == VpnRunStatus::Stopped && !vpn->IsOperationInFlight())
				return;
			Sleep(step);
		}
	}
}

VpnModuleUpdateApply& VpnModuleUpdateApply::Instance()
{
	static VpnModuleUpdateApply instance;
	return instance;
}

void VpnModuleUpdateApply::RequestApplyMihomo(VpnManager* vpn)
{
	if (m_applyingMihomo.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = "Обновление mihomo...";
	}
	std::thread([this, vpn]() { RunApplyMihomo(vpn); }).detach();
}

void VpnModuleUpdateApply::RequestApplyWintun(VpnManager* vpn)
{
	if (m_applyingWintun.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = "Обновление wintun...";
	}
	std::thread([this, vpn]() { RunApplyWintun(vpn); }).detach();
}

std::string VpnModuleUpdateApply::GetMihomoStatusMessage() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mihomoStatusMessage;
}

std::string VpnModuleUpdateApply::GetWintunStatusMessage() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_wintunStatusMessage;
}

void VpnModuleUpdateApply::RunApplyMihomo(VpnManager* vpn)
{
	const bool wasRunning = vpn && vpn->IsRunning();
	if (wasRunning)
	{
		vpn->RequestStop();
		WaitVpnStopped(vpn, 15000);
	}

	const fs::path root(ZapretPaths::GetVpnDirectory());
	const fs::path zipPath = root / L"_mihomo_update.zip";
	const fs::path extractDir = root / L"_mihomo_update_tmp";
	std::error_code ec;
	fs::create_directories(root, ec);

	std::string tag = VpnModuleUpdateCheck::Instance().GetMihomoRemoteTag();
	std::string remoteVersion = VpnModuleUpdateCheck::Instance().GetMihomoRemoteVersion();
	if (tag.empty() && !remoteVersion.empty())
		tag = "v" + remoteVersion;

	if (tag.empty())
	{
		const std::string msg = "Неизвестна удалённая версия mihomo.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingMihomo.store(false);
		return;
	}

	char url[512] = {};
	snprintf(url, sizeof url, AppConfig::kMihomoWin64ZipUrlTemplate, tag.c_str(), tag.c_str());

	AppLog::Instance().Append(LogSource::VpnRouting, std::string("Скачивание mihomo ") + tag + "...");
	std::string downloadError;
	if (!DownloadFileToPath(url, zipPath, downloadError))
	{
		const std::string msg = "Не удалось скачать mihomo" + (downloadError.empty() ? "." : (": " + downloadError));
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingMihomo.store(false);
		return;
	}

	fs::remove_all(extractDir, ec);
	fs::create_directories(extractDir, ec);
	if (!ExtractZipWithPowerShell(zipPath, extractDir))
	{
		const std::string msg = "Не удалось распаковать mihomo.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingMihomo.store(false);
		return;
	}

	const fs::path found = FindMihomoExe(extractDir);
	if (found.empty())
	{
		const std::string msg = "В архиве mihomo не найден исполняемый файл.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingMihomo.store(false);
		return;
	}

	const fs::path target = root / L"mihomo.exe";
	fs::copy_file(found, target, fs::copy_options::overwrite_existing, ec);
	if (ec)
	{
		const std::string msg = "Не удалось заменить mihomo.exe.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingMihomo.store(false);
		return;
	}

	try
	{
		fs::remove_all(extractDir);
		fs::remove(zipPath);
	}
	catch (...)
	{
	}

	VpnModuleUpdateCheck::Instance().RequestCheck();

	const std::string doneMsg = "Обновление mihomo завершено.";
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatusMessage = doneMsg;
	}
	AppLog::Instance().Append(LogSource::VpnRouting, doneMsg);
	m_applyingMihomo.store(false);
	(void)wasRunning; // SyncVpnRuntime restart if toggle still on
}

void VpnModuleUpdateApply::RunApplyWintun(VpnManager* vpn)
{
	const bool wasRunning = vpn && vpn->IsRunning();
	if (wasRunning)
	{
		vpn->RequestStop();
		WaitVpnStopped(vpn, 15000);
	}

	const fs::path root(ZapretPaths::GetVpnDirectory());
	const fs::path zipPath = root / L"_wintun_update.zip";
	const fs::path extractDir = root / L"_wintun_update_tmp";
	std::error_code ec;
	fs::create_directories(root, ec);

	std::string remoteVersion = VpnModuleUpdateCheck::Instance().GetWintunRemoteVersion();
	if (remoteVersion.empty())
	{
		const std::string msg = "Неизвестна удалённая версия wintun.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingWintun.store(false);
		return;
	}

	char url[256] = {};
	snprintf(url, sizeof url, AppConfig::kWintunZipUrlTemplate, remoteVersion.c_str());

	AppLog::Instance().Append(LogSource::VpnRouting, std::string("Скачивание wintun ") + remoteVersion + "...");
	std::string downloadError;
	if (!DownloadFileToPath(url, zipPath, downloadError))
	{
		const std::string msg = "Не удалось скачать wintun" + (downloadError.empty() ? "." : (": " + downloadError));
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingWintun.store(false);
		return;
	}

	fs::remove_all(extractDir, ec);
	fs::create_directories(extractDir, ec);
	if (!ExtractZipWithPowerShell(zipPath, extractDir))
	{
		const std::string msg = "Не удалось распаковать wintun.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingWintun.store(false);
		return;
	}

	const fs::path found = FindWintunDll(extractDir);
	if (found.empty())
	{
		const std::string msg = "В архиве wintun не найден wintun.dll.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingWintun.store(false);
		return;
	}

	const fs::path target = root / L"wintun.dll";
	fs::copy_file(found, target, fs::copy_options::overwrite_existing, ec);
	if (ec)
	{
		const std::string msg = "Не удалось заменить wintun.dll.";
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = msg;
		AppLog::Instance().Append(LogSource::VpnRouting, msg);
		m_applyingWintun.store(false);
		return;
	}

	try
	{
		fs::remove_all(extractDir);
		fs::remove(zipPath);
	}
	catch (...)
	{
	}

	VpnModuleUpdateCheck::Instance().RequestCheck();

	const std::string doneMsg = "Обновление wintun завершено.";
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wintunStatusMessage = doneMsg;
	}
	AppLog::Instance().Append(LogSource::VpnRouting, doneMsg);
	m_applyingWintun.store(false);
	(void)wasRunning;
}
