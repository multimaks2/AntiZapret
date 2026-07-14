#pragma once

#include <string>
#include <vector>

namespace VpnRulesUpdater
{
	void StartBackgroundUpdate();
	bool IsUpdateInProgress();
	std::string GetStatusMessage();
	void EnsureGeositeFiles(const std::vector<std::string>& geositeNames);
}
