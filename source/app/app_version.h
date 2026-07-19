#pragma once

#include <string>

namespace AppVersion
{
	// Reads version.txt next to the running executable. Falls back to compile-time ANTIZAPRET_VERSION.
	std::string ReadLocal();
}
