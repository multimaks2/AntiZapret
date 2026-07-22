#include "AntiZapret-Installer/installer_ui.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Shellapi.h>
#include <WinInet.h>
#include <ShlObj.h>
#include <objbase.h>
#include <shobjidl.h>
#include <TlHelp32.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

namespace
{
	constexpr char kReleasesApiUrl[] =
		"https://api.github.com/repos/multimaks2/AntiZapret/releases/latest";
	constexpr wchar_t kAppExeName[] = L"AntiZapret.exe";

	void Bump(InstallerUiState& ui)
	{
		ui.revision.fetch_add(1, std::memory_order_relaxed);
	}

	void SetStatus(InstallerUiState& ui, const std::string& status, float progress)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.status = status;
		if (progress >= 0.f)
			ui.progress = progress > 1.f ? 1.f : progress;
		Bump(ui);
	}

	void Log(InstallerUiState& ui, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.logs.push_back(message);
		if (ui.logs.size() > 400)
			ui.logs.erase(ui.logs.begin(), ui.logs.begin() + 100);
		ui.status = message;
		Bump(ui);
	}

	void Fail(InstallerUiState& ui, const std::string& error)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.error = error;
		ui.installFailed = true;
		ui.installFinished = true;
		ui.status = "Ошибка установки";
		ui.logs.push_back(std::string("Ошибка: ") + error);
		Bump(ui);
	}

	void FinishOk(InstallerUiState& ui, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(ui.mutex);
		ui.installFailed = false;
		ui.installFinished = true;
		ui.progress = 1.f;
		ui.status = message;
		ui.logs.push_back(message);
		Bump(ui);
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

	std::wstring ToLower(std::wstring value)
	{
		for (wchar_t& ch : value)
			ch = static_cast<wchar_t>(towlower(ch));
		return value;
	}

	std::wstring NormalizePathKey(const fs::path& path)
	{
		std::error_code ec;
		fs::path abs = fs::weakly_canonical(path, ec);
		if (ec || abs.empty())
		{
			ec.clear();
			abs = fs::absolute(path, ec);
			if (ec || abs.empty())
				abs = path;
		}
		std::wstring s = ToLower(abs.wstring());
		std::replace(s.begin(), s.end(), L'/', L'\\');
		while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/'))
			s.pop_back();
		return s;
	}

	bool PathEqualsOrUnder(const std::wstring& path, const std::wstring& root)
	{
		if (root.empty() || path.empty())
			return false;
		if (path == root)
			return true;
		if (path.size() <= root.size())
			return false;
		if (path.compare(0, root.size(), root) != 0)
			return false;
		return path[root.size()] == L'\\';
	}

	bool IsDriveRoot(const std::wstring& path)
	{
		return (path.size() == 2 && path[1] == L':') ||
			(path.size() == 3 && path[1] == L':' && path[2] == L'\\');
	}

	std::wstring KnownFolderPath(REFKNOWNFOLDERID id)
	{
		PWSTR raw = nullptr;
		if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw)
			return {};
		std::wstring out = raw;
		CoTaskMemFree(raw);
		return NormalizePathKey(out);
	}

	std::wstring WindowsDirKey()
	{
		wchar_t windir[MAX_PATH] = {};
		const UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
		if (n == 0 || n >= MAX_PATH)
			return L"c:\\windows";
		return NormalizePathKey(windir);
	}

	bool IsForbiddenSystemLocation(const std::wstring& pathKey, std::string& outReason)
	{
		if (IsDriveRoot(pathKey))
		{
			outReason =
				"Нельзя устанавливать в корень диска. Укажите подпапку, например Program Files (x86)\\AntiZapret.";
			return true;
		}

		const std::wstring win = WindowsDirKey();
		if (PathEqualsOrUnder(pathKey, win))
		{
			outReason = "Нельзя устанавливать в каталог Windows или его подпапки.";
			return true;
		}

		const std::wstring pf = KnownFolderPath(FOLDERID_ProgramFiles);
		const std::wstring pf86 = KnownFolderPath(FOLDERID_ProgramFilesX86);
		if (!pf.empty() && pathKey == pf)
		{
			outReason = "Укажите подпапку внутри Program Files, а не сам каталог Program Files.";
			return true;
		}
		if (!pf86.empty() && pathKey == pf86)
		{
			outReason =
				"Укажите подпапку внутри Program Files (x86), а не сам каталог Program Files (x86).";
			return true;
		}
		if (!pf.empty() && PathEqualsOrUnder(pathKey, pf + L"\\windowsapps"))
		{
			outReason = "Нельзя устанавливать в WindowsApps.";
			return true;
		}

		if (pathKey.find(L"\\$recycle.bin") != std::wstring::npos ||
			pathKey.find(L"\\system volume information") != std::wstring::npos)
		{
			outReason = "Нельзя устанавливать в системные служебные каталоги.";
			return true;
		}

		return false;
	}

	bool DirectoryIsEmpty(const fs::path& dir)
	{
		std::error_code ec;
		if (!fs::exists(dir, ec) || ec)
			return true;
		if (!fs::is_directory(dir, ec) || ec)
			return false;
		const auto it = fs::directory_iterator(dir, ec);
		return !ec && it == fs::directory_iterator{};
	}

	bool LooksLikeAntiZapretInstall(const fs::path& dir)
	{
		std::error_code ec;
		if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
			return false;

		const fs::path markers[] = {
			dir / L"AntiZapret.exe",
			dir / L"AntiZapret-Updater.exe",
			dir / L"winws.exe",
			dir / L"bin" / L"winws.exe",
			dir / L"WinDivert.dll",
			dir / L"WinDivert64.sys",
			dir / L"bin" / L"WinDivert.dll",
			dir / L"bin" / L"WinDivert64.sys",
			dir / L"service.bat",
			dir / L"version.txt",
		};
		for (const fs::path& marker : markers)
		{
			if (fs::exists(marker, ec))
				return true;
		}

		for (const auto& entry : fs::directory_iterator(dir, ec))
		{
			if (ec)
				break;
			const std::wstring name = ToLower(entry.path().filename().wstring());
			if (name.find(L"windivert") != std::wstring::npos)
				return true;
			if (name == L"zapret" || name == L"bin" || name == L"lists")
				return true;
		}
		return false;
	}

	bool KillProcessesByName(const wchar_t* exeName)
	{
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return false;

		PROCESSENTRY32W pe = {};
		pe.dwSize = sizeof(pe);
		bool killed = false;
		if (Process32FirstW(snap, &pe))
		{
			do
			{
				if (_wcsicmp(pe.szExeFile, exeName) != 0)
					continue;
				if (pe.th32ProcessID == GetCurrentProcessId())
					continue;
				HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
				if (!proc)
					continue;
				if (TerminateProcess(proc, 1))
					killed = true;
				CloseHandle(proc);
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		return killed;
	}

	void StopServiceByName(const wchar_t* name)
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		SC_HANDLE service = OpenServiceW(manager, name, SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (!service)
		{
			CloseServiceHandle(manager);
			return;
		}

		SERVICE_STATUS status = {};
		ControlService(service, SERVICE_CONTROL_STOP, &status);
		for (int i = 0; i < 40; ++i)
		{
			SERVICE_STATUS_PROCESS ssp = {};
			DWORD needed = 0;
			if (!QueryServiceStatusEx(
					service,
					SC_STATUS_PROCESS_INFO,
					reinterpret_cast<LPBYTE>(&ssp),
					sizeof(ssp),
					&needed))
				break;
			if (ssp.dwCurrentState == SERVICE_STOPPED)
				break;
			Sleep(100);
		}
		CloseServiceHandle(service);
		CloseServiceHandle(manager);
	}

	// Same single-instance keys as Desktop\interface and AZ2 ProtocolHandler.
	constexpr wchar_t kSingleInstanceMutex[] = L"AntiZapret_SingleInstance_7F83B2E1";
	constexpr wchar_t kNewWindowClass[] = L"AntiZapretWindowClass"; // AZ2
	constexpr wchar_t kOldWindowClass[] = L"AntiZapret";            // Desktop\interface
	constexpr wchar_t kOldWindowTitle[] = L"AntiZapret";

	bool TerminatePid(DWORD pid)
	{
		if (pid == 0 || pid == GetCurrentProcessId())
			return false;
		HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
		if (!proc)
			return false;
		const bool ok = TerminateProcess(proc, 1) != FALSE;
		CloseHandle(proc);
		return ok;
	}

	// Close by FindWindow pattern (old: WM_CLOSE quits; new AZ2: WM_CLOSE → tray, then terminate).
	int CloseAntiZapretWindows(const wchar_t* className, const wchar_t* title)
	{
		int closed = 0;
		for (int attempt = 0; attempt < 12; ++attempt)
		{
			HWND hwnd = FindWindowW(className, title);
			if (!hwnd)
				break;

			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);
			PostMessageW(hwnd, WM_CLOSE, 0, 0);

			// Old interface exits on WM_CLOSE; new app only hides to tray.
			for (int wait = 0; wait < 15 && IsWindow(hwnd); ++wait)
				Sleep(100);

			if (IsWindow(hwnd) && pid != 0)
				TerminatePid(pid);

			++closed;
			Sleep(50);
		}
		return closed;
	}

	bool WaitSingleInstanceMutexReleased(DWORD timeoutMs)
	{
		HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kSingleInstanceMutex);
		if (!mutex)
			return true; // not held / not created

		const DWORD wait = WaitForSingleObject(mutex, timeoutMs);
		CloseHandle(mutex);
		return wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
	}

	void StopAntiZapretRuntime(InstallerUiState* ui)
	{
		auto note = [&](const std::string& msg) {
			if (ui)
				Log(*ui, msg);
		};

		note("Закрываю запущенный AntiZapret (окно + single-instance mutex)...");
		const int closedNew = CloseAntiZapretWindows(kNewWindowClass, nullptr);
		const int closedOld = CloseAntiZapretWindows(kOldWindowClass, kOldWindowTitle);
		if (closedNew > 0 || closedOld > 0)
		{
			char buf[128];
			snprintf(
				buf,
				sizeof(buf),
				"Отправлено закрытие окон: новое=%d, старое=%d",
				closedNew,
				closedOld);
			note(buf);
		}

		if (!WaitSingleInstanceMutexReleased(8000))
			note("Предупреждение: mutex AntiZapret ещё занят — принудительное завершение процессов");

		note("Останавливаю процессы AntiZapret / winws...");
		KillProcessesByName(L"AntiZapret.exe");
		KillProcessesByName(L"AntiZapret-Updater.exe");
		KillProcessesByName(L"winws.exe");
		WaitSingleInstanceMutexReleased(2000);
		Sleep(200);

		note("Останавливаю драйверы/службы WinDivert...");
		StopServiceByName(L"WinDivert");
		StopServiceByName(L"WinDivert14");
		Sleep(200);
	}

	bool ClearDirectoryContents(const fs::path& dir, std::string& outError)
	{
		std::error_code ec;
		if (!fs::exists(dir, ec))
			return true;
		if (!fs::is_directory(dir, ec))
		{
			outError = "Путь установки существует и не является папкой";
			return false;
		}

		for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
			{
				outError = "Не удалось прочитать содержимое папки: " + ec.message();
				return false;
			}
			ec.clear();
			fs::remove_all(entry.path(), ec);
			if (ec)
			{
				outError = "Не удалось удалить \"" + WideToUtf8(entry.path().wstring()) + "\": " + ec.message();
				return false;
			}
		}
		return true;
	}

	InstallPathCheck CheckInstallPathImpl(const std::string& pathUtf8)
	{
		InstallPathCheck check;
		const std::string trimmed = Trim(pathUtf8);
		if (trimmed.empty())
		{
			check.message = "Укажите папку установки.";
			return check;
		}

		const fs::path path = Utf8ToWide(trimmed);
		if (!path.is_absolute())
		{
			check.message =
				"Укажите полный абсолютный путь (например C:\\Program Files (x86)\\AntiZapret).";
			return check;
		}

		const std::wstring key = NormalizePathKey(path);
		std::string forbiddenReason;
		if (IsForbiddenSystemLocation(key, forbiddenReason))
		{
			check.message = forbiddenReason;
			return check;
		}

		std::error_code ec;
		if (fs::exists(path, ec) && fs::is_regular_file(path, ec))
		{
			check.message = "Указанный путь указывает на файл, а не на папку.";
			return check;
		}

		if (!DirectoryIsEmpty(path))
		{
			if (!LooksLikeAntiZapretInstall(path))
			{
				check.message =
					"Папка не пуста и не похожа на установку AntiZapret. "
					"Укажите пустую папку или каталог предыдущей версии AntiZapret.";
				return check;
			}
			check.willCleanExisting = true;
			check.ok = true;
			check.message =
				"В папке найдена предыдущая установка AntiZapret. "
				"Перед установкой процессы и драйверы WinDivert будут остановлены, папка будет очищена.";
			return check;
		}

		check.ok = true;
		check.message = "Путь допустим.";
		return check;
	}

	std::string HttpGetText(const char* url, DWORD timeoutMs = 15000)
	{
		std::string result;
		HINTERNET internet = InternetOpenA("AntiZapret-Installer", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
			return {};

		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char headers[] = "User-Agent: AntiZapret-Installer\r\nAccept: application/vnd.github+json\r\n";
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

	bool ParseWin32ZipRelease(const std::string& json, std::string& outTag, std::string& outZipName, std::string& outZipUrl)
	{
		outTag.clear();
		outZipName.clear();
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
			const std::string url = json.substr(q1 + 1, q2 - q1 - 1);
			search = q2 + 1;

			std::string lower = url;
			for (char& ch : lower)
			{
				if (ch >= 'A' && ch <= 'Z')
					ch = static_cast<char>(ch - 'A' + 'a');
			}
			if (lower.find("antizapret-") == std::string::npos || lower.find("-win32.zip") == std::string::npos)
				continue;

			outZipUrl = url;
			const size_t slash = url.find_last_of('/');
			outZipName = (slash == std::string::npos) ? url : url.substr(slash + 1);
			break;
		}

		if (outZipName.empty() && !outTag.empty())
			outZipName = "AntiZapret-" + outTag + "-win32.zip";

		return !outTag.empty() && !outZipName.empty() && !outZipUrl.empty();
	}

	bool DownloadFile(const char* url, const fs::path& dest, InstallerUiState& ui, std::string& outError)
	{
		std::error_code ec;
		fs::create_directories(dest.parent_path(), ec);
		fs::remove(dest, ec);

		HINTERNET internet = InternetOpenA("AntiZapret-Installer", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "InternetOpen failed";
			return false;
		}

		DWORD timeoutMs = 300000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		const char headers[] = "User-Agent: AntiZapret-Installer\r\nAccept: */*\r\n";
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
			if (now - lastUiTick >= 80)
			{
				lastUiTick = now;
				if (contentLen > 0)
				{
					const float frac = static_cast<float>(total) / static_cast<float>(contentLen);
					char status[160];
					snprintf(
						status,
						sizeof(status),
						"Скачивание архива... %.1f / %.1f МБ",
						total / (1024.0 * 1024.0),
						contentLen / (1024.0 * 1024.0));
					SetStatus(ui, status, 0.12f + frac * 0.48f);
				}
				else
				{
					char status[128];
					snprintf(status, sizeof(status), "Скачивание архива... %.1f МБ", total / (1024.0 * 1024.0));
					SetStatus(ui, status, 0.35f);
				}
				Sleep(0);
			}
		}

		CloseHandle(file);
		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (total < 1024)
		{
			outError = "Скачанный файл слишком маленький или пустой";
			return false;
		}

		char done[160];
		snprintf(done, sizeof(done), "Архив скачан (%.1f МБ): %s", total / (1024.0 * 1024.0), WideToUtf8(dest.wstring()).c_str());
		Log(ui, done);
		return true;
	}

	bool ExtractZip(const fs::path& zipPath, const fs::path& destDir, InstallerUiState& ui)
	{
		std::error_code ec;
		fs::remove_all(destDir, ec);
		fs::create_directories(destDir, ec);
		Log(ui, "Распаковываю архив в: " + WideToUtf8(destDir.wstring()));
		SetStatus(ui, "Распаковка архива...", 0.65f);

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

		while (WaitForSingleObject(pi.hProcess, 80) == WAIT_TIMEOUT)
		{
			float p = 0.65f;
			{
				std::lock_guard<std::mutex> lock(ui.mutex);
				p = ui.progress;
			}
			if (p < 0.74f)
				SetStatus(ui, "Распаковка архива...", p + 0.004f);
			Sleep(0);
		}

		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return code == 0;
	}

	bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
	{
		return _wcsicmp(a.c_str(), b.c_str()) == 0;
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

	bool CopyTree(const fs::path& fromRoot, const fs::path& toRoot, InstallerUiState& ui, std::string& outError)
	{
		std::error_code ec;
		std::vector<fs::path> files;
		for (const auto& entry : fs::recursive_directory_iterator(fromRoot, ec))
		{
			if (ec)
				break;
			if (!entry.is_regular_file())
				continue;
			files.push_back(entry.path());
		}

		if (files.empty())
		{
			outError = "В архиве нет файлов для установки";
			return false;
		}

		Log(ui, "Найдено файлов для копирования: " + std::to_string(files.size()));
		fs::create_directories(toRoot, ec);

		for (size_t i = 0; i < files.size(); ++i)
		{
			const fs::path& src = files[i];
			const fs::path rel = fs::relative(src, fromRoot, ec);
			const fs::path dest = toRoot / rel;
			fs::create_directories(dest.parent_path(), ec);
			ec.clear();
			fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				outError = "Не удалось скопировать: " + WideToUtf8(dest.wstring()) + " (" + ec.message() + ")";
				return false;
			}

			const float progress = 0.76f + (static_cast<float>(i + 1) / static_cast<float>(files.size())) * 0.14f;
			if ((i % 8) == 0 || i + 1 == files.size())
			{
				Log(ui, "Копирую: " + WideToUtf8(rel.wstring()));
				SetStatus(ui, "Копирование файлов...", progress);
			}
		}
		return true;
	}

	void WriteVersionFile(const fs::path& appDir, const std::string& version)
	{
		std::ofstream output(appDir / L"version.txt", std::ios::binary | std::ios::trunc);
		if (output)
			output << version << "\n";
	}

	bool CreateDesktopShortcut(const fs::path& exePath, InstallerUiState& ui, std::string& outError)
	{
		HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		const bool shouldUninit = (hrInit == S_OK);

		IShellLinkW* link = nullptr;
		HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
		if (FAILED(hr) || !link)
		{
			outError = "Не удалось создать ShellLink";
			if (shouldUninit)
				CoUninitialize();
			return false;
		}

		link->SetPath(exePath.wstring().c_str());
		link->SetWorkingDirectory(exePath.parent_path().wstring().c_str());
		link->SetDescription(L"AntiZapret");
		link->SetIconLocation(exePath.wstring().c_str(), 0);

		IPersistFile* file = nullptr;
		hr = link->QueryInterface(IID_PPV_ARGS(&file));
		if (FAILED(hr) || !file)
		{
			link->Release();
			outError = "Не удалось получить IPersistFile";
			if (shouldUninit)
				CoUninitialize();
			return false;
		}

		auto saveToDesktop = [&](int csidl, const char* label) -> bool {
			wchar_t desktop[MAX_PATH] = {};
			if (FAILED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, desktop)))
				return false;
			const fs::path shortcut = fs::path(desktop) / L"AntiZapret.lnk";
			Log(ui, std::string("Создаю ярлык (") + label + "): " + WideToUtf8(shortcut.wstring()));
			const HRESULT saveHr = file->Save(shortcut.wstring().c_str(), TRUE);
			return SUCCEEDED(saveHr);
		};

		// Prefer all-users desktop (installer runs elevated), fallback to current user.
		bool ok = saveToDesktop(CSIDL_COMMON_DESKTOPDIRECTORY, "общий рабочий стол");
		if (!ok)
			ok = saveToDesktop(CSIDL_DESKTOPDIRECTORY, "рабочий стол пользователя");

		file->Release();
		link->Release();
		if (shouldUninit)
			CoUninitialize();

		if (!ok)
			outError = "Не удалось сохранить ярлык на рабочий стол";
		return ok;
	}
}

