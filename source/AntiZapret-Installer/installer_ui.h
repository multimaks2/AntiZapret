#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct InstallerUiState
{
	std::string installPath = "C:\\Program Files (x86)\\AntiZapret";
	std::string releaseVersion;   // e.g. "1.3.5"
	std::string releaseZipName;   // e.g. "AntiZapret-1.3.5-win32.zip"
	std::string releaseZipUrl;

	bool acceptedTerms = false;
	bool createDesktopShortcut = true;
	bool launchAfterInstall = true;

	bool closeRequested = false;
	bool installRequested = false;
	bool launchRequested = false;

	std::mutex mutex;
	std::string status = "Ожидание...";
	std::string error;
	std::vector<std::string> logs;
	float progress = 0.f;
	std::atomic<std::uint64_t> revision{ 0 };
	bool installStarted = false;
	bool installFinished = false;
	bool installFailed = false;
};

struct InstallPathCheck
{
	bool ok = false;
	bool willCleanExisting = false;
	std::string message;
};

// Validates install folder: blocks Windows system paths; non-empty AntiZapret dirs are OK (will be cleaned).
InstallPathCheck CheckInstallPath(const std::string& pathUtf8);

// Resolves latest AntiZapret-*-win32.zip from GitHub Releases into state.
void ResolveLatestRelease(InstallerUiState& state);

// Downloads, extracts, copies files, optional desktop shortcut. Thread-safe UI updates.
void RunInstallWorker(InstallerUiState& state);

// Runs the ImGui installer wizard. Returns true if the user completed the flow.
bool RunInstallerUi(InstallerUiState& state);

// Launches AntiZapret.exe from the install directory.
bool LaunchInstalledApp(const std::string& installPathUtf8);
