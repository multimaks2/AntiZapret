#include "zapret/zapret_paths.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>

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
