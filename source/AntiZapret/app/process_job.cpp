#include "app/process_job.h"

#include "app/app_log.h"
#include "zapret/zapret_paths.h"

#include <TlHelp32.h>
#include <WinInet.h>

#include <mutex>
#include <string>

#pragma comment(lib, "wininet.lib")

namespace ProcessJob
{
namespace
{
	std::mutex g_mutex;
	HANDLE g_job = nullptr;
	bool g_initAttempted = false;

	std::wstring NormalizePath(std::wstring path)
	{
		for (wchar_t& ch : path)
		{
			if (ch == L'/')
				ch = L'\\';
		}
		while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
			path.pop_back();
		return path;
	}

	bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
	{
		if (a.size() != b.size())
			return false;
		return _wcsicmp(a.c_str(), b.c_str()) == 0;
	}

	bool PathIsUnderOrEqual(const std::wstring& fullPath, const std::wstring& root)
	{
		const std::wstring path = NormalizePath(fullPath);
		const std::wstring base = NormalizePath(root);
		if (base.empty() || path.empty())
			return false;
		if (EqualsIgnoreCase(path, base))
			return true;
		if (path.size() <= base.size())
			return false;
		if (_wcsnicmp(path.c_str(), base.c_str(), base.size()) != 0)
			return false;
		return path[base.size()] == L'\\';
	}

	std::wstring QueryProcessImagePath(HANDLE process)
	{
		wchar_t buffer[MAX_PATH * 4] = {};
		DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
		if (!QueryFullProcessImageNameW(process, 0, buffer, &size) || size == 0)
			return {};
		return NormalizePath(buffer);
	}

	std::wstring FileNameOf(const std::wstring& path)
	{
		const size_t slash = path.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return path;
		return path.substr(slash + 1);
	}

	bool TerminatePid(DWORD pid)
	{
		HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
		if (!process)
			return false;
		const BOOL ok = TerminateProcess(process, 1);
		if (ok)
			WaitForSingleObject(process, 3000);
		CloseHandle(process);
		return ok != FALSE;
	}

	void ClearStaleLocalSystemProxy()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(
				HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
				0,
				KEY_QUERY_VALUE | KEY_SET_VALUE,
				&key) != ERROR_SUCCESS)
			return;

		DWORD proxyEnable = 0;
		DWORD type = REG_DWORD;
		DWORD size = sizeof(proxyEnable);
		if (RegQueryValueExW(key, L"ProxyEnable", nullptr, &type, reinterpret_cast<LPBYTE>(&proxyEnable), &size) != ERROR_SUCCESS
			|| proxyEnable == 0)
		{
			RegCloseKey(key);
			return;
		}

		wchar_t proxyServer[512] = {};
		DWORD proxyServerSize = sizeof(proxyServer);
		type = REG_SZ;
		if (RegQueryValueExW(key, L"ProxyServer", nullptr, &type, reinterpret_cast<LPBYTE>(proxyServer), &proxyServerSize) != ERROR_SUCCESS)
		{
			RegCloseKey(key);
			return;
		}

		const bool isLocal = (_wcsnicmp(proxyServer, L"127.0.0.1:", 10) == 0)
			|| (_wcsnicmp(proxyServer, L"localhost:", 10) == 0);
		if (!isLocal)
		{
			RegCloseKey(key);
			return;
		}

		proxyEnable = 0;
		RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&proxyEnable), sizeof(proxyEnable));
		RegDeleteValueW(key, L"ProxyServer");
		RegCloseKey(key);

		InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
		InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
		AppLog::Instance().Append(LogSource::VpnRouting, "Сброшен залипший системный proxy после orphan cleanup.");
	}
}

bool EnsureInitialized()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_job)
		return true;
	if (g_initAttempted)
		return false;
	g_initAttempted = true;

	g_job = CreateJobObjectW(nullptr, nullptr);
	if (!g_job)
	{
		AppLog::Instance().Append(
			LogSource::Zapret,
			std::string("ProcessJob: CreateJobObject failed (") + std::to_string(GetLastError()) + ")");
		return false;
	}

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
	info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof(info)))
	{
		AppLog::Instance().Append(
			LogSource::Zapret,
			std::string("ProcessJob: SetInformationJobObject failed (") + std::to_string(GetLastError()) + ")");
		CloseHandle(g_job);
		g_job = nullptr;
		return false;
	}

	return true;
}

