#include "tgproxy/tg_ws_proxy_manager.h"

#include "app/app_log.h"
#include "app/process_job.h"

#include "app/app_settings.h"
#include "zapret/zapret_connectivity.h"
#include "zapret/zapret_paths.h"

#include <TlHelp32.h>

#include <iphlpapi.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <cctype>
#include <map>
#include <shellapi.h>
#include <sstream>
#include <thread>
#include <vector>
#include <algorithm>

#pragma comment(lib, "iphlpapi.lib")

namespace
{
	namespace fs = std::filesystem;

	std::string TrimLine(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
			value.pop_back();
		return value;
	}

	bool IsProxyLayoutValid(const fs::path& root)
	{
		return fs::exists(root / "TgWsProxy_windows.exe")
			|| fs::exists(root / "proxy" / "tg_ws_proxy.py");
	}

	std::string FileSignature(const fs::path& path)
	{
		std::error_code ec;
		if (!fs::is_regular_file(path, ec))
			return "0";
		return std::to_string(fs::file_size(path, ec));
	}

	bool DepsMarkerMatches(const fs::path& root, const std::string& launcher)
	{
		const fs::path marker = root / ".antizapret_tg_deps";
		if (!fs::exists(marker))
			return false;

		std::ifstream input(marker, std::ios::binary);
		if (!input)
			return false;

		std::string stored((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		while (!stored.empty() && (stored.back() == '\r' || stored.back() == '\n'))
			stored.pop_back();

		std::ostringstream expected;
		expected << FileSignature(root / "requirements.txt") << "\n"
			<< FileSignature(root / "pyproject.toml") << "\n"
			<< launcher;

		return stored == expected.str();
	}

	std::wstring ProxyWorkDir()
	{
		return ZapretPaths::GetTgWsProxyDirectory();
	}

	std::wstring NativeToWide(const std::string& value)
	{
		if (value.empty())
			return {};

		const int length = MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, nullptr, 0);
		if (length <= 0)
			return ZapretPaths::Utf8ToWide(value);

		std::wstring result(static_cast<size_t>(length - 1), L'\0');
		MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, result.data(), length);
		return result;
	}

	bool RunProcessWide(
		const std::wstring& commandLine,
		const std::wstring& workDir,
		DWORD timeoutMs,
		DWORD creationFlags = CREATE_NO_WINDOW)
	{
		std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
		buffer.push_back(L'\0');

		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};

		if (!CreateProcessW(
				nullptr,
				buffer.data(),
				nullptr,
				nullptr,
				FALSE,
				creationFlags,
				nullptr,
				workDir.empty() ? nullptr : workDir.c_str(),
				&si,
				&pi))
		{
			return false;
		}

