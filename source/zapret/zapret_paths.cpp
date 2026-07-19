#include "zapret/zapret_paths.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace ZapretPaths
{
namespace
{
	std::wstring NormalizeDirectory(std::wstring path)
	{
		while (!path.empty() && (path.back() == L'\\' || path.back() == L'/'))
			path.pop_back();
		return path;
	}
}

std::wstring GetExecutableDirectory()
{
	wchar_t buffer[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return L".";

	std::filesystem::path path(buffer);
	path = path.parent_path();
	return NormalizeDirectory(path.wstring());
}

std::wstring GetAppDirectory()
{
	return GetExecutableDirectory();
}

std::wstring GetAntiZapretDirectory()
{
	return NormalizeDirectory(GetAppDirectory() + L"\\anti-zapret");
}

std::wstring GetBinDirectory()
{
	return NormalizeDirectory(GetAntiZapretDirectory() + L"\\bin");
}

std::wstring GetListsDirectory()
{
	return NormalizeDirectory(GetAntiZapretDirectory() + L"\\lists");
}

std::wstring GetTgWsProxyDirectory()
{
	return NormalizeDirectory(GetAppDirectory() + L"\\tg-ws-proxy");
}

std::wstring GetVpnDirectory()
{
	return NormalizeDirectory(GetAppDirectory() + L"\\vpn");
}

std::wstring GetCacheDirectory()
{
	return NormalizeDirectory(GetAppDirectory() + L"\\cache");
}

std::wstring GetSettingsPath()
{
	return (std::filesystem::path(GetAppDirectory()) / L"settings.ini").wstring();
}

namespace
{
	void CopyFileIfMissing(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		std::error_code ec;
		if (!std::filesystem::exists(from, ec) || std::filesystem::exists(to, ec))
			return;
		std::filesystem::create_directories(to.parent_path(), ec);
		std::filesystem::copy_file(from, to, ec);
	}

	void CopyTreeIfMissing(const std::filesystem::path& from, const std::filesystem::path& to)
	{
		std::error_code ec;
		if (!std::filesystem::exists(from, ec) || !std::filesystem::is_directory(from, ec))
			return;
		if (!std::filesystem::exists(to, ec))
		{
			std::filesystem::create_directories(to.parent_path(), ec);
			std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, ec);
			return;
		}
		for (const auto& entry : std::filesystem::directory_iterator(from, ec))
		{
			const auto dest = to / entry.path().filename();
			if (entry.is_directory(ec))
				CopyTreeIfMissing(entry.path(), dest);
			else
				CopyFileIfMissing(entry.path(), dest);
		}
	}

	void AppendFileSectionIfAbsent(
		const std::filesystem::path& settingsPath,
		const char* sectionName,
		const std::filesystem::path& legacyFile)
	{
		std::error_code ec;
		if (!std::filesystem::exists(legacyFile, ec))
			return;

		std::string existing;
		{
			std::ifstream in(settingsPath, std::ios::binary);
			if (in)
				existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		const std::string sectionHeader = std::string("[") + sectionName + "]";
		if (existing.find(sectionHeader) != std::string::npos)
			return;

		std::ifstream legacy(legacyFile, std::ios::binary);
		if (!legacy)
			return;

		std::ofstream out(settingsPath, std::ios::binary | std::ios::app);
		if (!out)
			return;

		out << "\r\n" << sectionHeader << "\r\n";
		std::string line;
		while (std::getline(legacy, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;
			if (line.front() == '[' && line.back() == ']')
				continue;
			out << line << "\r\n";
		}
	}

	void MergeVpnSettingsIntoAppSettings(const std::filesystem::path& settingsPath, const std::filesystem::path& legacyVpnSettings)
	{
		AppendFileSectionIfAbsent(settingsPath, "vpn", legacyVpnSettings);
	}

	void MergeZapretResultsIntoAppSettings(const std::filesystem::path& settingsPath, const std::filesystem::path& legacyResult)
	{
		std::error_code ec;
		if (!std::filesystem::exists(legacyResult, ec))
			return;

		std::string existing;
		{
			std::ifstream in(settingsPath, std::ios::binary);
			if (in)
				existing.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}
		if (existing.find("[zapret_results]") != std::string::npos)
			return;

		std::ifstream legacy(legacyResult, std::ios::binary);
		if (!legacy)
			return;

		std::ofstream out(settingsPath, std::ios::binary | std::ios::app);
		if (!out)
			return;

		out << "\r\n[zapret_results]\r\n";
		std::string line;
		while (std::getline(legacy, line))
		{
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			if (line.empty())
				continue;
			if (line.rfind("; last_strategy=", 0) == 0)
			{
				out << "last_strategy=" << line.substr(16) << "\r\n";
				continue;
			}
			if (line[0] == ';' || line[0] == '#')
				continue;
			const size_t pipe = line.find('|');
			if (pipe == std::string::npos)
				continue;
			out << "r." << line.substr(0, pipe) << "=" << line.substr(pipe + 1) << "\r\n";
		}
	}
}

void EnsureDataLayout()
{
	const std::filesystem::path appDir(GetAppDirectory());
	const std::filesystem::path cacheDir = appDir / L"cache";
	const std::filesystem::path marker = cacheDir / L".layout_v2";
	const std::filesystem::path settingsPath = appDir / L"settings.ini";
	const std::filesystem::path vpnDir = appDir / L"vpn";
	const std::filesystem::path oldVpnCache = vpnDir / L"cache";

	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);

	if (std::filesystem::exists(marker, ec))
		return;

	// Runtime-generated mihomo home files: vpn/ -> cache/
	CopyFileIfMissing(vpnDir / L"config.yaml", cacheDir / L"config.yaml");
	CopyFileIfMissing(vpnDir / L"cache.db", cacheDir / L"cache.db");
	CopyFileIfMissing(vpnDir / L"geoip.metadb", cacheDir / L"geoip.metadb");
	CopyTreeIfMissing(vpnDir / L"srss", cacheDir / L"srss");
	CopyTreeIfMissing(vpnDir / L"flags", cacheDir / L"flags");

	// Old vpn/cache data files (except settings.ini — merged below)
	if (std::filesystem::exists(oldVpnCache, ec))
	{
		for (const auto& entry : std::filesystem::directory_iterator(oldVpnCache, ec))
		{
			const auto name = entry.path().filename().wstring();
			if (name == L"settings.ini")
				continue;
			const auto dest = cacheDir / entry.path().filename();
			if (entry.is_directory(ec))
				CopyTreeIfMissing(entry.path(), dest);
			else
				CopyFileIfMissing(entry.path(), dest);
		}
	}

	MergeVpnSettingsIntoAppSettings(settingsPath, oldVpnCache / L"settings.ini");
	AppendFileSectionIfAbsent(settingsPath, "smart_strategy", appDir / L"smart_strategy.ini");
	MergeZapretResultsIntoAppSettings(settingsPath, appDir / L"result.ini");

	{
		std::ofstream out(marker, std::ios::binary | std::ios::trunc);
		if (out)
			out << "layout=v2\r\n";
	}
}

bool IsValidLayout(const std::wstring& antiZapretDir)
{
	const std::filesystem::path root(antiZapretDir);
	return std::filesystem::exists(root / L"bin" / L"winws.exe")
		&& std::filesystem::is_directory(root / L"lists");
}

void EnsureUserListsFiles(const std::wstring& antiZapretDir)
{
	const std::filesystem::path lists = std::filesystem::path(antiZapretDir) / L"lists";
	std::error_code ec;
	std::filesystem::create_directories(lists, ec);

	const auto writeIfMissing = [&](const wchar_t* fileName, const char* content)
	{
		const std::filesystem::path file = lists / fileName;
		if (std::filesystem::exists(file))
			return;

		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		if (out)
			out << content;
	};

	writeIfMissing(L"ipset-exclude-user.txt", "203.0.113.113/32");
	writeIfMissing(L"list-general-user.txt", "domain.example.abc");
	writeIfMissing(L"list-exclude-user.txt", "domain.example.abc");
}

std::string WideToUtf8(const std::wstring& value)
{
	if (value.empty())
		return {};

	const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0)
		return {};

	std::string result(static_cast<size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
	return result;
}

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty())
		return {};

	const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0)
		return {};

	std::wstring result(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
	return result;
}

}  // namespace ZapretPaths