BOOL CreateInJob(
	LPCWSTR applicationName,
	LPWSTR commandLine,
	LPSECURITY_ATTRIBUTES processAttributes,
	LPSECURITY_ATTRIBUTES threadAttributes,
	BOOL inheritHandles,
	DWORD creationFlags,
	LPVOID environment,
	LPCWSTR currentDirectory,
	LPSTARTUPINFOW startupInfo,
	LPPROCESS_INFORMATION processInformation)
{
	EnsureInitialized();

	HANDLE job = nullptr;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		job = g_job;
	}

	const DWORD flags = creationFlags | (job ? CREATE_SUSPENDED : 0);
	if (!CreateProcessW(
			applicationName,
			commandLine,
			processAttributes,
			threadAttributes,
			inheritHandles,
			flags,
			environment,
			currentDirectory,
			startupInfo,
			processInformation))
		return FALSE;

	if (job)
	{
		if (!AssignProcessToJobObject(job, processInformation->hProcess))
		{
			static bool loggedAssignFail = false;
			if (!loggedAssignFail)
			{
				loggedAssignFail = true;
				AppLog::Instance().Append(
					LogSource::Zapret,
					std::string("ProcessJob: AssignProcessToJobObject failed (")
						+ std::to_string(GetLastError()) + "), процесс продолжит без job");
			}
		}
		ResumeThread(processInformation->hThread);
	}

	return TRUE;
}

void CleanupOrphansAtStartup()
{
	EnsureInitialized();

	const std::wstring vpnDir = ZapretPaths::GetVpnDirectory();
	const std::wstring antiZapretDir = ZapretPaths::GetAntiZapretDirectory();
	const std::wstring tgDir = ZapretPaths::GetTgWsProxyDirectory();
	const std::wstring mihomoExpected = NormalizePath(vpnDir + L"\\mihomo.exe");
	const std::wstring winwsExpected = NormalizePath(ZapretPaths::GetBinDirectory() + L"\\winws.exe");
	const std::wstring tgExeExpected = NormalizePath(tgDir + L"\\TgWsProxy_windows.exe");

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	const DWORD selfPid = GetCurrentProcessId();
	int killedMihomo = 0;
	int killedWinws = 0;
	int killedTg = 0;

	PROCESSENTRY32W entry = { sizeof(entry) };
	if (Process32FirstW(snapshot, &entry))
	{
		do
		{
			if (entry.th32ProcessID == 0 || entry.th32ProcessID == selfPid)
				continue;

			const std::wstring exeName = entry.szExeFile;
			const bool nameMatch =
				EqualsIgnoreCase(exeName, L"mihomo.exe")
				|| EqualsIgnoreCase(exeName, L"winws.exe")
				|| EqualsIgnoreCase(exeName, L"TgWsProxy_windows.exe");
			if (!nameMatch)
				continue;

			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
			if (!process)
				continue;

			const std::wstring image = QueryProcessImagePath(process);
			CloseHandle(process);
			if (image.empty())
				continue;

			bool shouldKill = false;
			if (EqualsIgnoreCase(FileNameOf(image), L"mihomo.exe") && EqualsIgnoreCase(image, mihomoExpected))
				shouldKill = true;
			else if (EqualsIgnoreCase(FileNameOf(image), L"winws.exe") && EqualsIgnoreCase(image, winwsExpected))
				shouldKill = true;
			else if (EqualsIgnoreCase(FileNameOf(image), L"TgWsProxy_windows.exe") && EqualsIgnoreCase(image, tgExeExpected))
				shouldKill = true;
			else if (EqualsIgnoreCase(FileNameOf(image), L"winws.exe") && PathIsUnderOrEqual(image, antiZapretDir))
				shouldKill = true;

			if (!shouldKill)
				continue;

			if (!TerminatePid(entry.th32ProcessID))
				continue;

			if (EqualsIgnoreCase(FileNameOf(image), L"mihomo.exe"))
				++killedMihomo;
			else if (EqualsIgnoreCase(FileNameOf(image), L"winws.exe"))
				++killedWinws;
			else
				++killedTg;
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);

	if (killedMihomo || killedWinws || killedTg)
	{
		AppLog::Instance().Append(
			LogSource::Zapret,
			"Orphan cleanup: mihomo=" + std::to_string(killedMihomo)
				+ ", winws=" + std::to_string(killedWinws)
				+ ", tg=" + std::to_string(killedTg));
	}

	if (killedMihomo > 0)
		ClearStaleLocalSystemProxy();
}
}
