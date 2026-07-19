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
	constexpr wchar_t kUpdaterExeName[] = L"z-updater.exe";

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

	// Forward user args to the updater (skip exe path and --updated itself).
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
	{
		// No updater next to exe — run as-is (dev builds / incomplete packs).
		return false;
	}

	const std::wstring forwarded = BuildUpdaterArgs(GetCommandLineW());
	std::wstring commandLine = L"\"" + updaterPath + L"\"";
	if (!forwarded.empty())
	{
		commandLine.push_back(L' ');
		commandLine += forwarded;
	}

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> mutableCmd(commandLine.begin(), commandLine.end());
	mutableCmd.push_back(L'\0');

	const BOOL ok = CreateProcessW(
		updaterPath.c_str(),
		mutableCmd.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		dir.c_str(),
		&si,
		&pi);

	if (!ok)
	{
		MessageBoxW(
			nullptr,
			L"Не удалось запустить z-updater.exe.\nПриложение стартует без проверки обновлений.",
			L"AntiZapret",
			MB_OK | MB_ICONWARNING);
		return false;
	}

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}
}