InstallPathCheck CheckInstallPath(const std::string& pathUtf8)
{
	return CheckInstallPathImpl(pathUtf8);
}

void ResolveLatestRelease(InstallerUiState& state)
{
	const std::string json = HttpGetText(kReleasesApiUrl);
	std::string tag;
	std::string zipName;
	std::string zipUrl;
	if (json.empty() || !ParseWin32ZipRelease(json, tag, zipName, zipUrl))
	{
		state.releaseVersion.clear();
		state.releaseZipName = "AntiZapret-<версия>-win32.zip";
		state.releaseZipUrl.clear();
		return;
	}
	state.releaseVersion = tag;
	state.releaseZipName = zipName;
	state.releaseZipUrl = zipUrl;
}

void RunInstallWorker(InstallerUiState& state)
{
	std::string installPath;
	std::string zipName;
	std::string zipUrl;
	std::string version;
	bool wantShortcut = true;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		installPath = state.installPath;
		zipName = state.releaseZipName;
		zipUrl = state.releaseZipUrl;
		version = state.releaseVersion;
		wantShortcut = state.createDesktopShortcut;
		state.logs.clear();
		state.error.clear();
		state.installFailed = false;
		state.installFinished = false;
		state.progress = 0.02f;
		state.status = "Подготовка...";
		Bump(state);
	}

	Log(state, "Начинаю установку AntiZapret");
	Log(state, "Папка установки: " + installPath);

	const InstallPathCheck pathCheck = CheckInstallPath(installPath);
	if (!pathCheck.ok)
	{
		Fail(state, pathCheck.message.empty() ? "Недопустимая папка установки" : pathCheck.message);
		return;
	}
	if (pathCheck.willCleanExisting)
		Log(state, pathCheck.message);

	if (zipUrl.empty())
	{
		Log(state, "URL релиза не найден заранее — запрашиваю GitHub Releases...");
		ResolveLatestRelease(state);
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			zipName = state.releaseZipName;
			zipUrl = state.releaseZipUrl;
			version = state.releaseVersion;
		}
	}

	if (zipUrl.empty())
	{
		Fail(state, "Не удалось получить ссылку на AntiZapret-*-win32.zip с GitHub");
		return;
	}

	Log(state, "Релиз: " + (version.empty() ? std::string("(неизвестен)") : version));
	Log(state, "Архив: " + zipName);
	Log(state, "URL: " + zipUrl);
	SetStatus(state, "Подготовка папки установки...", 0.06f);

	const fs::path destRoot = Utf8ToWide(installPath);
	std::error_code ec;

	// Always try to quiet old runtime if markers exist or folder will be cleaned.
	if (pathCheck.willCleanExisting || LooksLikeAntiZapretInstall(destRoot))
	{
		StopAntiZapretRuntime(&state);
		Log(state, "Очищаю папку предыдущей установки...");
		std::string clearError;
		if (!ClearDirectoryContents(destRoot, clearError))
		{
			// Retry once after another stop pass — files may have been locked.
			StopAntiZapretRuntime(&state);
			Sleep(400);
			clearError.clear();
			if (!ClearDirectoryContents(destRoot, clearError))
			{
				Fail(state, clearError.empty() ? "Не удалось очистить папку установки" : clearError);
				return;
			}
		}
		Log(state, "Папка очищена");
	}
	else
	{
		// Even for empty/new dirs: stop leftover global WinDivert if AntiZapret was running from elsewhere.
		StopAntiZapretRuntime(&state);
	}

	SetStatus(state, "Создание каталогов...", 0.08f);

	const fs::path stage = fs::temp_directory_path() / L"AntiZapret-Installer";
	const fs::path zipPath = stage / Utf8ToWide(zipName.empty() ? "release.zip" : zipName);
	const fs::path extractDir = stage / L"extract";
	fs::create_directories(stage, ec);
	fs::create_directories(destRoot, ec);
	if (ec)
	{
		Fail(state, "Не удалось создать папку установки: " + installPath + " (" + ec.message() + ")");
		return;
	}
	Log(state, "Временный каталог: " + WideToUtf8(stage.wstring()));

	Log(state, "Скачиваю архив...");
	std::string downloadError;
	if (!DownloadFile(zipUrl.c_str(), zipPath, state, downloadError))
	{
		fs::remove_all(stage, ec);
		Fail(state, downloadError.empty() ? "Ошибка скачивания" : downloadError);
		return;
	}

	Log(state, "Распаковываю архив Expand-Archive...");
	if (!ExtractZip(zipPath, extractDir, state))
	{
		fs::remove_all(stage, ec);
		Fail(state, "Не удалось распаковать архив");
		return;
	}
	Log(state, "Архив успешно распакован");

	SetStatus(state, "Поиск файлов приложения...", 0.75f);
	const fs::path payload = FindPayloadRoot(extractDir);
	Log(state, "Корень приложения в архиве: " + WideToUtf8(payload.wstring()));
	if (!fs::exists(payload / kAppExeName))
	{
		fs::remove_all(stage, ec);
		Fail(state, "В архиве не найден AntiZapret.exe");
		return;
	}

	Log(state, "Копирую файлы в: " + installPath);
	std::string copyError;
	if (!CopyTree(payload, destRoot, state, copyError))
	{
		fs::remove_all(stage, ec);
		Fail(state, copyError.empty() ? "Ошибка копирования файлов" : copyError);
		return;
	}
	Log(state, "Файлы скопированы");

	if (!version.empty())
	{
		Log(state, "Записываю version.txt: " + version);
		WriteVersionFile(destRoot, version);
	}
	SetStatus(state, "Завершение...", 0.94f);

	if (wantShortcut)
	{
		std::string shortcutError;
		if (!CreateDesktopShortcut(destRoot / kAppExeName, state, shortcutError))
			Log(state, "Предупреждение: ярлык не создан (" + shortcutError + ")");
		else
			Log(state, "Ярлык на рабочем столе создан");
	}
	else
	{
		Log(state, "Создание ярлыка пропущено (снята галочка)");
	}

	Log(state, "Удаляю временные файлы...");
	fs::remove_all(stage, ec);
	SetStatus(state, "Готово", 0.99f);
	FinishOk(state, "Установка завершена успешно");
}

