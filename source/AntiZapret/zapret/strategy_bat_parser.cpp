#include "zapret/strategy_bat_parser.h"

#include "zapret/zapret_paths.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>

namespace StrategyBatParser
{
namespace
{
	std::string ReadFileUtf8(const std::wstring& filePath)
	{
		std::ifstream file(filePath, std::ios::binary);
		if (!file)
			return {};
		return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	}

	size_t FindWinwsExe(const std::string& line)
	{
		constexpr char needle[] = "winws.exe";
		for (size_t i = 0; i + sizeof(needle) - 1 <= line.size(); ++i)
		{
			bool matches = true;
			for (size_t j = 0; j + 1 < sizeof(needle); ++j)
			{
				if (std::tolower(static_cast<unsigned char>(line[i + j])) != needle[j])
				{
					matches = false;
					break;
				}
			}
			if (matches)
				return i;
		}
		return std::string::npos;
	}

	std::string ExtractArgs(const std::string& content)
	{
		size_t offset = 0;
		while (offset < content.size())
		{
			const size_t lineEnd = content.find('\n', offset);
			const size_t end = lineEnd == std::string::npos ? content.size() : lineEnd;
			std::string line = content.substr(offset, end - offset);
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			const size_t exe = FindWinwsExe(line);
			if (exe == std::string::npos)
			{
				offset = lineEnd == std::string::npos ? content.size() : lineEnd + 1;
				continue;
			}

			std::string args = line.substr(exe + std::strlen("winws.exe"));
			offset = lineEnd == std::string::npos ? content.size() : lineEnd + 1;
			while (!args.empty() && args.back() == '^' && offset < content.size())
			{
				args.pop_back();
				const size_t nextEnd = content.find('\n', offset);
				const size_t next = nextEnd == std::string::npos ? content.size() : nextEnd;
				std::string continuation = content.substr(offset, next - offset);
				if (!continuation.empty() && continuation.back() == '\r')
					continuation.pop_back();
				args += continuation;
				offset = nextEnd == std::string::npos ? content.size() : nextEnd + 1;
			}

			const size_t first = args.find_first_not_of(" \t");
			if (first == std::string::npos)
				return {};
			args.erase(0, first);
			if (!args.empty() && args.front() == '"')
				args.erase(0, 1); // closing quote after "%BIN%winws.exe"
			const size_t argFirst = args.find_first_not_of(" \t");
			return argFirst == std::string::npos ? std::string() : args.substr(argFirst);
		}
		return {};
	}

	void ReplaceAll(std::string& value, const char* from, const std::string& to)
	{
		size_t position = 0;
		const size_t fromLength = std::strlen(from);
		while ((position = value.find(from, position)) != std::string::npos)
		{
			value.replace(position, fromLength, to);
			position += to.size();
		}
	}
}

std::string BuildExpandedArgsFromBat(
	const std::wstring& filePath,
	const std::string& binPath,
	const std::string& listsPath,
	const std::string& gameFilterTcp,
	const std::string& gameFilterUdp)
{
	std::string args = ExtractArgs(ReadFileUtf8(filePath));
	if (args.empty())
		return {};

	ReplaceAll(args, "%BIN%", binPath);
	ReplaceAll(args, "%LISTS%", listsPath);
	ReplaceAll(args, "%GameFilterTCP%", gameFilterTcp);
	ReplaceAll(args, "%GameFilterUDP%", gameFilterUdp);
	return args;
}

}  // namespace StrategyBatParser
