#pragma once

namespace AppUpdateGate
{
	// True when launched by AntiZapret-Updater after the update check (or skip).
	bool HasUpdatedFlag();

	// If AntiZapret was started without --updated, hand off to AntiZapret-Updater.exe and exit.
	// Returns true when the caller should exit immediately.
	bool HandOffToUpdaterAndShouldExit();
}
