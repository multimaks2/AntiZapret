#pragma once

#include <string>

namespace AppVersion
{
	// Version of the running binary (compile-time). Prefer this for UI.
	std::string ReadEmbedded();

	// Reads version.txt next to the executable, or falls back to ReadEmbedded().
	std::string ReadLocal();

	// Writes ANTIZAPRET_VERSION into version.txt so updater/UI stay in sync with the exe.
	void SyncLocalFile();
}
