#include "vpn/vpn_module_update_check.h"

#include "app/app_config.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>

#include <cstdio>
#include <filesystem>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "version.lib")

namespace
{
	std::string TrimWhitespace(const std::string& value)
	{
		const size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	bool HasDigit(const std::string& value)
	{
		for (unsigned char ch : value)
		{
			if (ch >= '0' && ch <= '9')
				return true;
		}
		return false;
	}

	std::string NormalizeVersion(std::string value)
	{
		value = TrimWhitespace(value);
		if (!value.empty() && (value[0] == 'v' || value[0] == 'V'))
			value.erase(0, 1);
		return value;
	}

	std::string HttpGetText(const char* url, DWORD timeoutMs = 20000)
	{
		std::string result;
		HINTERNET hInternet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!hInternet)
			return {};

		InternetSetOptionA(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char headers[] = "User-Agent: AntiZapret\r\nAccept: application/vnd.github+json, text/html, */*\r\n";
		HINTERNET hUrl = InternetOpenUrlA(
			hInternet,
			url,
			headers,
			static_cast<DWORD>(sizeof(headers) - 1),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
			0);
		if (!hUrl)
		{
			InternetCloseHandle(hInternet);
			return {};
		}

		char buffer[4096];
		DWORD bytesRead = 0;
		while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0)
			result.append(buffer, buffer + bytesRead);

		InternetCloseHandle(hUrl);
		InternetCloseHandle(hInternet);
		return result;
	}

	std::string RunProcessCaptureOutput(const std::wstring& exePath, const std::wstring& args)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(sa) };
		sa.bInheritHandle = TRUE;
		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
			return {};

		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		si.hStdOutput = writePipe;
		si.hStdError = writePipe;
		si.wShowWindow = SW_HIDE;

		std::wstring cmd = L"\"" + exePath + L"\" " + args;
		std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
		cmdBuf.push_back(L'\0');

		PROCESS_INFORMATION pi = {};
		const BOOL ok = CreateProcessW(
			nullptr,
			cmdBuf.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&si,
			&pi);
		CloseHandle(writePipe);
		if (!ok)
		{
			CloseHandle(readPipe);
			return {};
		}

		std::string output;
		char buffer[512];
		DWORD bytesRead = 0;
		while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
			output.append(buffer, buffer + bytesRead);

		WaitForSingleObject(pi.hProcess, 8000);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(readPipe);
		return output;
	}

	// mihomo -v → "... v1.19.0 ..."
	std::string ReadMihomoLocalVersion()
	{
		const std::filesystem::path exe = std::filesystem::path(ZapretPaths::GetVpnDirectory()) / L"mihomo.exe";
		std::error_code ec;
		if (!std::filesystem::exists(exe, ec))
			return {};

		const std::string output = RunProcessCaptureOutput(exe.wstring(), L"-v");
		static const std::regex re(R"(v?([0-9]+\.[0-9]+(?:\.[0-9]+)?))", std::regex::icase);
		std::smatch match;
		if (std::regex_search(output, match, re) && match.size() >= 2)
			return NormalizeVersion(match[1].str());
		return {};
	}

	std::string ReadDllFileVersion(const std::filesystem::path& path)
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec))
			return {};

		const std::wstring wpath = path.wstring();
		DWORD handle = 0;
		const DWORD size = GetFileVersionInfoSizeW(wpath.c_str(), &handle);
		if (size == 0)
			return {};

		std::vector<BYTE> buffer(size);
		if (!GetFileVersionInfoW(wpath.c_str(), 0, size, buffer.data()))
			return {};

		VS_FIXEDFILEINFO* info = nullptr;
		UINT len = 0;
		if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &len) || !info)
			return {};

		char ver[64] = {};
		snprintf(
			ver,
			sizeof ver,
			"%u.%u.%u",
			HIWORD(info->dwFileVersionMS),
			LOWORD(info->dwFileVersionMS),
			HIWORD(info->dwFileVersionLS));
		return ver;
	}

	std::string ReadWintunLocalVersion()
	{
		return ReadDllFileVersion(std::filesystem::path(ZapretPaths::GetVpnDirectory()) / L"wintun.dll");
	}

	bool FetchMihomoRemoteRelease(std::string& outVersion, std::string& outTag)
	{
		outVersion.clear();
		outTag.clear();

		const std::string body = HttpGetText(AppConfig::kMihomoReleasesLatestApiUrl);
		if (body.empty())
			return false;

		const std::string key = "\"tag_name\"";
		const size_t keyPos = body.find(key);
		if (keyPos == std::string::npos)
			return false;

		const size_t colon = body.find(':', keyPos + key.size());
		if (colon == std::string::npos)
			return false;
		const size_t q1 = body.find('"', colon + 1);
		if (q1 == std::string::npos)
			return false;
		const size_t q2 = body.find('"', q1 + 1);
		if (q2 == std::string::npos)
			return false;

		outTag = body.substr(q1 + 1, q2 - q1 - 1);
		outVersion = NormalizeVersion(outTag);
		return HasDigit(outVersion);
	}

	// wintun.net page: "Wintun 0.14.1"
	bool FetchWintunRemoteVersion(std::string& outVersion)
	{
		outVersion.clear();
		const std::string body = HttpGetText(AppConfig::kWintunHomeUrl);
		if (body.empty())
			return false;

		static const std::regex re(R"(Wintun\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?))", std::regex::icase);
		std::smatch match;
		if (!std::regex_search(body, match, re) || match.size() < 2)
			return false;
		outVersion = NormalizeVersion(match[1].str());
		return HasDigit(outVersion);
	}

	ComponentUpdateStatus CompareVersions(const std::string& local, const std::string& remote, bool remoteOk)
	{
		if (!HasDigit(local))
			return remoteOk ? ComponentUpdateStatus::UpdateAvailable : ComponentUpdateStatus::Unknown;
		if (!remoteOk)
			return ComponentUpdateStatus::UpToDate;
		if (NormalizeVersion(local) == NormalizeVersion(remote))
			return ComponentUpdateStatus::UpToDate;
		return ComponentUpdateStatus::UpdateAvailable;
	}
}

