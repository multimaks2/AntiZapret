#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ProcessJob
{
	// Job с JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE: при аварийном закрытии приложения
	// дочерние процессы в job завершаются вместе с ним.
	bool EnsureInitialized();

	// CreateProcess + AssignProcessToJobObject (CREATE_SUSPENDED → assign → Resume).
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
		LPPROCESS_INFORMATION processInformation);

	// При старте: убить осиротевшие mihomo/winws/TgWsProxy из наших папок;
	// если убили mihomo — сбросить залипший системный proxy 127.0.0.1:*.
	void CleanupOrphansAtStartup();
}
