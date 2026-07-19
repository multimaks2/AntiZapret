// z-updater — gatekeeper for AntiZapret.
// 1) Read version.txt next to this exe
// 2) Compare with GitHub Releases latest
// 3) If newer: download zip, extract, copy files (skip z-updater.exe — self is locked)
// 4) Launch AntiZapret.exe

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shellapi.h>
#include <WinInet.h>
#include <TlHelp32.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace
{
	constexpr char kReleasesApiUrl[] =
		"https://api.github.com/repos/multimaks2/AntiZapret/releases/latest";
	constexpr wchar_t kAppExeName[] = L"AntiZapret.exe";
	constexpr wchar_t kUpdaterExeName[] = L"z-updater.exe";
	constexpr wchar_t kVersionFileName[] = L"version.txt";

	void Log(const std::string& message)
	{
		std::cout << message << std::endl;
	}

	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		return value.substr(start);
	}

	std::string NormalizeVersion(std::string value)
	{
		value = Trim(value);
		if (!value.empty() && (value[0] == 'v' || value[0] == 'V'))
			value.erase(0, 1);
		return value;
	}

	std::wstring GetExeDirectory()
	{
		wchar_t buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return L".";
		fs::path path(buffer);
		return path.parent_path().wstring();
	}

	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty())
			return {};
		const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (size <= 1)
			return {};
		std::string out(static_cast<size_t>(size - 1), '\0');
		WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
		return out;
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
			return {};
		const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (size <= 1)
			return {};
		std::wstring out(static_cast<size_t>(size - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
		return out;
	}

	std::string ReadVersionFile(const fs::path& appDir)
	{
		std::ifstream input(appDir / kVersionFileName);
		if (!input)
			return {};
		std::string line;
		while (std::getline(input, line))
		{
			const std::string trimmed = Trim(line);
			if (!trimmed.empty())
				return NormalizeVersion(trimmed);
		}
		return {};
	}

	void WriteVersionFile(const fs::path& appDir, const std::string& version)
	{
		std::ofstream output(appDir / kVersionFileName, std::ios::binary | std::ios::trunc);
		if (output)
			output << version << "\n";
	}

	// Returns true if remote > local (semver-ish dotted numbers).
	bool IsRemoteNewer(const std::string& localRaw, const std::string& remoteRaw)
	{
		const std::string local = NormalizeVersion(localRaw);
		const std::string remote = NormalizeVersion(remoteRaw);
		if (remote.empty())
			return false;
		if (local.empty())
			return true;
		if (local == remote)
			return false;

		auto nextPart = [](const std::string& s, size_t& pos) -> int {
			if (pos >= s.size())
				return 0;
			int value = 0;
			while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
			{
				value = value * 10 + (s[pos] - '0');
				++pos;
			}
			if (pos < s.size() && s[pos] == '.')
				++pos;
			return value;
		};

		size_t li = 0;
		size_t ri = 0;
		for (int i = 0; i < 8; ++i)
		{
			const int lv = nextPart(local, li);
			const int rv = nextPart(remote, ri);
			if (rv > lv)
				return true;
			if (rv < lv)
				return false;
			if (li >= local.size() && ri >= remote.size())
				break;
		}
		return false;
	}

	std::string HttpGetText(const char* url, DWORD timeoutMs = 20000)
	{
		std::string result;
		HINTERNET internet = InternetOpenA("AntiZapret-Updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
			return {};

		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char headers[] = "User-Agent: AntiZapret-Updater\r\nAccept: application/vnd.github+json\r\n";
		HINTERNET request = InternetOpenUrlA(
			internet,
			url,
			headers,
			static_cast<DWORD>(sizeof(headers) - 1),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
			0);
		if (!request)
		{
			InternetCloseHandle(internet);
			return {};
		}

		char buffer[8192];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			result.append(buffer, buffer + read);

		InternetCloseHandle(request);
		InternetCloseHandle(internet);
		return result;
	}

	bool DownloadFile(const char* url, const fs::path& dest, std::string& outError)
	{
		std::error_code ec;
		fs::create_directories(dest.parent_path(), ec);
		fs::remove(dest, ec);

		const std::wstring urlW = Utf8ToWide(url);
		const std::wstring destW = dest.wstring();
		std::wstring cmd =
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
			L"Invoke-WebRequest -Uri \\\"" + urlW + L"\\\" -OutFile \\\"" + destW
			+ L"\\\" -UserAgent 'AntiZapret-Updater' -TimeoutSec 300\"";

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::wstring full = L"cmd /c " + cmd;
		std::vector<wchar_t> buf(full.begin(), full.end());
		buf.push_back(L'\0');

		if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			outError = "CreateProcess download failed";
			return false;
		}
		WaitForSingleObject(pi.hProcess, 320000);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		if (code != 0 || !fs::exists(dest) || fs::file_size(dest, ec) < 1024)
		{
			outError = "Download failed";
			return false;
		}
		return true;
	}

	bool ExtractZip(const fs::path& zipPath, const fs::path& destDir)
	{
		std::error_code ec;
		fs::remove_all(destDir, ec);
		fs::create_directories(destDir, ec);

		const std::wstring zipW = zipPath.wstring();
		const std::wstring destW = destDir.wstring();
		std::wstring cmd =
			L"powershell -NoProfile -ExecutionPolicy Bypass -Command "
			L"\"Expand-Archive -Path \\\"" + zipW + L"\\\" -DestinationPath \\\"" + destW + L"\\\" -Force\"";

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};
		std::wstring full = L"cmd /c " + cmd;
		std::vector<wchar_t> buf(full.begin(), full.end());
		buf.push_back(L'\0');

		if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
			return false;
		WaitForSingleObject(pi.hProcess, 180000);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return code == 0;
	}

	bool ParseLatestRelease(const std::string& json, std::string& outTag, std::string& outZipUrl)
	{
		outTag.clear();
		outZipUrl.clear();

		const std::string tagKey = "\"tag_name\"";
		const size_t tagPos = json.find(tagKey);
		if (tagPos != std::string::npos)
		{
			const size_t q1 = json.find('"', tagPos + tagKey.size());
			const size_t q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
			if (q1 != std::string::npos && q2 != std::string::npos)
				outTag = NormalizeVersion(json.substr(q1 + 1, q2 - q1 - 1));
		}

		// Prefer first .zip browser_download_url
		size_t search = 0;
		while (true)
		{
			const std::string key = "\"browser_download_url\"";
			const size_t pos = json.find(key, search);
			if (pos == std::string::npos)
				break;
			const size_t q1 = json.find('"', pos + key.size());
			const size_t q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
			if (q1 == std::string::npos || q2 == std::string::npos)
				break;
			std::string url = json.substr(q1 + 1, q2 - q1 - 1);
			search = q2 + 1;
			for (char& ch : url)
			{
				if (ch >= 'A' && ch <= 'Z')
					ch = static_cast<char>(ch - 'A' + 'a');
			}
			if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".zip") == 0)
			{
				outZipUrl = json.substr(q1 + 1, q2 - q1 - 1); // original case
				break;
			}
		}

		// Fallback: source zipball_url
		if (outZipUrl.empty())
		{
			const std::string key = "\"zipball_url\"";
			const size_t pos = json.find(key);
			if (pos != std::string::npos)
			{
				const size_t q1 = json.find('"', pos + key.size());
				const size_t q2 = (q1 == std::string::npos) ? std::string::npos : json.find('"', q1 + 1);
				if (q1 != std::string::npos && q2 != std::string::npos)
					outZipUrl = json.substr(q1 + 1, q2 - q1 - 1);
			}
		}

		return !outTag.empty() && !outZipUrl.empty();
	}

	bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
	{
		return _wcsicmp(a.c_str(), b.c_str()) == 0;
	}

	bool ShouldSkipCopy(const fs::path& relative)
	{
		const std::wstring name = relative.filename().wstring();
		if (EqualsIgnoreCase(name, kUpdaterExeName))
			return true;
		if (EqualsIgnoreCase(name, L"z-updater.pdb"))
			return true;
		return false;
	}

	fs::path FindPayloadRoot(const fs::path& extractDir)
	{
		std::error_code ec;
		if (fs::exists(extractDir / kAppExeName))
			return extractDir;

		for (const auto& entry : fs::directory_iterator(extractDir, ec))
		{
			if (!entry.is_directory())
				continue;
			if (fs::exists(entry.path() / kAppExeName))
				return entry.path();
		}

		// Nested: first directory that contains any exe
		for (const auto& entry : fs::recursive_directory_iterator(extractDir, ec))
		{
			if (!entry.is_regular_file())
				continue;
			if (EqualsIgnoreCase(entry.path().filename().wstring(), kAppExeName))
				return entry.path().parent_path();
		}
		return extractDir;
	}

	bool CopyTreeSkipUpdater(const fs::path& fromRoot, const fs::path& toRoot, std::string& outError)
	{
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(fromRoot, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file())
				continue;

			const fs::path rel = fs::relative(entry.path(), fromRoot, ec);
			if (ec || ShouldSkipCopy(rel))
				continue;

			const fs::path dest = toRoot / rel;
			fs::create_directories(dest.parent_path(), ec);
			fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				outError = "Не удалось скопировать: " + WideToUtf8(dest.wstring()) + " (" + ec.message() + ")";
				return false;
			}
		}
		return true;
	}

	void TerminateProcessByName(const wchar_t* exeName)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return;

		PROCESSENTRY32W pe = { sizeof(pe) };
		const DWORD self = GetCurrentProcessId();
		if (Process32FirstW(snap, &pe))
		{
			do
			{
				if (pe.th32ProcessID == self)
					continue;
				if (!EqualsIgnoreCase(pe.szExeFile, exeName))
					continue;
				HANDLE proc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
				if (!proc)
					continue;
				TerminateProcess(proc, 0);
				WaitForSingleObject(proc, 8000);
				CloseHandle(proc);
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		Sleep(400);
	}

	bool LaunchApp(const fs::path& appDir)
	{
		const fs::path exe = appDir / kAppExeName;
		if (!fs::exists(exe))
		{
			Log("AntiZapret.exe не найден рядом с updater.");
			return false;
		}

		SHELLEXECUTEINFOW info = { sizeof(info) };
		info.fMask = SEE_MASK_NOCLOSEPROCESS;
		info.lpVerb = L"open";
		info.lpFile = exe.c_str();
		info.lpDirectory = appDir.c_str();
		info.nShow = SW_SHOWNORMAL;
		if (!ShellExecuteExW(&info))
		{
			Log("Не удалось запустить AntiZapret.exe");
			return false;
		}
		if (info.hProcess)
			CloseHandle(info.hProcess);
		return true;
	}

	bool CommandHasFlag(LPWSTR cmdLine, const wchar_t* flag)
	{
		if (!cmdLine || !flag)
			return false;
		return wcsstr(cmdLine, flag) != nullptr;
	}
}