bool LaunchInstalledApp(const std::string& installPathUtf8)
{
	const fs::path exe = fs::path(Utf8ToWide(installPathUtf8)) / kAppExeName;
	if (!fs::exists(exe) || !fs::is_regular_file(exe))
		return false;

	// Keep wide strings alive for the whole call — parent_path().c_str() would dangle.
	const std::wstring exeW = exe.wstring();
	const std::wstring dirW = exe.parent_path().wstring();
	std::wstring cmdLine = L"\"" + exeW + L"\"";
	std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
	cmdBuf.push_back(L'\0');

	AllowSetForegroundWindow(ASFW_ANY);

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_SHOWNORMAL;
	PROCESS_INFORMATION pi = {};
	if (CreateProcessW(
			exeW.c_str(),
			cmdBuf.data(),
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			dirW.empty() ? nullptr : dirW.c_str(),
			&si,
			&pi))
	{
		AllowSetForegroundWindow(GetProcessId(pi.hProcess));
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	SHELLEXECUTEINFOW info = {};
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOCLOSEPROCESS;
	info.lpVerb = L"open";
	info.lpFile = exeW.c_str();
	info.lpDirectory = dirW.empty() ? nullptr : dirW.c_str();
	info.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&info))
		return false;
	if (info.hProcess)
	{
		AllowSetForegroundWindow(GetProcessId(info.hProcess));
		CloseHandle(info.hProcess);
	}
	return true;
}
