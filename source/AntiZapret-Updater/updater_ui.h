#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct UpdaterUiState
{
	std::mutex mutex;
	std::string title = "Обновление AntiZapret";
	std::string status;
	std::string error;
	std::vector<std::string> logs;
	float progress = 0.f; // 0..1
	std::atomic<std::uint64_t> revision{ 0 };
	bool finished = false;
	bool failed = false;
	bool closeRequested = false;
	bool launchRequested = false; // user clicked continue after error
};

// Runs the ImGui window until finished+auto-close or user closes after error.
// Returns true if the app should be launched afterward.
bool RunUpdaterUi(UpdaterUiState& state);
