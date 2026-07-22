#include "zapret/strategy_argument_builder.h"

#include <cstring>
#include <string_view>
#include <vector>

namespace StrategyArgumentBuilder
{
namespace
{
	std::string WithTrailingSlash(std::string path)
	{
		if (!path.empty() && path.back() != '\\' && path.back() != '/')
			path.push_back('\\');
		return path;
	}

	void ReplaceAll(std::string& target, const char* needle, const std::string& replacement)
	{
		const size_t needleLen = strlen(needle);
		size_t pos = 0;
		while ((pos = target.find(needle, pos)) != std::string::npos)
		{
			target.replace(pos, needleLen, replacement);
			pos += replacement.size();
		}
	}

	std::string ExpandArg(
		const char* arg,
		const std::string& binDir,
		const std::string& listsDir,
		const ZapretStrategies::GameFilterValues& gameFilter)
	{
		std::string expanded = arg ? arg : "";

		ReplaceAll(expanded, "${BIN}", binDir);
		ReplaceAll(expanded, "${LISTS}", listsDir);
		ReplaceAll(expanded, "${GameFilterTCP}", std::string(gameFilter.gameFilterTcp));
		ReplaceAll(expanded, "${GameFilterUDP}", std::string(gameFilter.gameFilterUdp));
		ReplaceAll(expanded, "${GameFilter}", std::string(gameFilter.gameFilter));

		return expanded;
	}

	std::string QuoteIfNeeded(const std::string& arg)
	{
		if (arg.find_first_of(" \t") == std::string::npos)
			return arg;

		std::string quoted = "\"";
		for (const char ch : arg)
		{
			if (ch == '"')
				quoted += "\"\"";
			else
				quoted.push_back(ch);
		}
		quoted.push_back('"');
		return quoted;
	}
}

std::string BuildCommandLine(
	const ZapretStrategies::StrategyDefinition& strategy,
	ZapretStrategies::GameFilterMode gameFilterMode,
	const std::string& binDirUtf8,
	const std::string& listsDirUtf8)
{
	const std::string binPath = WithTrailingSlash(binDirUtf8);
	const std::string listsPath = WithTrailingSlash(listsDirUtf8);
	const ZapretStrategies::GameFilterValues gameFilter = ZapretStrategies::GetGameFilterValues(gameFilterMode);

	std::string commandLine;
	for (std::size_t i = 0; i < strategy.argCount; ++i)
	{
		const std::string expanded = ExpandArg(strategy.args[i], binPath, listsPath, gameFilter);
		if (!commandLine.empty())
			commandLine.push_back(' ');
		commandLine += QuoteIfNeeded(expanded);
	}

	return commandLine;
}

std::string BuildCommandLine(
	const std::vector<std::string>& args,
	ZapretStrategies::GameFilterMode gameFilterMode,
	const std::string& binDirUtf8,
	const std::string& listsDirUtf8)
{
	const std::string binPath = WithTrailingSlash(binDirUtf8);
	const std::string listsPath = WithTrailingSlash(listsDirUtf8);
	const ZapretStrategies::GameFilterValues gameFilter = ZapretStrategies::GetGameFilterValues(gameFilterMode);

	std::string commandLine;
	for (const std::string& arg : args)
	{
		const std::string expanded = ExpandArg(arg.c_str(), binPath, listsPath, gameFilter);
		if (!commandLine.empty())
			commandLine.push_back(' ');
		commandLine += QuoteIfNeeded(expanded);
	}

	return commandLine;
}

}  // namespace StrategyArgumentBuilder
