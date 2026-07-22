#pragma once

#include <string>

namespace MihomoApi
{
	bool ReloadConfig(const std::wstring& configPath, int apiPort, std::string& outError);
	void FlushConnections(int apiPort);

	// Core-measured delay via GET /proxies/{name}/delay (Clash Meta). -1 on failure.
	int GetProxyDelayMs(
		int apiPort,
		const std::string& proxyName,
		const char* testUrl = "https://www.gstatic.com/generate_204",
		int timeoutMs = 5000);
}