		WaitForSingleObject(pi.hProcess, timeoutMs);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return exitCode == 0;
	}

	void WriteDepsMarker(const fs::path& root, const std::string& launcher)
	{
		std::ofstream output(root / ".antizapret_tg_deps", std::ios::binary | std::ios::trunc);
		if (!output)
			return;
		output << FileSignature(root / "requirements.txt") << "\n"
			<< FileSignature(root / "pyproject.toml") << "\n"
			<< launcher;
	}

	std::string FormatPythonLauncherToken(const std::string& launcher)
	{
		if (launcher.empty())
			return launcher;
		if (launcher.front() == '"')
			return launcher;

		const bool looksLikePath = launcher.find('\\') != std::string::npos
			|| (launcher.size() > 1 && launcher[1] == ':');
		if (looksLikePath)
			return "\"" + launcher + "\"";

		return launcher;
	}

	void SyncTrayConfig(const AppSettings& settings)
	{
		wchar_t appData[MAX_PATH] = {};
		if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0)
			return;

		const fs::path configDir = fs::path(appData) / L"TgWsProxy";
		std::error_code ec;
		fs::create_directories(configDir, ec);

		const fs::path configFile = configDir / L"config.json";
		std::ostringstream json;
		json << "{\n"
			<< "  \"host\": \"" << settings.GetTgProxyHost() << "\",\n"
			<< "  \"port\": " << settings.GetTgProxyPort() << ",\n"
			<< "  \"secret\": \"" << settings.GetTgProxySecret() << "\",\n"
			<< "  \"dc_ip\": [\"2:149.154.167.220\", \"4:149.154.167.220\"],\n"
			<< "  \"cfproxy\": true,\n"
			<< "  \"cfproxy_priority\": true\n"
			<< "}\n";

		std::ofstream output(configFile, std::ios::binary | std::ios::trunc);
		if (output)
			output << json.str();
	}

	bool IsLoopbackOrAny(DWORD addr)
	{
		return addr == 0 || addr == 0x0100007F;  // 0.0.0.0 or 127.0.0.1
	}

	DWORD FindNamedProxyProcessPid()
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return 0;

		DWORD pid = 0;
		PROCESSENTRY32 entry = { sizeof(entry) };
		if (Process32First(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, L"TgWsProxy_windows.exe") == 0)
				{
					pid = entry.th32ProcessID;
					break;
				}
			} while (Process32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
		return pid;
	}

	void AppendPythonExeCandidate(const fs::path& path, std::vector<std::string>& out)
	{
		std::error_code ec;
		if (!fs::is_regular_file(path, ec))
			return;

		const std::string value = path.string();
		if (std::find(out.begin(), out.end(), value) == out.end())
			out.push_back(value);
	}

	void ScanPythonDirectory(const fs::path& dir, std::vector<std::string>& out, bool scanChildren)
	{
		std::error_code ec;
		if (!fs::exists(dir, ec))
			return;

		AppendPythonExeCandidate(dir / "python.exe", out);
		AppendPythonExeCandidate(dir / "python3.exe", out);

		if (!scanChildren)
			return;

		for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
		{
			if (ec)
				break;
			if (!entry.is_directory(ec))
				continue;
			AppendPythonExeCandidate(entry.path() / "python.exe", out);
			AppendPythonExeCandidate(entry.path() / "python3.exe", out);
		}
	}

	std::vector<std::string> CollectPythonLaunchers()
	{
		std::vector<std::string> launchers;

		wchar_t localAppData[MAX_PATH] = {};
		if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) != 0)
		{
			const fs::path localApp(localAppData);
			ScanPythonDirectory(localApp / "Python" / "bin", launchers, false);
			ScanPythonDirectory(localApp / "Python", launchers, true);
			ScanPythonDirectory(localApp / "Programs" / "Python", launchers, true);
		}

		wchar_t programFiles[MAX_PATH] = {};
		if (GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH) != 0)
			ScanPythonDirectory(fs::path(programFiles) / "Python", launchers, true);

		const char* pathLaunchers[] = { "py -3", "py", "python", "python3" };
		for (const char* launcher : pathLaunchers)
			launchers.push_back(launcher);

		return launchers;
	}

	std::string BuildPythonCmd(const std::string& launcher, const std::string& args)
	{
		return "cmd.exe /C \"" + launcher + " " + args + "\"";
	}

	bool RunPythonCmd(const std::string& launcher, const std::string& args, DWORD timeoutMs)
	{
		return RunProcessWide(NativeToWide(BuildPythonCmd(launcher, args)), ProxyWorkDir(), timeoutMs);
	}

	bool OpenExternalUrl(const char* url)
	{
		return reinterpret_cast<intptr_t>(ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
	}

	const char* EnvStateUserMessage(TgProxyEnvState state)
	{
		switch (state)
		{
		case TgProxyEnvState::Checking:
			return "Проверка окружения TG WS Proxy...";
		case TgProxyEnvState::MissingFiles:
			return "Папка tg-ws-proxy не найдена рядом с AntiZapret.exe.";
		case TgProxyEnvState::MissingPython:
			return "Python 3 не найден. Нажмите «Установить Python» на вкладке TG WS Proxy.";
		case TgProxyEnvState::MissingDependencies:
			return "Зависимости tg-ws-proxy не установлены. Нажмите «Установить зависимости».";
		case TgProxyEnvState::SettingUp:
			return "Подготовка TG WS Proxy...";
		case TgProxyEnvState::Ready:
		default:
			return "Окружение TG WS Proxy не готово.";
		}
	}
}

TgWsProxyManager::TgWsProxyManager()
{
	RefreshEnvironmentStateQuick();
}

void TgWsProxyManager::SetSettings(AppSettings* settings)
{
	m_settings = settings;
	if (!m_envProbeStarted)
	{
		m_envProbeStarted = true;
		BeginEnvironmentProbe();
	}
}

TgWsProxyManager::~TgWsProxyManager()
{
	if (m_startedByUs)
		Stop();
	else
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			CloseHandle(m_process);
			m_process = nullptr;
		}
	}
}

void TgWsProxyManager::Update(float deltaTime)
{
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			DWORD exitCode = 0;
			if (GetExitCodeProcess(m_process, &exitCode) && exitCode == STILL_ACTIVE)
				m_cachedRunning.store(true);
			else
			{
				ClearProcessHandleLocked();
				m_cachedRunning.store(false);
			}
		}
	}

	m_statusPollTimer += deltaTime;
	if (m_statusPollTimer >= 0.5f)
	{
		m_statusPollTimer = 0.f;
		RefreshRunningState();
	}

	TryFlushPendingStart();
}

