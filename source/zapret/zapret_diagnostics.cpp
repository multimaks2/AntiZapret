#include "zapret/zapret_diagnostics.h"

#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace ZapretDiagnostics
{
namespace
{
	void Add(Report& report, Severity severity, const std::string& text)
	{
		report.lines.push_back({ severity, text });
		if (severity == Severity::Error)
			++report.errorCount;
		else if (severity == Severity::Warn)
			++report.warnCount;
	}

	bool RunHidden(const std::wstring& commandLine, DWORD* outExitCode = nullptr)
	{
		STARTUPINFOW si = {};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::wstring mutableCmd = commandLine;
		if (!CreateProcessW(
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
		{
			return false;
		}
		WaitForSingleObject(pi.hProcess, 15000);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		if (outExitCode)
			*outExitCode = code;
		return true;
	}

	bool ServiceState(const char* name, bool* outExists, bool* outRunning, bool* outStopPending)
	{
		if (outExists)
			*outExists = false;
		if (outRunning)
			*outRunning = false;
		if (outStopPending)
			*outStopPending = false;

		SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!scm)
			return false;

		wchar_t wide_buf[128];
		MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_buf, 128);
		SC_HANDLE service = OpenServiceW(scm, wide_buf, SERVICE_QUERY_STATUS);
		if (!service)
		{
			CloseServiceHandle(scm);
			return true;
		}

		if (outExists)
			*outExists = true;

		SERVICE_STATUS_PROCESS ssp = {};
		DWORD needed = 0;
		if (QueryServiceStatusEx(
				service,
				SC_STATUS_PROCESS_INFO,
				reinterpret_cast<LPBYTE>(&ssp),
				sizeof(ssp),
				&needed))
		{
			if (outRunning)
				*outRunning = ssp.dwCurrentState == SERVICE_RUNNING;
			if (outStopPending)
				*outStopPending = ssp.dwCurrentState == SERVICE_STOP_PENDING;
		}

		CloseServiceHandle(service);
		CloseServiceHandle(scm);
		return true;
	}

	bool ProcessRunning(const wchar_t* exeName)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return false;

		PROCESSENTRY32W pe = {};
		pe.dwSize = sizeof(pe);
		bool found = false;
		if (Process32FirstW(snap, &pe))
		{
			do
			{
				if (_wcsicmp(pe.szExeFile, exeName) == 0)
				{
					found = true;
					break;
				}
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		return found;
	}

	bool KillProcess(const wchar_t* exeName)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return false;

		PROCESSENTRY32W pe = {};
		pe.dwSize = sizeof(pe);
		bool killed = false;
		if (Process32FirstW(snap, &pe))
		{
			do
			{
				if (_wcsicmp(pe.szExeFile, exeName) == 0)
				{
					HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
					if (proc)
					{
						if (TerminateProcess(proc, 1))
							killed = true;
						CloseHandle(proc);
					}
				}
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		return killed;
	}

	std::vector<std::wstring> EnumServiceNamesContaining(const std::vector<std::wstring>& needles)
	{
		std::vector<std::wstring> matches;
		SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
		if (!scm)
			return matches;

		DWORD bytesNeeded = 0;
		DWORD count = 0;
		DWORD resume = 0;
		EnumServicesStatusExW(
			scm,
			SC_ENUM_PROCESS_INFO,
			SERVICE_WIN32,
			SERVICE_STATE_ALL,
			nullptr,
			0,
			&bytesNeeded,
			&count,
			&resume,
			nullptr);

		std::vector<BYTE> buffer(bytesNeeded + 16);
		resume = 0;
		if (!EnumServicesStatusExW(
				scm,
				SC_ENUM_PROCESS_INFO,
				SERVICE_WIN32,
				SERVICE_STATE_ALL,
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				&bytesNeeded,
				&count,
				&resume,
				nullptr))
		{
			CloseServiceHandle(scm);
			return matches;
		}

		const auto* services = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
		for (DWORD i = 0; i < count; ++i)
		{
			const std::wstring name = services[i].lpServiceName ? services[i].lpServiceName : L"";
			const std::wstring display = services[i].lpDisplayName ? services[i].lpDisplayName : L"";
			std::wstring hay = name + L" " + display;
			for (wchar_t& ch : hay)
				ch = static_cast<wchar_t>(towlower(ch));

			for (const std::wstring& needle : needles)
			{
				std::wstring n = needle;
				for (wchar_t& ch : n)
					ch = static_cast<wchar_t>(towlower(ch));
				if (hay.find(n) != std::wstring::npos)
				{
					matches.push_back(name);
					break;
				}
			}
		}

		CloseServiceHandle(scm);
		return matches;
	}

	std::string WideToUtf8(const std::wstring& wide)
	{
		if (wide.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};
		std::string out(static_cast<size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
		return out;
	}

	bool StopAndDeleteService(const std::string& name)
	{
		wchar_t wide[128] = {};
		MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide, 128);
		std::wstring cmd = L"cmd.exe /c net stop \"";
		cmd += wide;
		cmd += L"\" >nul 2>&1 & sc delete \"";
		cmd += wide;
		cmd += L"\" >nul 2>&1";
		return RunHidden(cmd);
	}

	bool HasWinDivertSys()
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path bin = ZapretPaths::GetBinDirectory();
		if (!fs::exists(bin, ec))
			return false;
		for (const auto& entry : fs::directory_iterator(bin, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file(ec))
				continue;
			if (entry.path().extension() == L".sys")
				return true;
		}
		return false;
	}

	bool HostsMentionsYoutube()
	{
		wchar_t windir[MAX_PATH] = {};
		GetWindowsDirectoryW(windir, MAX_PATH);
		const std::filesystem::path hosts =
			std::filesystem::path(windir) / L"System32" / L"drivers" / L"etc" / L"hosts";
		std::error_code ec;
		if (!std::filesystem::exists(hosts, ec))
			return false;

		HANDLE file = CreateFileW(
			hosts.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
			return false;

		LARGE_INTEGER size = {};
		if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024)
		{
			CloseHandle(file);
			return false;
		}

		std::string data(static_cast<size_t>(size.QuadPart), '\0');
		DWORD read = 0;
		const BOOL ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr);
		CloseHandle(file);
		if (!ok)
			return false;
		data.resize(read);

		for (char& ch : data)
			ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
		return data.find("youtube.com") != std::string::npos
			|| data.find("youtu.be") != std::string::npos;
	}

	bool SecureDnsConfigured()
	{
		DWORD exitCode = 1;
		const std::wstring cmd =
			L"powershell.exe -NoProfile -Command "
			L"\"if ((Get-ChildItem -Recurse -Path "
			L"'HKLM:System\\CurrentControlSet\\Services\\Dnscache\\InterfaceSpecificParameters\\' "
			L"-ErrorAction SilentlyContinue | Get-ItemProperty -ErrorAction SilentlyContinue | "
			L"Where-Object { $_.DohFlags -gt 0 } | Measure-Object).Count -gt 0) { exit 0 } else { exit 1 }\"";
		if (!RunHidden(cmd, &exitCode))
			return false;
		return exitCode == 0;
	}

	bool TcpTimestampsEnabled()
	{
		DWORD exitCode = 1;
		const std::wstring cmd =
			L"cmd.exe /c netsh interface tcp show global | findstr /i timestamps | findstr /i enabled >nul";
		if (!RunHidden(cmd, &exitCode))
			return false;
		return exitCode == 0;
	}

	bool EnableTcpTimestamps()
	{
		DWORD exitCode = 1;
		return RunHidden(L"cmd.exe /c netsh interface tcp set global timestamps=enabled >nul 2>&1", &exitCode)
			&& exitCode == 0;
	}

	bool SystemProxyEnabled(std::string& outServer)
	{
		outServer.clear();
		HKEY key = nullptr;
		if (RegOpenKeyExW(
				HKEY_CURRENT_USER,
				L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
				0,
				KEY_READ,
				&key) != ERROR_SUCCESS)
		{
			return false;
		}

		DWORD enable = 0;
		DWORD size = sizeof(enable);
		DWORD type = 0;
		const bool enabled =
			RegQueryValueExW(key, L"ProxyEnable", nullptr, &type, reinterpret_cast<LPBYTE>(&enable), &size)
				== ERROR_SUCCESS
			&& enable == 1;

		if (enabled)
		{
			wchar_t server[512] = {};
			DWORD serverSize = sizeof(server);
			if (RegQueryValueExW(
					key,
					L"ProxyServer",
					nullptr,
					&type,
					reinterpret_cast<LPBYTE>(server),
					&serverSize)
				== ERROR_SUCCESS)
			{
				outServer = WideToUtf8(server);
			}
		}

		RegCloseKey(key);
		return enabled;
	}
}

Report Run()
{
	Report report;

	bool bfeExists = false;
	bool bfeRunning = false;
	ServiceState("BFE", &bfeExists, &bfeRunning, nullptr);
	if (bfeRunning)
		Add(report, Severity::Ok, "Проверка Base Filtering Engine пройдена");
	else
		Add(report, Severity::Error, "[X] Служба Base Filtering Engine не запущена. Она необходима для работы zapret");

	std::string proxyServer;
	if (SystemProxyEnabled(proxyServer))
	{
		Add(report, Severity::Warn, "[?] Включён системный прокси: " + (proxyServer.empty() ? "(неизвестно)" : proxyServer));
		Add(report, Severity::Warn, "Убедитесь, что он настроен правильно, или отключите, если прокси не используете");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка прокси пройдена");
	}

	if (TcpTimestampsEnabled())
	{
		Add(report, Severity::Ok, "Проверка TCP timestamps пройдена");
	}
	else
	{
		Add(report, Severity::Warn, "[?] TCP timestamps отключены. Включаем timestamps...");
		if (EnableTcpTimestamps())
			Add(report, Severity::Ok, "TCP timestamps успешно включены");
		else
			Add(report, Severity::Error, "[X] Не удалось включить TCP timestamps");
	}

	if (ProcessRunning(L"AdguardSvc.exe"))
	{
		Add(report, Severity::Error, "[X] Обнаружен процесс Adguard. Adguard может мешать работе Discord");
		Add(report, Severity::Error, "https://github.com/Flowseal/zapret-discord-youtube/issues/417");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка Adguard пройдена");
	}

	const auto killer = EnumServiceNamesContaining({ L"Killer" });
	if (!killer.empty())
	{
		Add(report, Severity::Error, "[X] Обнаружены службы Killer. Killer конфликтует с zapret");
		Add(report, Severity::Error, "https://github.com/Flowseal/zapret-discord-youtube/issues/2512#issuecomment-2821119513");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка Killer пройдена");
	}

	DWORD intelExit = 1;
	RunHidden(
		L"cmd.exe /c sc query | findstr /I \"Intel\" | findstr /I \"Connectivity\" | findstr /I \"Network\" >nul",
		&intelExit);
	if (intelExit == 0)
	{
		Add(report, Severity::Error, "[X] Обнаружена служба Intel Connectivity Network. Она конфликтует с zapret");
		Add(report, Severity::Error, "https://github.com/ValdikSS/GoodbyeDPI/issues/541#issuecomment-2661670982");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка Intel Connectivity пройдена");
	}

	bool checkpointFound = false;
	bool exists = false;
	ServiceState("TracSrvWrapper", &exists, nullptr, nullptr);
	if (exists)
		checkpointFound = true;
	ServiceState("EPWD", &exists, nullptr, nullptr);
	if (exists)
		checkpointFound = true;
	if (checkpointFound)
	{
		Add(report, Severity::Error, "[X] Обнаружены службы Check Point. Check Point конфликтует с zapret");
		Add(report, Severity::Error, "Попробуйте удалить Check Point");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка Check Point пройдена");
	}

	if (!EnumServiceNamesContaining({ L"SmartByte" }).empty())
	{
		Add(report, Severity::Error, "[X] Обнаружены службы SmartByte. SmartByte конфликтует с zapret");
		Add(report, Severity::Error, "Попробуйте удалить SmartByte или отключить через services.msc");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка SmartByte пройдена");
	}

	if (!HasWinDivertSys())
		Add(report, Severity::Error, "Файл WinDivert64.sys не найден.");

	const auto vpnServices = EnumServiceNamesContaining({ L"VPN" });
	if (!vpnServices.empty())
	{
		std::string list;
		for (size_t i = 0; i < vpnServices.size(); ++i)
		{
			if (i)
				list += ", ";
			list += WideToUtf8(vpnServices[i]);
		}
		Add(report, Severity::Warn, "[?] Обнаружены VPN-службы: " + list + ". Некоторые VPN могут конфликтовать с zapret");
		Add(report, Severity::Warn, "Убедитесь, что все VPN отключены");
	}
	else
	{
		Add(report, Severity::Ok, "Проверка VPN пройдена");
	}

	if (SecureDnsConfigured())
		Add(report, Severity::Ok, "Проверка Secure DNS пройдена");
	else
	{
		Add(report, Severity::Warn, "[?] Настройте защищённый DNS в браузере (нестандартный DNS-провайдер),");
		Add(report, Severity::Warn, "В Windows 11 можно включить шифрованный DNS в Параметрах, чтобы скрыть это предупреждение");
	}

	if (HostsMentionsYoutube())
	{
		Add(
			report,
			Severity::Warn,
			"[?] В файле hosts есть записи для youtube.com или youtu.be. Это может мешать доступу к YouTube");
	}

	const bool winwsRunning = ProcessRunning(L"winws.exe");
	bool windivertExists = false;
	bool windivertRunning = false;
	bool windivertStopPending = false;
	ServiceState("WinDivert", &windivertExists, &windivertRunning, &windivertStopPending);
	if (!winwsRunning && (windivertRunning || windivertStopPending))
	{
		Add(report, Severity::Warn, "[?] winws.exe не запущен, но служба WinDivert активна. Пытаемся удалить WinDivert...");
		StopAndDeleteService("WinDivert");
		bool stillExists = false;
		ServiceState("WinDivert", &stillExists, nullptr, nullptr);
		if (stillExists)
		{
			Add(report, Severity::Error, "[X] Не удалось удалить WinDivert. Ищем конфликтующие службы...");
			bool foundConflict = false;
			bool gdExists = false;
			ServiceState("GoodbyeDPI", &gdExists, nullptr, nullptr);
			if (gdExists)
			{
				Add(report, Severity::Warn, "[?] Найдена конфликтующая служба: GoodbyeDPI. Останавливаем и удаляем...");
				StopAndDeleteService("GoodbyeDPI");
				foundConflict = true;
				Add(report, Severity::Ok, "Служба успешно удалена: GoodbyeDPI");
			}
			if (!foundConflict)
			{
				Add(report, Severity::Error, "[X] Конфликтующие службы не найдены. Проверьте вручную, не использует ли WinDivert другой обход.");
			}
			else
			{
				StopAndDeleteService("WinDivert");
				ServiceState("WinDivert", &stillExists, nullptr, nullptr);
				if (!stillExists)
					Add(report, Severity::Ok, "WinDivert успешно удалён после удаления конфликтующих служб");
				else
					Add(report, Severity::Error, "[X] WinDivert всё ещё не удаётся удалить. Проверьте вручную, не использует ли его другой обход.");
			}
		}
		else
		{
			Add(report, Severity::Ok, "WinDivert успешно удалён");
		}
	}

	static const char* kConflicting[] = { "GoodbyeDPI", "discordfix_zapret", "winws1", "winws2" };
	for (const char* name : kConflicting)
	{
		bool found = false;
		ServiceState(name, &found, nullptr, nullptr);
		if (found)
			report.conflictingServices.emplace_back(name);
	}
	if (!report.conflictingServices.empty())
	{
		std::string list;
		for (size_t i = 0; i < report.conflictingServices.size(); ++i)
		{
			if (i)
				list += " ";
			list += report.conflictingServices[i];
		}
		Add(report, Severity::Error, "[X] Обнаружены конфликтующие службы обхода: " + list);
	}

	Add(report, Severity::Info, "Диагностика завершена.");
	return report;
}

bool RemoveServices(const std::vector<std::string>& serviceNames)
{
	bool allOk = true;
	for (const std::string& name : serviceNames)
	{
		StopAndDeleteService(name);
		bool exists = false;
		ServiceState(name.c_str(), &exists, nullptr, nullptr);
		if (exists)
			allOk = false;
	}
	StopAndDeleteService("WinDivert");
	StopAndDeleteService("WinDivert14");
	return allOk;
}

bool ClearDiscordCache()
{
	if (ProcessRunning(L"Discord.exe"))
		KillProcess(L"Discord.exe");

	wchar_t appdata[MAX_PATH] = {};
	if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0)
		return false;

	namespace fs = std::filesystem;
	const fs::path discordDir = fs::path(appdata) / L"discord";
	bool anyOk = false;
	for (const wchar_t* sub : { L"Cache", L"Code Cache", L"GPUCache" })
	{
		const fs::path dir = discordDir / sub;
		std::error_code ec;
		if (!fs::exists(dir, ec))
			continue;
		fs::remove_all(dir, ec);
		if (!ec)
			anyOk = true;
	}
	return anyOk;
}
}
