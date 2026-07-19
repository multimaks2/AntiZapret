#pragma once

namespace AppUpdateGate
{
	// True when launched by z-updater after the update check (or skip).
	bool HasUpdatedFlag();

	// If AntiZapret was started without --updated, hand off to z-updater.exe and exit.
	// Returns true when the caller should exit immediately.
	bool HandOffToUpdaterAndShouldExit();
}