void TgWsProxyManager::BeginEnvironmentProbe()
{
	if (m_envProbeRunning.exchange(true))
		return;

	std::thread([this]()
	{
		if (m_setupRunning.load())
		{
			m_envProbeRunning.store(false);
			return;
		}

		if (m_envState.load() != TgProxyEnvState::Ready && m_envState.load() != TgProxyEnvState::SettingUp)
		{
			m_envState.store(TgProxyEnvState::Checking);
			SetStatusMessage("Проверка окружения...");
		}

		RefreshEnvironmentState();
		if (m_envState.load() == TgProxyEnvState::Ready)
		{
			std::lock_guard<std::mutex> lock(m_messageMutex);
			if (m_statusMessage == "Проверка окружения..."
				|| m_statusMessage == "Проверка окружения TG WS Proxy...")
			{
				m_statusMessage.clear();
			}
		}
		m_envProbeRunning.store(false);
		TryFlushPendingStart();
	}).detach();
}

void TgWsProxyManager::RefreshEnvironmentStateQuick()
{
	if (m_setupRunning.load())
		return;

	const fs::path root = fs::path(ZapretPaths::GetTgWsProxyDirectory());
	if (!IsProxyLayoutValid(root))
	{
		m_envState.store(TgProxyEnvState::MissingFiles);
		return;
	}

	if (fs::exists(root / "TgWsProxy_windows.exe"))
		m_envState.store(TgProxyEnvState::Ready);
}

void TgWsProxyManager::SetStatusMessage(const std::string& message)
{
	std::lock_guard<std::mutex> lock(m_messageMutex);
	m_statusMessage = message;
	if (!message.empty())
		AppLog::Instance().Append(LogSource::Telegram, message);
}

const std::string& TgWsProxyManager::GetStatusMessage() const
{
	std::lock_guard<std::mutex> lock(m_messageMutex);
	return m_statusMessage;
}

const char* TgWsProxyManager::GetEnvStatusLabel() const
{
	if (m_envProbeRunning.load() || m_envState.load() == TgProxyEnvState::Checking)
		return "Проверка...";
	if (m_setupRunning.load() || m_envState.load() == TgProxyEnvState::SettingUp)
		return "Подготовка...";

	switch (m_envState.load())
	{
	case TgProxyEnvState::Ready:
		return "Ок";
	case TgProxyEnvState::MissingFiles:
		return "Файлы не найдены";
	case TgProxyEnvState::MissingPython:
		return "Нет Python";
	case TgProxyEnvState::MissingDependencies:
		return "Нет зависимостей";
	default:
		return "—";
	}
}

bool TgWsProxyManager::IsRunning() const
{
	return m_cachedRunning.load();
}

void TgWsProxyManager::ClearProcessHandleLocked()
{
	if (m_process)
	{
		CloseHandle(m_process);
		m_process = nullptr;
	}
	m_startedByUs = false;
	SetStatusMessage("TG WS Proxy остановлен.");
}

