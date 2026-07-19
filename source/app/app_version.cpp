#include "app/app_version.h"

#include "version.h"
#include "zapret/zapret_paths.h"

#include <fstream>
#include <string>

namespace AppVersion
{
namespace
{
	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		return value.substr(start);
	}
}

std::string ReadLocal()
{
	const std::wstring dir = ZapretPaths::GetAppDirectory();
	const std::wstring path = dir + L"\\version.txt";
	std::ifstream input(path);
	if (input)
	{
		std::string line;
		while (std::getline(input, line))
		{
			const std::string trimmed = Trim(line);
			if (!trimmed.empty())
				return trimmed;
		}
	}
	return ANTIZAPRET_VERSION;
}
}
