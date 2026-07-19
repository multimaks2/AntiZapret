#pragma once

#include <string>

namespace StrategyBatParser
{

// Reads the winws.exe invocation from a zapret batch file, joins ^ continuations,
// and expands the variables supplied by AntiZapret at launch time.
std::string BuildExpandedArgsFromBat(
	const std::wstring& filePath,
	const std::string& binPath,
	const std::string& listsPath,
	const std::string& gameFilterTcp,
	const std::string& gameFilterUdp);

}  // namespace StrategyBatParser