DWORD TgWsProxyManager::FindListenerPidForProxy() const
{
	if (!m_settings)
		return 0;

	const int port = m_settings->GetTgProxyPort();
	if (port <= 0 || port > 65535)
		return 0;

	const USHORT targetPort = htons(static_cast<USHORT>(port));
	DWORD tableSize = 0;
	if (GetExtendedTcpTable(nullptr, &tableSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != ERROR_INSUFFICIENT_BUFFER)
		return 0;

	std::vector<BYTE>& buffer = m_tcpTableBuffer;
	if (buffer.size() < tableSize)
		buffer.resize(tableSize);

	tableSize = static_cast<DWORD>(buffer.size());
	const PMIB_TCPTABLE_OWNER_PID table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
	if (GetExtendedTcpTable(table, &tableSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR)
		return 0;

	for (DWORD i = 0; i < table->dwNumEntries; ++i)
	{
		const MIB_TCPROW_OWNER_PID& row = table->table[i];
		if (row.dwState != MIB_TCP_STATE_LISTEN)
			continue;
		if (row.dwLocalPort != targetPort)
			continue;
		if (!IsLoopbackOrAny(row.dwLocalAddr))
			continue;
		if (row.dwOwningPid != 0)
			return row.dwOwningPid;
	}

	return 0;
}

void TgWsProxyManager::RefreshRunningState()
{
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			DWORD exitCode = 0;
			if (GetExitCodeProcess(m_process, &exitCode) && exitCode == STILL_ACTIVE)
			{
				m_cachedRunning.store(true);
				return;
			}
			ClearProcessHandleLocked();
		}
	}

	DWORD pid = FindNamedProxyProcessPid();
	if (pid == 0)
		pid = FindListenerPidForProxy();
	if (pid == 0)
	{
		m_cachedRunning.store(false);
		return;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
	if (!process)
	{
		m_cachedRunning.store(true);
		return;
	}

	DWORD exitCode = 0;
	if (!GetExitCodeProcess(process, &exitCode) || exitCode != STILL_ACTIVE)
	{
		CloseHandle(process);
		m_cachedRunning.store(false);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
			CloseHandle(m_process);
		m_process = process;
		m_startedByUs = false;
	}

	m_cachedRunning.store(true);
	SetStatusMessage("TG WS Proxy уже запущен (обнаружен внешний процесс).");
}

bool TgWsProxyManager::WaitForProxyRunning(int timeoutMs)
{
	const int stepMs = 200;
	for (int elapsed = 0; elapsed < timeoutMs; elapsed += stepMs)
	{
		RefreshRunningState();
		if (IsRunning())
			return true;
		Sleep(stepMs);
	}

	RefreshRunningState();
	return IsRunning();
}

void TgWsProxyManager::TryFlushPendingStart()
{
	if (!m_pendingStart.load())
		return;

	RefreshRunningState();
	if (IsRunning())
	{
		m_pendingStart.store(false);
		return;
	}

	if (m_setupRunning.load() || m_asyncOpRunning.load() || m_envProbeRunning.load())
		return;

	const TgProxyEnvState env = m_envState.load();
	if (env == TgProxyEnvState::Checking)
		return;

	const bool openTelegram = m_pendingOpenTelegram.load();
	m_pendingStart.store(false);

	if (env == TgProxyEnvState::Ready)
	{
		if (!LaunchIfReady(openTelegram))
		{
			RefreshRunningState();
			if (!IsRunning() && m_lastError.empty())
				m_lastError = "Не удалось запустить TG WS Proxy.";
		}
		return;
	}

	if (env == TgProxyEnvState::MissingFiles || env == TgProxyEnvState::MissingPython)
	{
		m_lastError = EnvStateUserMessage(env);
		SetStatusMessage(m_lastError);
		return;
	}

	std::thread([this, openTelegram]() { RunSetupAndStart(openTelegram); }).detach();
	SetStatusMessage("Подготовка TG WS Proxy...");
}

void TgWsProxyManager::RefreshEnvironmentState()
{
	const fs::path root = fs::path(ZapretPaths::GetTgWsProxyDirectory());
	if (!IsProxyLayoutValid(root))
	{
		m_envState.store(TgProxyEnvState::MissingFiles);
		return;
	}

	if (fs::exists(root / "TgWsProxy_windows.exe"))
	{
		m_envState.store(TgProxyEnvState::Ready);
		return;
	}

	std::string launcher;
	if (!DetectPythonLauncher(launcher))
	{
		m_envState.store(TgProxyEnvState::MissingPython);
		return;
	}

	m_pythonLauncher = launcher;
	if (!RunPythonCmd(launcher, "-m pip --version", 30000))
	{
		m_envState.store(TgProxyEnvState::MissingDependencies);
		return;
	}

	if (!DepsMarkerMatches(root, launcher)
		&& (fs::exists(root / "requirements.txt") || fs::exists(root / "pyproject.toml")))
	{
		m_envState.store(TgProxyEnvState::MissingDependencies);
		return;
	}

	if (!CanRunProxyModule(launcher))
	{
		m_envState.store(TgProxyEnvState::MissingDependencies);
		return;
	}

	m_envState.store(TgProxyEnvState::Ready);
}

bool TgWsProxyManager::CommandSucceeded(const std::string& commandLine, const std::wstring& workDir, DWORD timeoutMs)
{
	return RunProcessWide(NativeToWide(commandLine), workDir, timeoutMs);
}

bool TgWsProxyManager::DetectPythonLauncher(std::string& outLauncher)
{
	if (!m_pythonLauncher.empty() && RunPythonCmd(m_pythonLauncher, "--version", 5000))
	{
		outLauncher = m_pythonLauncher;
		return true;
	}

	for (const std::string& launcher : CollectPythonLaunchers())
	{
		if (RunPythonCmd(launcher, "--version", 5000))
		{
			outLauncher = launcher;
			return true;
		}
	}
	outLauncher.clear();
	return false;
}

bool TgWsProxyManager::CanRunProxyModule(const std::string& launcher) const
{
	if (launcher.empty())
		return false;

	return RunPythonCmd(launcher, "-c \"import proxy.tg_ws_proxy\"", 30000);
}

bool TgWsProxyManager::EnsureDependencies(std::string& outError)
{
	const fs::path root = fs::path(ZapretPaths::GetTgWsProxyDirectory());
	std::string launcher = m_pythonLauncher;
	if (launcher.empty() && !DetectPythonLauncher(launcher))
	{
		outError = "Python 3 не найден.";
		return false;
	}
	m_pythonLauncher = launcher;

	if (DepsMarkerMatches(root, launcher) && CanRunProxyModule(launcher))
		return true;

	if (!RunPythonCmd(launcher, "-m pip --version", 30000))
	{
		RunPythonCmd(launcher, "-m ensurepip --upgrade", 120000);
		if (!RunPythonCmd(launcher, "-m pip --version", 30000))
		{
			outError = "pip недоступен.";
			return false;
		}
	}

	auto runPip = [&](const std::string& args) -> bool
	{
		if (RunPythonCmd(launcher, "-m pip install --disable-pip-version-check --no-input " + args, 300000))
			return true;
		return RunPythonCmd(launcher, "-m pip install --disable-pip-version-check " + args, 300000);
	};

	if (fs::exists(root / "requirements.txt"))
	{
		if (!runPip("-r \"" + (root / "requirements.txt").string() + "\""))
		{
			outError = "Не удалось установить зависимости из requirements.txt.";
			return false;
		}
		WriteDepsMarker(root, launcher);
		return true;
	}

	if (fs::exists(root / "pyproject.toml"))
	{
		if (!runPip("-e ."))
		{
			outError = "Не удалось установить пакет tg-ws-proxy.";
			return false;
		}
		WriteDepsMarker(root, launcher);
		return true;
	}

	return true;
}

void TgWsProxyManager::KillProcessTree(DWORD rootPid)
{
	if (rootPid == 0)
		return;

	std::vector<std::pair<DWORD, DWORD>> processes;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32 entry = { sizeof(entry) };
	if (Process32First(snapshot, &entry))
	{
		do
		{
			processes.push_back({ entry.th32ProcessID, entry.th32ParentProcessID });
		} while (Process32Next(snapshot, &entry));
	}
	CloseHandle(snapshot);

	std::map<DWORD, std::vector<DWORD>> byParent;
	for (const auto& pair : processes)
		byParent[pair.second].push_back(pair.first);

	std::vector<DWORD> order;
	std::function<void(DWORD)> collect = [&](DWORD pid)
	{
		const auto it = byParent.find(pid);
		if (it != byParent.end())
		{
			for (DWORD child : it->second)
				collect(child);
		}
		order.push_back(pid);
	};
	collect(rootPid);

	for (DWORD pid : order)
	{
		HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
		if (!process)
			continue;
		TerminateProcess(process, 0);
		WaitForSingleObject(process, 2000);
		CloseHandle(process);
	}
}

void TgWsProxyManager::Stop()
{
	DWORD pid = 0;
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			pid = GetProcessId(m_process);
			TerminateProcess(m_process, 0);
			ClearProcessHandleLocked();
		}
	}

	if (pid != 0)
		KillProcessTree(pid);

	const DWORD listenerPid = FindListenerPidForProxy();
	if (listenerPid != 0 && listenerPid != pid)
		KillProcessTree(listenerPid);

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot != INVALID_HANDLE_VALUE)
	{
		PROCESSENTRY32 entry = { sizeof(entry) };
		if (Process32First(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, L"TgWsProxy_windows.exe") == 0)
				{
					HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
					if (process)
					{
						TerminateProcess(process, 0);
						CloseHandle(process);
					}
				}
			} while (Process32Next(snapshot, &entry));
		}
		CloseHandle(snapshot);
	}

	m_lastError.clear();
	SetStatusMessage("TG WS Proxy остановлен.");
	m_cachedRunning.store(false);
}

