// AntiZapret-Installer — first-time setup wizard (download latest release + install).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "AntiZapret-Installer/installer_ui.h"

int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	InstallerUiState state;
	const bool completed = RunInstallerUi(state);

	if (completed && state.launchRequested)
	{
		if (!LaunchInstalledApp(state.installPath))
		{
			MessageBoxW(
				nullptr,
				L"AntiZapret установлен, но не удалось запустить AntiZapret.exe",
				L"AntiZapret-Installer",
				MB_OK | MB_ICONWARNING);
		}
	}

	return state.installFailed ? 1 : 0;
}
