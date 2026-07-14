#pragma once

#include <string>

namespace MihomoApi
{
	bool ReloadConfig(const std::wstring& configPath, int apiPort, std::string& outError);
	void FlushConnections(int apiPort);
}