bool TgWsProxyManager::LaunchProxyProcess(const std::string& secret)
{
	if (!m_settings)
		return false;

	RefreshRunningState();
	if (IsRunning())
	{
		m_lastError.clear();
		SetStatusMessage("TG WS Proxy уже запущен.");
		return true;
	}

	const fs::path root = fs::path(ZapretPaths::GetTgWsProxyDirectory());
	const fs::path proxyExe = root / "TgWsProxy_windows.exe";
	const fs::path proxyScript = root / "proxy" / "tg_ws_proxy.py";
	const std::wstring workDir = ProxyWorkDir();

	const std::string host = m_settings->GetTgProxyHost();
	const int port = m_settings->GetTgProxyPort();

	STARTUPINFOW siGui = { sizeof(siGui) };
	STARTUPINFOW siHidden = { sizeof(siHidden) };
	siHidden.dwFlags = STARTF_USESHOWWINDOW;
	siHidden.wShowWindow = SW_HIDE;

	auto tryLaunch = [&](wchar_t* cmdLine, DWORD flags, STARTUPINFOW& siRef) -> HANDLE
	{
		PROCESS_INFORMATION pi = {};
		if (!ProcessJob::CreateInJob(nullptr, cmdLine, nullptr, nullptr, FALSE, flags, nullptr, workDir.c_str(), &siRef, &pi))
			return nullptr;
		CloseHandle(pi.hThread);
		Sleep(800);
		DWORD exitCode = 0;
		if (!GetExitCodeProcess(pi.hProcess, &exitCode) || exitCode != STILL_ACTIVE)
		{
			CloseHandle(pi.hProcess);
			return nullptr;
		}
		return pi.hProcess;
	};

	HANDLE launched = nullptr;
	if (fs::exists(proxyExe))
	{
		std::wstring cmdLine = L"\"" + proxyExe.wstring() + L"\"";
		std::vector<wchar_t> buffer(cmdLine.begin(), cmdLine.end());
		buffer.push_back(L'\0');
		launched = tryLaunch(buffer.data(), 0, siGui);
	}

	if (!launched && fs::exists(proxyScript))
	{
		if (WaitForProxyRunning(500))
		{
			m_lastError.clear();
			SetStatusMessage("TG WS Proxy запущен.");
			return true;
		}

		std::string launcher = m_pythonLauncher;
		if (launcher.empty())
			DetectPythonLauncher(launcher);

		std::ostringstream args;
		args << FormatPythonLauncherToken(launcher)
			<< " -m proxy.tg_ws_proxy"
			<< " --host " << host
			<< " --port " << port
			<< " --secret " << secret
			<< " --dc-ip 2:149.154.167.220"
			<< " --dc-ip 4:149.154.167.220";

		wchar_t sysDir[MAX_PATH] = {};
		if (GetSystemDirectoryW(sysDir, MAX_PATH) != 0)
		{
			const std::wstring fullCmd =
				std::wstring(L"\"") + sysDir + L"\\cmd.exe\" /C \"" + NativeToWide(args.str()) + L"\"";
			std::vector<wchar_t> buffer(fullCmd.begin(), fullCmd.end());
			buffer.push_back(L'\0');
			launched = tryLaunch(buffer.data(), CREATE_NO_WINDOW, siHidden);
		}
	}

	if (!launched)
	{
		if (WaitForProxyRunning(5000))
		{
			m_lastError.clear();
			SetStatusMessage("TG WS Proxy запущен.");
			return true;
		}

		std::string checkLauncher = m_pythonLauncher;
		if (checkLauncher.empty())
			DetectPythonLauncher(checkLauncher);
		if (!checkLauncher.empty() && !CanRunProxyModule(checkLauncher))
		{
			m_envState.store(TgProxyEnvState::MissingDependencies);
			m_lastError = "Зависимости tg-ws-proxy не установлены для текущего Python. Нажмите «Установить зависимости».";
		}
		else
		{
			m_lastError = "Не удалось запустить tg-ws-proxy.";
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
			CloseHandle(m_process);
		m_process = launched;
		m_startedByUs = true;
	}

	m_lastError.clear();
	SetStatusMessage("TG WS Proxy запущен.");
	m_cachedRunning.store(true);
	return true;
}

bool TgWsProxyManager::LaunchIfReady(bool openTelegram)
{
	RefreshRunningState();
	if (IsRunning())
	{
		if (openTelegram)
			OpenTelegramLink();
		return true;
	}

	if (!m_settings)
		return false;

	if (m_envState.load() != TgProxyEnvState::Ready)
		return false;

	const std::string secret = m_settings->EnsureTgProxySecret();
	SyncTrayConfig(*m_settings);

	if (!LaunchProxyProcess(secret))
	{
		RefreshRunningState();
		if (IsRunning())
		{
			if (openTelegram)
				OpenTelegramLink();
			return true;
		}
		return false;
	}

	if (openTelegram)
		OpenTelegramLink();

	return true;
}

bool TgWsProxyManager::Start(bool openTelegram)
{
	RefreshRunningState();
	if (IsRunning())
	{
		if (openTelegram)
			OpenTelegramLink();
		return true;
	}

	if (m_setupRunning.load())
	{
		m_pendingStart.store(true);
		m_pendingOpenTelegram.store(openTelegram);
		SetStatusMessage("Подготовка TG WS Proxy...");
		return false;
	}

	if (m_envState.load() == TgProxyEnvState::Ready)
		return LaunchIfReady(openTelegram);

	if (m_envState.load() == TgProxyEnvState::MissingFiles
		|| m_envState.load() == TgProxyEnvState::MissingPython)
	{
		m_lastError = EnvStateUserMessage(m_envState.load());
		return false;
	}

	if (m_envState.load() == TgProxyEnvState::Checking)
	{
		m_pendingStart.store(true);
		m_pendingOpenTelegram.store(openTelegram);
		if (!m_envProbeRunning.load())
			BeginEnvironmentProbe();
		SetStatusMessage("Проверка окружения...");
		return false;
	}

	std::thread([this, openTelegram]() { RunSetupAndStart(openTelegram); }).detach();
	SetStatusMessage("Подготовка TG WS Proxy...");
	return false;
}

void TgWsProxyManager::RequestStart(bool openTelegram)
{
	std::thread([this, openTelegram]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_asyncOpRunning.store(true);
		Start(openTelegram);
		m_asyncOpRunning.store(false);
	}).detach();
}

