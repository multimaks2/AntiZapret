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

	std::wstring VersionFilePath()
	{
		return ZapretPaths::GetAppDirectory() + L"\\version.txt";
	}
}

std::string ReadEmbedded()
{
	return ANTIZAPRET_VERSION;
}

std::string ReadLocal()
{
	// Always show the version baked into the running binary.
	// version.txt can lag behind after a partial update/install and used to show e.g. 1.3.5
	// while the title should already be 1.3.5i.
	return ReadEmbedded();
}

void SyncLocalFile()
{
	const std::string embedded = ReadEmbedded();
	const std::wstring path = VersionFilePath();

	std::string existing;
	{
		std::ifstream input(path);
		if (input)
		{
			std::string line;
			while (std::getline(input, line))
			{
				const std::string trimmed = Trim(line);
				if (!trimmed.empty())
				{
					existing = trimmed;
					break;
				}
			}
		}
	}

	if (existing == embedded)
		return;

	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (output)
		output << embedded << "\n";
}
}
