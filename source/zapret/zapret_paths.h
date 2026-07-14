#pragma once

#include <string>

namespace ZapretPaths
{

std::wstring GetExecutableDirectory();
std::wstring GetAppDirectory();
std::wstring GetAntiZapretDirectory();
std::wstring GetBinDirectory();
std::wstring GetListsDirectory();
std::wstring GetTgWsProxyDirectory();
std::wstring GetVpnDirectory();

bool IsValidLayout(const std::wstring& antiZapretDir);
void EnsureUserListsFiles(const std::wstring& antiZapretDir);

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);

}  // namespace ZapretPaths
