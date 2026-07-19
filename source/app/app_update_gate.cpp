#include "app/app_update_gate.h"

#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <Shellapi.h>

#include <string>
#include <vector>

namespace AppUpdateGate
{
namespace
{
	constexpr wchar_t kUpdatedFlag[] = L"--updated";
	constexpr wchar_t kUpdaterExeName[] = L"AntiZapret-Updater.exe";

	bool CommandHasFlag(const wchar_t* cmdLine, const wchar_t* flag)
	{
		if (!cmdLine || !flag)
			return false;

		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
		if (!argv)
			return wcsstr(cmdLine, flag) != nullptr;

		bool found = false;
		for (int i = 1; i < argc; ++i)
		{
			if (_wcsicmp(argv[i], flag) == 0)
			{
				found = true;
				break;
			}
		}
		LocalFree(argv);
		return found;
	}

	std::wstring BuildUpdaterArgs(const wchar_t* cmdLine)
	{
		std::wstring args;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(cmdLine ? cmdLine : L"", &argc);
		if (!argv)
			return args;

		for (int i = 1; i < argc; ++i)
		{
			if (_wcsicmp(argv[i], kUpdatedFlag) == 0)
				continue;
			if (!args.empty())
				args.push_back(L' ');
			const bool needsQuotes = wcschr(argv[i], L' ') != nullptr;
			if (needsQuotes)
				args.push_back(L'"');
			args += argv[i];
			if (needsQuotes)
				args.push_back(L'"');
		}
		LocalFree(argv);
		return args;
	}
}

bool HasUpdatedFlag()
{
	return CommandHasFlag(GetCommandLineW(), kUpdatedFlag);
}

bool HandOffToUpdaterAndShouldExit()
{
	if (HasUpdatedFlag())
		return false;

	const std::wstring dir = ZapretPaths::GetAppDirectory();
	const std::wstring updaterPath = dir + L"\\" + kUpdaterExeName;
	if (GetFileAttributesW(updaterPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false;

	const std::wstring forwarded = BuildUpdaterArgs(GetCommandLineW());

	// ShellExecute + wait for input idle so the updater window can appear
	// before this process exits (otherwise Windows may suppress the child UI).
	SHELLEXECUTEINFOW info = { sizeof(info) };
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"open";
	info.lpFile = updaterPath.c_str();
	info.lpParameters = forwarded.empty() ? nullptr : forwarded.c_str();
	info.lpDirectory = dir.c_str();
	info.nShow = SW_SHOWNORMAL;

	AllowSetForegroundWindow(ASFW_ANY);

	if (!ShellExecuteExW(&info))
	{
		MessageBoxW(
			nullptr,
			L"Не удалось запустить AntiZapret-Updater.exe.\nПриложение стартует без проверки обновлений.",
			L"AntiZapret",
			MB_OK | MB_ICONWARNING);
		return false;
	}

	if (info.hProcess)
	{
		AllowSetForegroundWindow(GetProcessId(info.hProcess));
		WaitForInputIdle(info.hProcess, 5000);
		CloseHandle(info.hProcess);
	}

	return true;
}
}