void TgWsProxyManager::RequestStop()
{
	std::thread([this]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_asyncOpRunning.store(true);
		Stop();
		m_asyncOpRunning.store(false);
	}).detach();
}

void TgWsProxyManager::RunSetupAndStart(bool openTelegram)
{
	m_setupRunning.store(true);
	m_envState.store(TgProxyEnvState::SettingUp);
	SetStatusMessage("Подготовка TG WS Proxy...");

	const fs::path root = fs::path(ZapretPaths::GetTgWsProxyDirectory());
	if (!IsProxyLayoutValid(root))
	{
		m_lastError = "Папка tg-ws-proxy не найдена рядом с AntiZapret.exe.";
		SetStatusMessage(m_lastError);
		m_envState.store(TgProxyEnvState::MissingFiles);
		m_setupRunning.store(false);
		return;
	}

	if (fs::exists(root / "TgWsProxy_windows.exe"))
	{
		m_envState.store(TgProxyEnvState::Ready);
		SetStatusMessage("Запуск TG WS Proxy...");
		if (!LaunchIfReady(openTelegram))
		{
			RefreshRunningState();
			if (!IsRunning() && m_lastError.empty())
				m_lastError = "Не удалось запустить TG WS Proxy.";
		}
		m_setupRunning.store(false);
		return;
	}

	if (fs::exists(root / "proxy" / "tg_ws_proxy.py"))
	{
		SetStatusMessage("Поиск Python...");
		if (!DetectPythonLauncher(m_pythonLauncher))
		{
			std::string message;
			if (OpenPythonInstaller(message))
			{
				m_lastError = "Python 3 не найден. " + message;
				SetStatusMessage(message);
			}
			else
			{
				m_lastError = message;
				SetStatusMessage(message);
			}
			m_envState.store(TgProxyEnvState::MissingPython);
			m_setupRunning.store(false);
			return;
		}

		SetStatusMessage("Проверка pip...");
		if (!RunPythonCmd(m_pythonLauncher, "-m pip --version", 30000))
		{
			RunPythonCmd(m_pythonLauncher, "-m ensurepip --upgrade", 120000);
			if (!RunPythonCmd(m_pythonLauncher, "-m pip --version", 30000))
			{
				m_lastError = "pip недоступен.";
				SetStatusMessage(m_lastError);
				m_envState.store(TgProxyEnvState::MissingDependencies);
				m_setupRunning.store(false);
				return;
			}
		}

		if (DepsMarkerMatches(root, m_pythonLauncher) && CanRunProxyModule(m_pythonLauncher))
		{
			SetStatusMessage("Зависимости уже установлены, запуск TG WS Proxy...");
		}
		else
		{
			SetStatusMessage("Установка зависимостей tg-ws-proxy...");
			std::string error;
			if (!EnsureDependencies(error))
			{
				m_lastError = error;
				SetStatusMessage(error.empty() ? "Не удалось установить зависимости." : error);
				m_envState.store(TgProxyEnvState::MissingDependencies);
				m_setupRunning.store(false);
				return;
			}
		}
	}

	RefreshEnvironmentState();
	SetStatusMessage("Запуск TG WS Proxy...");
	if (!LaunchIfReady(openTelegram))
	{
		RefreshRunningState();
		if (!IsRunning() && m_lastError.empty())
			m_lastError = "Не удалось запустить TG WS Proxy.";
	}

	m_setupRunning.store(false);
}

