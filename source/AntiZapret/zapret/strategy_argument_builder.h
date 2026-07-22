#pragma once

#include "zapret/strategies.hpp"

#include <string>
#include <vector>

namespace StrategyArgumentBuilder
{

std::string BuildCommandLine(
	const ZapretStrategies::StrategyDefinition& strategy,
	ZapretStrategies::GameFilterMode gameFilterMode,
	const std::string& binDirUtf8,
	const std::string& listsDirUtf8);

std::string BuildCommandLine(
	const std::vector<std::string>& args,
	ZapretStrategies::GameFilterMode gameFilterMode,
	const std::string& binDirUtf8,
	const std::string& listsDirUtf8);

}  // namespace StrategyArgumentBuilder