int wmain(int /*argc*/, wchar_t** /*argv*/)
{
	SetConsoleOutputCP(CP_UTF8);
	const fs::path appDir = GetExeDirectory();
	Log(std::string("AntiZapret updater — ") + WideToUtf8(appDir.wstring()));

	const bool skipUpdate = CommandHasFlag(GetCommandLineW(), L"--noupdate");

	if (!skipUpdate)
	{
		const std::string localVersion = ReadVersionFile(appDir);
		Log(std::string("Локальная версия: ") + (localVersion.empty() ? "(нет version.txt)" : localVersion));

		Log("Проверка GitHub Releases...");
		const std::string json = HttpGetText(kReleasesApiUrl);
		std::string remoteTag;
		std::string zipUrl;
		if (!json.empty() && ParseLatestRelease(json, remoteTag, zipUrl))
		{
			Log(std::string("Релиз: ") + remoteTag);
			if (IsRemoteNewer(localVersion, remoteTag))
			{
				Log("Найдено обновление. Останавливаю AntiZapret...");
				TerminateProcessByName(kAppExeName);

				const fs::path stage = appDir / L".update";
				const fs::path zipPath = stage / L"release.zip";
				const fs::path extractDir = stage / L"extract";
				std::error_code ec;
				fs::create_directories(stage, ec);

				Log("Скачивание...");
				std::string downloadError;
				if (!DownloadFile(zipUrl.c_str(), zipPath, downloadError))
				{
					Log("Ошибка скачивания: " + downloadError);
					MessageBoxW(nullptr, L"Не удалось скачать обновление AntiZapret.", L"z-updater", MB_OK | MB_ICONWARNING);
				}
				else
				{
					Log("Распаковка...");
					if (!ExtractZip(zipPath, extractDir))
					{
						Log("Ошибка распаковки.");
						MessageBoxW(nullptr, L"Не удалось распаковать обновление.", L"z-updater", MB_OK | MB_ICONWARNING);
					}
					else
					{
						const fs::path payload = FindPayloadRoot(extractDir);
						Log("Копирование файлов (z-updater.exe пропускается)...");
						std::string copyError;
						if (!CopyTreeSkipUpdater(payload, appDir, copyError))
						{
							Log(copyError);
							MessageBoxA(nullptr, copyError.c_str(), "z-updater", MB_OK | MB_ICONWARNING);
						}
						else
						{
							WriteVersionFile(appDir, remoteTag);
							Log(std::string("Обновлено до ") + remoteTag);
						}
					}
				}

				fs::remove_all(stage, ec);
			}
			else
			{
				Log("Уже актуальная версия.");
			}
		}
		else
		{
			Log("Не удалось проверить обновления (сеть/API). Запускаю как есть.");
		}
	}
	else
	{
		Log("Пропуск проверки (--noupdate).");
	}

	if (!LaunchApp(appDir))
	{
		MessageBoxW(nullptr, L"Не найден AntiZapret.exe рядом с z-updater.exe", L"z-updater", MB_OK | MB_ICONERROR);
		return 1;
	}

	return 0;
}