bool TgWsProxyManager::OpenPythonInstaller(std::string& outMessage)
{
	if (OpenExternalUrl(kPythonManagerStoreUrl))
	{
		outMessage = "Открыт Microsoft Store. Установите Python Install Manager и нажмите кнопку снова.";
		return true;
	}

	if (OpenExternalUrl(kPythonManagerUrl))
	{
		outMessage = "Не удалось открыть Microsoft Store. Открыта прямая загрузка установщика Python.";
		return true;
	}

	outMessage = "Не удалось открыть Microsoft Store или загрузить установщик Python.";
	return false;
}

void TgWsProxyManager::HandlePrimaryAction(bool openTelegramAfterStart)
{
	if (IsRunning())
	{
		if (m_settings && m_settings->GetAutoStartTgProxyWithAntiZapret())
			m_settings->SetTgAutoStartSuppressed(true);
		RequestStop();
		return;
	}

	if (m_settings)
		m_settings->SetTgAutoStartSuppressed(false);

	const TgProxyEnvState state = m_envState.load();
	if (state == TgProxyEnvState::MissingPython)
	{
		std::string message;
		OpenPythonInstaller(message);
		SetStatusMessage(message);
		return;
	}

	if (state == TgProxyEnvState::MissingFiles)
	{
		m_lastError = "Скопируйте папку tg-ws-proxy в каталог с AntiZapret.exe.";
		SetStatusMessage(m_lastError);
		return;
	}

	if (state == TgProxyEnvState::SettingUp || m_setupRunning.load() || m_envProbeRunning.load())
		return;

	std::thread([this, openTelegramAfterStart]() { RunSetupAndStart(openTelegramAfterStart); }).detach();
	SetStatusMessage("Подготовка TG WS Proxy...");
}

