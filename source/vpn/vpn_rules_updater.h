#pragma once

#include <string>
#include <vector>

namespace VpnRulesUpdater
{
	void StartBackgroundUpdate();
	bool IsUpdateInProgress();
	// True when essential RUv1 rule-set files exist under cache/srss/.
	bool AreCoreRulesReady();
	std::string GetStatusMessage();
	void EnsureGeositeFiles(const std::vector<std::string>& geositeNames);
}