VpnModuleUpdateCheck& VpnModuleUpdateCheck::Instance()
{
	static VpnModuleUpdateCheck instance;
	return instance;
}

void VpnModuleUpdateCheck::SeedLocalVersions()
{
	const std::string mihomoLocal = ReadMihomoLocalVersion();
	const std::string wintunLocal = ReadWintunLocalVersion();

	std::lock_guard<std::mutex> lock(m_mutex);
	m_mihomoLocalVersion = mihomoLocal;
	m_mihomoStatus = HasDigit(mihomoLocal) ? ComponentUpdateStatus::Checking : ComponentUpdateStatus::Unknown;
	m_wintunLocalVersion = wintunLocal;
	m_wintunStatus = HasDigit(wintunLocal) ? ComponentUpdateStatus::Checking : ComponentUpdateStatus::Unknown;
}

void VpnModuleUpdateCheck::StartBackgroundCheck()
{
	if (m_checkStarted.exchange(true))
		return;

	SeedLocalVersions();
	std::thread([this]() { RunCheck(); }).detach();
}

void VpnModuleUpdateCheck::RequestCheck()
{
	m_checkStarted.store(true);
	SeedLocalVersions();
	std::thread([this]() { RunCheck(); }).detach();
}

void VpnModuleUpdateCheck::RunCheck()
{
	if (m_checkInProgress.exchange(true))
		return;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatus = ComponentUpdateStatus::Checking;
		m_wintunStatus = ComponentUpdateStatus::Checking;
	}

	const std::string mihomoLocal = ReadMihomoLocalVersion();
	std::string mihomoRemote;
	std::string mihomoTag;
	const bool mihomoRemoteOk = FetchMihomoRemoteRelease(mihomoRemote, mihomoTag);
	const ComponentUpdateStatus mihomoStatus = CompareVersions(mihomoLocal, mihomoRemote, mihomoRemoteOk);

	const std::string wintunLocal = ReadWintunLocalVersion();
	std::string wintunRemote;
	const bool wintunRemoteOk = FetchWintunRemoteVersion(wintunRemote);
	const ComponentUpdateStatus wintunStatus = CompareVersions(wintunLocal, wintunRemote, wintunRemoteOk);

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_mihomoStatus = mihomoStatus;
		m_mihomoLocalVersion = mihomoLocal;
		m_mihomoRemoteVersion = mihomoRemote;
		m_mihomoRemoteTag = mihomoTag;
		m_wintunStatus = wintunStatus;
		m_wintunLocalVersion = wintunLocal;
		m_wintunRemoteVersion = wintunRemote;
	}

	m_checkInProgress.store(false);
}

ComponentUpdateStatus VpnModuleUpdateCheck::GetMihomoStatus() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mihomoStatus;
}

std::string VpnModuleUpdateCheck::GetMihomoLocalVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mihomoLocalVersion;
}

std::string VpnModuleUpdateCheck::GetMihomoRemoteVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mihomoRemoteVersion;
}

std::string VpnModuleUpdateCheck::GetMihomoRemoteTag() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_mihomoRemoteTag;
}

ComponentUpdateStatus VpnModuleUpdateCheck::GetWintunStatus() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_wintunStatus;
}

std::string VpnModuleUpdateCheck::GetWintunLocalVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_wintunLocalVersion;
}

std::string VpnModuleUpdateCheck::GetWintunRemoteVersion() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_wintunRemoteVersion;
}