const char* TgWsProxyManager::GetPrimaryActionLabel() const
{
	if (IsRunning())
		return "Остановить TG WS Proxy";
	if (m_setupRunning.load())
		return "Подготовка TG WS Proxy...";
	if (m_envProbeRunning.load() || m_envState.load() == TgProxyEnvState::Checking)
		return "Проверка окружения...";

	switch (m_envState.load())
	{
	case TgProxyEnvState::MissingPython:
		return "Установить Python";
	case TgProxyEnvState::MissingDependencies:
		return "Установить зависимости";
	case TgProxyEnvState::MissingFiles:
		return "TG WS Proxy не найден";
	case TgProxyEnvState::SettingUp:
		return "Подготовка TG WS Proxy...";
	case TgProxyEnvState::Ready:
	default:
		return "Запустить TG WS Proxy";
	}
}

bool TgWsProxyManager::CanPrimaryAction() const
{
	if (m_asyncOpRunning.load())
		return false;
	if (IsRunning())
		return true;
	if (m_setupRunning.load() || m_envProbeRunning.load())
		return false;

	switch (m_envState.load())
	{
	case TgProxyEnvState::MissingFiles:
	case TgProxyEnvState::SettingUp:
	case TgProxyEnvState::Checking:
		return false;
	default:
		return true;
	}
}

void TgWsProxyManager::RefreshTelegramLinkCache() const
{
	if (!m_settings)
	{
		m_cachedTelegramLink.clear();
		m_cachedLinkHost.clear();
		m_cachedLinkPort = -1;
		m_cachedLinkSecret.clear();
		return;
	}

	const std::string host = m_settings->GetTgProxyHost();
	const int port = m_settings->GetTgProxyPort();
	std::string secret = m_settings->GetTgProxySecret();
	if (secret.size() != 32)
		secret = const_cast<AppSettings*>(m_settings)->EnsureTgProxySecret();

	if (host == m_cachedLinkHost && port == m_cachedLinkPort && secret == m_cachedLinkSecret)
		return;

	m_cachedLinkHost = host;
	m_cachedLinkPort = port;
	m_cachedLinkSecret = secret;
	m_cachedTelegramLink = "tg://proxy?server=" + host + "&port=" + std::to_string(port) + "&secret=dd" + secret;
}

const std::string& TgWsProxyManager::GetTelegramLinkCached() const
{
	RefreshTelegramLinkCache();
	return m_cachedTelegramLink;
}

bool TgWsProxyManager::CopyTelegramLinkToClipboard() const
{
	const std::string& link = GetTelegramLinkCached();
	if (link.empty() || !OpenClipboard(nullptr))
		return false;

	EmptyClipboard();
	const int wchars = MultiByteToWideChar(CP_UTF8, 0, link.c_str(), -1, nullptr, 0);
	if (wchars <= 0)
	{
		CloseClipboard();
		return false;
	}

	const SIZE_T bytes = static_cast<SIZE_T>(wchars) * sizeof(WCHAR);
	HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!memory)
	{
		CloseClipboard();
		return false;
	}

	LPWSTR dst = static_cast<LPWSTR>(GlobalLock(memory));
	if (!dst)
	{
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}

	MultiByteToWideChar(CP_UTF8, 0, link.c_str(), -1, dst, wchars);
	GlobalUnlock(memory);

	if (!SetClipboardData(CF_UNICODETEXT, memory))
	{
		GlobalFree(memory);
		CloseClipboard();
		return false;
	}

	CloseClipboard();
	return true;
}

bool TgWsProxyManager::OpenTelegramLink() const
{
	const std::string& link = GetTelegramLinkCached();
	if (link.empty())
		return false;

	return reinterpret_cast<intptr_t>(ShellExecuteA(nullptr, "open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool TgWsProxyManager::ProbeTelegramConnectivity() const
{
	const_cast<TgWsProxyManager*>(this)->RefreshRunningState();
	if (!m_settings || !IsRunning())
		return false;

	const std::string secretHex = m_settings->GetTgProxySecret();
	if (secretHex.size() != 32)
		return false;

	uint8_t secret[16] = {};
	for (size_t i = 0; i < 16; ++i)
	{
		const char hi = secretHex[i * 2];
		const char lo = secretHex[i * 2 + 1];
		if (!std::isxdigit(static_cast<unsigned char>(hi)) || !std::isxdigit(static_cast<unsigned char>(lo)))
			return false;

		auto nibble = [](char ch) -> int
		{
			if (ch >= '0' && ch <= '9')
				return ch - '0';
			if (ch >= 'a' && ch <= 'f')
				return ch - 'a' + 10;
			if (ch >= 'A' && ch <= 'F')
				return ch - 'A' + 10;
			return -1;
		};

		const int high = nibble(hi);
		const int low = nibble(lo);
		if (high < 0 || low < 0)
			return false;
		secret[i] = static_cast<uint8_t>((high << 4) | low);
	}

	return ZapretConnectivity::ProbeTelegramMtProxy(
		m_settings->GetTgProxyHost().c_str(),
		m_settings->GetTgProxyPort(),
		secret,
		4000);
}
