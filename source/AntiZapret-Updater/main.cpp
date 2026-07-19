// AntiZapret-Updater — gatekeeper for AntiZapret with ImGui progress UI.
// AntiZapret.exe launches this process; after check/update we start AntiZapret --updated.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shellapi.h>
#include <WinInet.h>
#include <TlHelp32.h>

#include "AntiZapret-Updater/updater_ui.h"

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

namespace
{
	constexpr char kReleasesApiUrl[] =
		"https://api.github.com/repos/multimaks2/AntiZapret/releases/latest";
	constexpr wchar_t kAppExeName[] = L"AntiZapret.exe";
	constexpr wchar_t kUpdaterExeName[] = L"AntiZapret-Updater.exe";
	constexpr wchar_t kVersionFileName[] = L"version.txt";

	void SetStatus(UpdaterUiState& ui, const std::string& status, float progress)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.status = status;
		if (progress >= 0.f)
			ui.progress = progress > 1.f ? 1.f : progress;
		ui.revision.fetch_add(1, std::memory_order_relaxed);
	}

	void Log(UpdaterUiState& ui, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.logs.push_back(message);
		if (ui.logs.size() > 200)
			ui.logs.erase(ui.logs.begin(), ui.logs.begin() + 50);
		ui.status = message;
		ui.revision.fetch_add(1, std::memory_order_relaxed);
	}

	void Fail(UpdaterUiState& ui, const std::string& error)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.error = error;
		ui.failed = true;
		ui.finished = true;
		ui.status = "Ошибка обновления";
		ui.logs.push_back(std::string("Ошибка: ") + error);
		ui.revision.fetch_add(1, std::memory_order_relaxed);
	}

	void FinishOk(UpdaterUiState& ui, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.failed = false;
		ui.finished = true;
		ui.progress = 1.f;
		ui.status = message;
		ui.logs.push_back(message);
		ui.launchRequested = true;
		ui.revision.fetch_add(1, std::memory_order_relaxed);
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
		return fs::path(buffer).parent_path().wstring();
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
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
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

	bool DownloadFile(const char* url, const fs::path& dest, UpdaterUiState& ui, std::string& outError)
	{
		std::error_code ec;
		fs::create_directories(dest.parent_path(), ec);
		fs::remove(dest, ec);

		HINTERNET internet = InternetOpenA("AntiZapret-Updater", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "InternetOpen failed";
			return false;
		}

		DWORD timeoutMs = 300000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char headers[] = "User-Agent: AntiZapret-Updater\r\nAccept: */*\r\n";
		HINTERNET request = InternetOpenUrlA(
			internet,
			url,
			headers,
			static_cast<DWORD>(sizeof(headers) - 1),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
			0);
		if (!request)
		{
			InternetCloseHandle(internet);
			outError = "Не удалось открыть URL загрузки";
			return false;
		}

		DWORD contentLen = 0;
		DWORD lenSize = sizeof(contentLen);
		HttpQueryInfoA(request, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLen, &lenSize, nullptr);

		HANDLE file = CreateFileW(
			dest.wstring().c_str(),
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			InternetCloseHandle(request);
			InternetCloseHandle(internet);
			outError = "Не удалось создать файл загрузки";
			return false;
		}

		std::vector<char> buffer(64 * 1024);
		DWORD read = 0;
		DWORD written = 0;
		unsigned long long total = 0;
		ULONGLONG lastUiTick = 0;
		while (InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read) && read > 0)
		{
			if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
			{
				CloseHandle(file);
				InternetCloseHandle(request);
				InternetCloseHandle(internet);
				outError = "Ошибка записи файла";
				return false;
			}
			total += read;

			const ULONGLONG now = GetTickCount64();
			if (now - lastUiTick >= 50)
			{
				lastUiTick = now;
				if (contentLen > 0)
				{
					const float frac = static_cast<float>(total) / static_cast<float>(contentLen);
					const float progress = 0.15f + frac * 0.55f;
					char status[128];
					snprintf(
						status,
						sizeof(status),
						"Скачивание... %.1f / %.1f МБ",
						total / (1024.0 * 1024.0),
						contentLen / (1024.0 * 1024.0));
					SetStatus(ui, status, progress);
				}
				else
				{
					char status[128];
					snprintf(status, sizeof(status), "Скачивание... %.1f МБ", total / (1024.0 * 1024.0));
					SetStatus(ui, status, 0.4f);
				}
				Sleep(0); // yield so UI thread can take the mutex / render
			}
		}

		CloseHandle(file);
		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (total < 1024)
		{
			outError = "Файл обновления слишком маленький или пустой";
			return false;
		}
		return true;
	}

	bool ExtractZip(const fs::path& zipPath, const fs::path& destDir, UpdaterUiState& ui)
	{
		std::error_code ec;
		fs::remove_all(destDir, ec);
		fs::create_directories(destDir, ec);
		SetStatus(ui, "Распаковка архива...", 0.75f);

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

		while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT)
		{
			float p = 0.75f;
			{
				std::lock_guard<std::mutex> lock(ui.mutex);
				p = ui.progress;
			}
			if (p < 0.88f)
				SetStatus(ui, "Распаковка архива...", p + 0.005f);
			Sleep(0);
		}

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
				outZipUrl = json.substr(q1 + 1, q2 - q1 - 1);
				break;
			}
		}

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
		if (EqualsIgnoreCase(name, L"AntiZapret-Updater.pdb"))
			return true;
		// Legacy name from older builds.
		if (EqualsIgnoreCase(name, L"z-updater.exe") || EqualsIgnoreCase(name, L"z-updater.pdb"))
			return true;
		return false;
	}

	void StopServiceByName(const wchar_t* serviceName)
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		SC_HANDLE service = OpenServiceW(manager, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (!service)
		{
			CloseServiceHandle(manager);
			return;
		}

		SERVICE_STATUS status = {};
		ControlService(service, SERVICE_CONTROL_STOP, &status);
		for (int i = 0; i < 40; ++i)
		{
			if (!QueryServiceStatus(service, &status))
				break;
			if (status.dwCurrentState == SERVICE_STOPPED)
				break;
			Sleep(100);
		}

		CloseServiceHandle(service);
		CloseServiceHandle(manager);
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
	}

	void PrepareForFileReplace(UpdaterUiState& ui)
	{
		Log(ui, "Останавливаю процессы и драйвер WinDivert...");
		TerminateProcessByName(kAppExeName);
		TerminateProcessByName(L"winws.exe");
		TerminateProcessByName(L"mihomo.exe");
		TerminateProcessByName(L"EnableLoopback.exe");
		StopServiceByName(L"WinDivert");
		StopServiceByName(L"WinDivert14");
		StopServiceByName(L"zapret");
		Sleep(600);
	}

	bool TryCopyOneFile(const fs::path& src, const fs::path& dest, std::error_code& ec, bool* outSkippedLocked)
	{
		if (outSkippedLocked)
			*outSkippedLocked = false;
		ec.clear();
		fs::create_directories(dest.parent_path(), ec);
		ec.clear();

		for (int attempt = 1; attempt <= 8; ++attempt)
		{
			ec.clear();
			fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
			if (!ec)
				return true;

			if (fs::exists(dest))
			{
				const fs::path bak = dest.wstring() + L".update-bak";
				std::error_code renameEc;
				fs::remove(bak, renameEc);
				renameEc.clear();
				fs::rename(dest, bak, renameEc);
				if (!renameEc)
				{
					ec.clear();
					fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
					if (!ec)
					{
						fs::remove(bak, renameEc);
						if (renameEc)
							MoveFileExW(bak.wstring().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
						return true;
					}
					fs::rename(bak, dest, renameEc);
				}
			}

			Sleep(250 * attempt);
		}

		// Driver/sys often stays locked even after stop — skip rather than abort whole update.
		const std::wstring ext = dest.extension().wstring();
		if (_wcsicmp(ext.c_str(), L".sys") == 0 || EqualsIgnoreCase(dest.filename().wstring(), L"WinDivert.dll"))
		{
			if (outSkippedLocked)
				*outSkippedLocked = true;
			ec.clear();
			return true;
		}
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

		for (const auto& entry : fs::recursive_directory_iterator(extractDir, ec))
		{
			if (!entry.is_regular_file())
				continue;
			if (EqualsIgnoreCase(entry.path().filename().wstring(), kAppExeName))
				return entry.path().parent_path();
		}
		return extractDir;
	}

	bool CopyTreeSkipUpdater(const fs::path& fromRoot, const fs::path& toRoot, UpdaterUiState& ui, std::string& outError)
	{
		std::error_code ec;
		std::vector<fs::path> files;
		for (const auto& entry : fs::recursive_directory_iterator(fromRoot, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file())
				continue;
			const fs::path rel = fs::relative(entry.path(), fromRoot, ec);
			if (ec || ShouldSkipCopy(rel))
				continue;
			files.push_back(entry.path());
		}

		if (files.empty())
		{
			outError = "В архиве нет файлов для установки";
			return false;
		}

		for (size_t i = 0; i < files.size(); ++i)
		{
			const fs::path& src = files[i];
			const fs::path rel = fs::relative(src, fromRoot, ec);
			const fs::path dest = toRoot / rel;
			bool skippedLocked = false;
			if (!TryCopyOneFile(src, dest, ec, &skippedLocked))
			{
				outError = "Не удалось скопировать: " + WideToUtf8(dest.wstring()) + " (" + ec.message() + ")";
				return false;
			}
			if (skippedLocked)
				Log(ui, "Пропущен занятый файл: " + WideToUtf8(rel.wstring()));
			const float progress = 0.88f + (static_cast<float>(i + 1) / static_cast<float>(files.size())) * 0.10f;
			SetStatus(ui, "Копирование файлов... " + WideToUtf8(rel.wstring()), progress);
		}
		return true;
	}

	bool LaunchApp(const fs::path& appDir, const std::wstring& extraArgs)
	{
		const fs::path exe = appDir / kAppExeName;
		if (!fs::exists(exe))
			return false;

		std::wstring cmd = L"\"" + exe.wstring() + L"\" --updated";
		if (!extraArgs.empty())
		{
			cmd.push_back(L' ');
			cmd += extraArgs;
		}

		AllowSetForegroundWindow(ASFW_ANY);

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_SHOWNORMAL;
		PROCESS_INFORMATION pi = {};
		std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
		mutableCmd.push_back(L'\0');

		// lpApplicationName = null: command line is the sole source of argv (reliable --updated).
		if (CreateProcessW(
				nullptr,
				mutableCmd.data(),
				nullptr,
				nullptr,
				FALSE,
				CREATE_NEW_PROCESS_GROUP,
				nullptr,
				appDir.c_str(),
				&si,
				&pi))
		{
			AllowSetForegroundWindow(pi.dwProcessId);
			WaitForInputIdle(pi.hProcess, 3000);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);
			return true;
		}

		std::wstring params = L"--updated";
		if (!extraArgs.empty())
		{
			params.push_back(L' ');
			params += extraArgs;
		}
		SHELLEXECUTEINFOW info = { sizeof(info) };
		info.fMask = SEE_MASK_NOCLOSEPROCESS;
		info.lpVerb = L"runas";
		info.lpFile = exe.c_str();
		info.lpParameters = params.c_str();
		info.lpDirectory = appDir.c_str();
		info.nShow = SW_SHOWNORMAL;
		if (!ShellExecuteExW(&info))
			return false;
		if (info.hProcess)
		{
			AllowSetForegroundWindow(GetProcessId(info.hProcess));
			WaitForInputIdle(info.hProcess, 3000);
			CloseHandle(info.hProcess);
		}
		return true;
	}

	bool CommandHasFlag(LPWSTR cmdLine, const wchar_t* flag)
	{
		if (!cmdLine || !flag)
			return false;
		return wcsstr(cmdLine, flag) != nullptr;
	}

	std::wstring CollectForwardArgs(LPWSTR cmdLine)
	{
		std::wstring out;
		int argc = 0;
		LPWSTR* argv = CommandLineToArgvW(cmdLine ? cmdLine : L"", &argc);
		if (!argv)
			return out;
		for (int i = 1; i < argc; ++i)
		{
			if (_wcsicmp(argv[i], L"--noupdate") == 0)
				continue;
			if (!out.empty())
				out.push_back(L' ');
			const bool needsQuotes = wcschr(argv[i], L' ') != nullptr;
			if (needsQuotes)
				out.push_back(L'"');
			out += argv[i];
			if (needsQuotes)
				out.push_back(L'"');
		}
		LocalFree(argv);
		return out;
	}

	void RunUpdateWorker(UpdaterUiState& ui, const fs::path& appDir, bool skipUpdate)
	{
		Log(ui, "Каталог: " + WideToUtf8(appDir.wstring()));
		SetStatus(ui, "Подготовка...", 0.02f);

		if (skipUpdate)
		{
			Log(ui, "Пропуск проверки (--noupdate).");
			FinishOk(ui, "Проверка пропущена");
			return;
		}

		const std::string localVersion = ReadVersionFile(appDir);
		Log(ui, std::string("Локальная версия: ") + (localVersion.empty() ? "(нет version.txt)" : localVersion));
		SetStatus(ui, "Проверка GitHub Releases...", 0.08f);

		const std::string json = HttpGetText(kReleasesApiUrl);
		std::string remoteTag;
		std::string zipUrl;
		if (json.empty() || !ParseLatestRelease(json, remoteTag, zipUrl))
		{
			Log(ui, "Не удалось проверить обновления (сеть/API).");
			FinishOk(ui, "Запуск без обновления");
			return;
		}

		Log(ui, std::string("Доступный релиз: ") + remoteTag);
		SetStatus(ui, "Сравнение версий...", 0.12f);

		if (!IsRemoteNewer(localVersion, remoteTag))
		{
			Log(ui, "Уже актуальная версия.");
			FinishOk(ui, "Обновление не требуется");
			return;
		}

		{
			std::lock_guard<std::mutex> lock(ui.mutex);
			ui.title = std::string("Обновление до ") + remoteTag;
		}

		Log(ui, "Найдено обновление. Останавливаю AntiZapret и связанные процессы...");
		PrepareForFileReplace(ui);
		SetStatus(ui, "Скачивание релиза...", 0.15f);

		const fs::path stage = appDir / L".update";
		const fs::path zipPath = stage / L"release.zip";
		const fs::path extractDir = stage / L"extract";
		std::error_code ec;
		fs::create_directories(stage, ec);

		Log(ui, "Скачивание: " + zipUrl);
		std::string downloadError;
		if (!DownloadFile(zipUrl.c_str(), zipPath, ui, downloadError))
		{
			fs::remove_all(stage, ec);
			Fail(ui, downloadError.empty() ? "Ошибка скачивания" : downloadError);
			return;
		}

		Log(ui, "Распаковка...");
		if (!ExtractZip(zipPath, extractDir, ui))
		{
			fs::remove_all(stage, ec);
			Fail(ui, "Не удалось распаковать архив");
			return;
		}

		const fs::path payload = FindPayloadRoot(extractDir);
		PrepareForFileReplace(ui);
		Log(ui, "Копирование файлов (AntiZapret-Updater.exe пропускается)...");
		std::string copyError;
		if (!CopyTreeSkipUpdater(payload, appDir, ui, copyError))
		{
			// One more stop+retry pass for stubborn locks (WinDivert.sys etc.)
			PrepareForFileReplace(ui);
			Log(ui, "Повторное копирование после остановки драйвера...");
			copyError.clear();
			if (!CopyTreeSkipUpdater(payload, appDir, ui, copyError))
			{
				fs::remove_all(stage, ec);
				Fail(ui, copyError.empty() ? "Ошибка копирования файлов" : copyError);
				return;
			}
		}

		WriteVersionFile(appDir, remoteTag);
		fs::remove_all(stage, ec);
		Log(ui, std::string("Успешно обновлено до ") + remoteTag);
		FinishOk(ui, std::string("Обновлено до ") + remoteTag);
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
	const fs::path appDir = GetExeDirectory();
	const bool skipUpdate = CommandHasFlag(GetCommandLineW(), L"--noupdate");
	const std::wstring forwardArgs = CollectForwardArgs(GetCommandLineW());

	UpdaterUiState ui;
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.status = "Запуск...";
		ui.progress = 0.f;
	}

	std::thread worker([&] { RunUpdateWorker(ui, appDir, skipUpdate); });

	const bool shouldLaunch = RunUpdaterUi(ui);

	if (worker.joinable())
		worker.join();

	if (shouldLaunch)
	{
		if (!LaunchApp(appDir, forwardArgs))
		{
			MessageBoxW(
				nullptr,
				L"Не найден AntiZapret.exe рядом с AntiZapret-Updater.exe",
				L"AntiZapret-Updater",
				MB_OK | MB_ICONERROR);
			return 1;
		}
	}

	return 0;
}
