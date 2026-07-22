#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "vpn/vpn_manager.h"

#include "app/app_log.h"
#include "app/process_job.h"
#include "vpn/vpn_config_builder.h"
#include "vpn/vpn_mihomo_api.h"
#include "vpn/vpn_routing.h"
#include "vpn/vpn_rules_updater.h"
#include "vpn/vpn_transport_settings.h"
#include "zapret/zapret_paths.h"

#include <WinInet.h>

#include <filesystem>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
	void LogVpnMessage(const std::string& message)
	{
		if (!message.empty())
			AppLog::Instance().Append(LogSource::VpnRouting, message);
	}

	void EnsureWinsock()
	{
		static bool ready = false;
		if (ready)
			return;
		WSADATA data = {};
		ready = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
	}

	bool IsTcpPortFree(int port)
	{
		EnsureWinsock();
		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
			return false;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(static_cast<u_short>(port));
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

		const bool free = (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
		closesocket(sock);
		return free;
	}

	// Prefer defaultPort when free; otherwise bind ephemeral like v2rayN Utils.GetFreePort.
	int FindFreeTcpPort(int defaultPort)
	{
		EnsureWinsock();
		if (defaultPort > 0 && IsTcpPortFree(defaultPort))
			return defaultPort;

		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
			return defaultPort > 0 ? defaultPort : 59090;

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
		{
			closesocket(sock);
			return defaultPort > 0 ? defaultPort : 59090;
		}

		sockaddr_in bound = {};
		int len = sizeof(bound);
		getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len);
		const int port = ntohs(bound.sin_port);
		closesocket(sock);
		return port > 0 ? port : (defaultPort > 0 ? defaultPort : 59090);
	}
	std::wstring ToWide(const std::string& value)
	{
		if (value.empty())
			return {};
		const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
		if (length <= 1)
			return {};
		std::wstring result(static_cast<size_t>(length - 1), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
		return result;
	}

	std::string ReadRegString(HKEY key, const wchar_t* valueName)
	{
		wchar_t buffer[512] = {};
		DWORD bufferSize = sizeof(buffer);
		DWORD type = REG_SZ;
		if (RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferSize) != ERROR_SUCCESS)
			return {};

		char utf8[512] = {};
		WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, static_cast<int>(sizeof utf8), nullptr, nullptr);
		return utf8;
	}

	bool RunMihomoTest(const std::wstring& vpnDirectory, const std::wstring& cacheDirectory, std::string& outError)
	{
		const std::filesystem::path mihomoExe = std::filesystem::path(vpnDirectory) / L"mihomo.exe";
		const std::filesystem::path configPath = std::filesystem::path(cacheDirectory) / L"config.yaml";

		SECURITY_ATTRIBUTES securityAttributes = {};
		securityAttributes.nLength = sizeof(securityAttributes);
		securityAttributes.bInheritHandle = TRUE;

		HANDLE readPipe = nullptr;
		HANDLE writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
			return true;

		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		std::wstring commandLine = L"\"";
		commandLine += mihomoExe.wstring();
		commandLine += L"\" -f \"";
		commandLine += configPath.wstring();
		commandLine += L"\" -d \"";
		commandLine += cacheDirectory;
		commandLine += L"\" -t";

		std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
		mutableCommandLine.push_back(L'\0');

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(startupInfo);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdOutput = writePipe;
		startupInfo.hStdError = writePipe;

		PROCESS_INFORMATION processInfo = {};
		const BOOL created = CreateProcessW(
			mihomoExe.c_str(),
			mutableCommandLine.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			cacheDirectory.c_str(),
			&startupInfo,
			&processInfo);

		CloseHandle(writePipe);

		if (!created)
		{
			CloseHandle(readPipe);
			return true;
		}

		WaitForSingleObject(processInfo.hProcess, 8000);

		std::string output;
		char buffer[512];
		DWORD read = 0;
		while (ReadFile(readPipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0)
		{
			buffer[read] = '\0';
			output.append(buffer, buffer + read);
		}

		CloseHandle(readPipe);
		CloseHandle(processInfo.hProcess);
		CloseHandle(processInfo.hThread);

		if (output.find("test is successful") != std::string::npos)
			return true;

		const size_t errorPos = output.find("level=error");
		if (errorPos != std::string::npos)
		{
			const size_t messagePos = output.find("msg=\"", errorPos);
			if (messagePos != std::string::npos)
			{
				const size_t start = messagePos + 5;
				const size_t end = output.find('"', start);
				if (end != std::string::npos)
				{
					outError = "Ошибка config.yaml: " + output.substr(start, end - start);
					return false;
				}
			}
		}

		outError = "config.yaml не прошёл проверку mihomo.";
		return false;
	}
}

VpnManager::VpnManager() = default;

VpnManager::~VpnManager()
{
	Stop();
}

void VpnManager::Update(float deltaTime)
{
	RefreshRunStatus();
	(void)deltaTime;
}

bool VpnManager::ResolveActiveNode(
	const std::vector<VpnNode>& nodes,
	int activeIndex,
	const VpnNode*& outNode,
	std::string& outError) const
{
	if (nodes.empty())
	{
		outError = "Список VPN-серверов пуст. Импортируйте конфиг через Ctrl+V.";
		return false;
	}

	if (activeIndex >= 0 && activeIndex < static_cast<int>(nodes.size()))
	{
		outNode = &nodes[static_cast<size_t>(activeIndex)];
		return true;
	}

	outError = "Не выбран активный VPN-профиль.";
	return false;
}

bool VpnManager::IsRuntimeReady(std::string& outError) const
{
	const std::filesystem::path vpnRoot(ZapretPaths::GetVpnDirectory());
	const std::filesystem::path mihomoExe = vpnRoot / L"mihomo.exe";
	const std::filesystem::path wintunDll = vpnRoot / L"wintun.dll";

	if (!std::filesystem::exists(mihomoExe))
	{
		outError = "Не найден mihomo.exe в папке vpn/.";
		return false;
	}
	if (!std::filesystem::exists(wintunDll))
	{
		outError = "Не найден wintun.dll в папке vpn/.";
		return false;
	}
	if (!VpnRulesUpdater::AreCoreRulesReady())
	{
		outError = VpnRulesUpdater::IsUpdateInProgress()
			? "Ожидание загрузки правил маршрутизации..."
			: "Нет rule-set файлов в cache/srss/. Дождитесь автообновления или проверьте интернет.";
		return false;
	}

	outError.clear();
	return true;
}

void VpnManager::EnsurePortsAllocated()
{
	m_mixedPort = FindFreeTcpPort(kDefaultMixedPort);
	m_apiPort = FindFreeTcpPort(kDefaultApiPort);
	if (m_apiPort == m_mixedPort)
		m_apiPort = FindFreeTcpPort(0);
}

bool VpnManager::LaunchProcess()
{
	const std::wstring vpnDirectory = ZapretPaths::GetVpnDirectory();
	const std::wstring cacheDirectory = ZapretPaths::GetCacheDirectory();
	const std::filesystem::path mihomoExe = std::filesystem::path(vpnDirectory) / L"mihomo.exe";
	const std::filesystem::path configPath = std::filesystem::path(cacheDirectory) / L"config.yaml";

	std::wstring commandLine = L"\"";
	commandLine += mihomoExe.wstring();
	commandLine += L"\" -f \"";
	commandLine += configPath.wstring();
	commandLine += L"\" -d \"";
	commandLine += cacheDirectory;
	commandLine += L"\"";

	std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
	mutableCommandLine.push_back(L'\0');

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo = {};

	const BOOL created = ProcessJob::CreateInJob(
		mihomoExe.c_str(),
		mutableCommandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		cacheDirectory.c_str(),
		&startupInfo,
		&processInfo);

	if (!created)
	{
		m_lastError = "Не удалось запустить mihomo.exe.";
		LogVpnMessage(m_lastError);
		return false;
	}

	CloseHandle(processInfo.hThread);

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		m_process = processInfo.hProcess;
	}

	Sleep(400);

	DWORD exitCode = STILL_ACTIVE;
	if (GetExitCodeProcess(processInfo.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
	{
		m_lastError = "mihomo.exe завершился сразу после запуска. Проверьте config.yaml.";
		LogVpnMessage(m_lastError);
		Stop();
		return false;
	}

	return true;
}

void VpnManager::ApplySystemProxy(bool enable)
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(
			HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
			0,
			KEY_QUERY_VALUE | KEY_SET_VALUE,
			&key) != ERROR_SUCCESS)
		return;

	if (enable && !m_savedProxySettings)
	{
		DWORD type = REG_DWORD;
		DWORD bufferSize = sizeof(m_savedProxyEnable);
		if (RegQueryValueExW(key, L"ProxyEnable", nullptr, &type, reinterpret_cast<LPBYTE>(&m_savedProxyEnable), &bufferSize) == ERROR_SUCCESS)
		{
			wchar_t proxyServer[512] = {};
			DWORD proxyServerSize = sizeof(proxyServer);
			if (RegQueryValueExW(key, L"ProxyServer", nullptr, &type, reinterpret_cast<LPBYTE>(proxyServer), &proxyServerSize) == ERROR_SUCCESS)
				m_savedProxyServer = proxyServer;
			m_savedProxySettings = true;
		}
	}

	if (enable)
	{
		const DWORD proxyEnable = 1;
		const std::wstring proxyServer = L"127.0.0.1:" + std::to_wstring(m_mixedPort);
		RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&proxyEnable), sizeof(proxyEnable));
		RegSetValueExW(
			key,
			L"ProxyServer",
			0,
			REG_SZ,
			reinterpret_cast<const BYTE*>(proxyServer.c_str()),
			static_cast<DWORD>((proxyServer.size() + 1) * sizeof(wchar_t)));
	}
	else if (m_savedProxySettings)
	{
		RegSetValueExW(
			key,
			L"ProxyEnable",
			0,
			REG_DWORD,
			reinterpret_cast<const BYTE*>(&m_savedProxyEnable),
			sizeof(m_savedProxyEnable));
		if (!m_savedProxyServer.empty())
		{
			RegSetValueExW(
				key,
				L"ProxyServer",
				0,
				REG_SZ,
				reinterpret_cast<const BYTE*>(m_savedProxyServer.c_str()),
				static_cast<DWORD>((m_savedProxyServer.size() + 1) * sizeof(wchar_t)));
		}
	}
	else
	{
		const DWORD proxyEnable = 0;
		RegSetValueExW(key, L"ProxyEnable", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&proxyEnable), sizeof(proxyEnable));
	}

	RegCloseKey(key);
	InternetSetOptionW(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
	InternetSetOptionW(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
}

void VpnManager::RestoreSystemProxy()
{
	ApplySystemProxy(false);
	m_savedProxySettings = false;
	m_savedProxyServer.clear();
	m_savedProxyEnable = 0;
}

void VpnManager::RefreshRunStatus()
{
	if (m_opInFlight.load())
		return;

	HANDLE process = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		process = m_process;
	}

	if (!process)
	{
		if (m_runStatus != VpnRunStatus::Stopped)
		{
			m_runStatus = VpnRunStatus::Stopped;
			m_statusMessage = "VPN остановлен.";
			LogVpnMessage(m_statusMessage);
		}
		return;
	}

	DWORD exitCode = STILL_ACTIVE;
	if (GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE)
	{
		{
			std::lock_guard<std::mutex> lock(m_processMutex);
			CloseHandle(m_process);
			m_process = nullptr;
		}
		m_runStatus = VpnRunStatus::Stopped;
		m_lastError = "mihomo.exe завершился неожиданно.";
		m_statusMessage = "VPN остановлен.";
		LogVpnMessage(m_lastError);
		return;
	}

	m_runStatus = VpnRunStatus::Running;
}

bool VpnManager::WriteAndReloadConfig(
	const VpnNode& node,
	const VpnStoreSettings& settings,
	bool coldStart,
	std::string& outError)
{
	const VpnRoutingPreset preset = VpnRouting::PresetFromWorkMode(settings.workMode);
	const std::wstring cacheDirectory = ZapretPaths::GetCacheDirectory();
	if (!VpnConfigBuilder::WriteRuntimeConfig(
			node,
			preset,
			settings,
			cacheDirectory,
			m_mixedPort,
			m_apiPort,
			outError,
			coldStart))
		return false;

	if (coldStart && !RunMihomoTest(ZapretPaths::GetVpnDirectory(), cacheDirectory, outError))
		return false;

	return true;
}

void VpnManager::MarkSettingsApplied(const VpnStoreSettings& settings)
{
	m_appliedRoutingRevision = settings.routingRevision;
}

int VpnManager::ResolveActiveIndex(const std::vector<VpnNode>& nodes, const VpnStoreSettings& settings) const
{
	if (settings.activeUri.empty())
		return nodes.empty() ? -1 : 0;

	for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
	{
		if (nodes[static_cast<size_t>(i)].originalUri == settings.activeUri)
			return i;
	}

	return nodes.empty() ? -1 : 0;
}

bool VpnManager::Start(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings)
{
	Stop();

	std::string runtimeError;
	if (!IsRuntimeReady(runtimeError))
	{
		// Soft wait — SyncVpnRuntime retries by readiness; don't treat as hard crash spam.
		m_lastError = runtimeError;
		m_statusMessage = runtimeError;
		return false;
	}

	EnsurePortsAllocated();

	const VpnNode* activeNode = nullptr;
	std::string selectionMessage;
	if (!ResolveActiveNode(nodes, activeIndex, activeNode, selectionMessage))
	{
		m_lastError = selectionMessage;
		LogVpnMessage(m_lastError);
		return false;
	}

	if (!WriteAndReloadConfig(*activeNode, settings, true, m_lastError))
	{
		LogVpnMessage(m_lastError);
		return false;
	}

	m_runStatus = VpnRunStatus::Starting;
	m_statusMessage = "Запуск VPN...";
	LogVpnMessage(m_statusMessage);

	if (!LaunchProcess())
	{
		m_runStatus = VpnRunStatus::Stopped;
		return false;
	}

	if (settings.transportMode == 0)
		ApplySystemProxy(true);

	m_runStatus = VpnRunStatus::Running;
	m_lastError.clear();
	MarkSettingsApplied(settings);
	if (settings.transportMode == 0)
		m_statusMessage = "VPN (Proxy): " + activeNode->name;
	else
		m_statusMessage = "VPN (TUN): " + activeNode->name;
	LogVpnMessage(m_statusMessage);
	return true;
}

bool VpnManager::Reload(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings)
{
	if (!IsRunning())
		return Start(nodes, activeIndex, settings);

	const VpnNode* activeNode = nullptr;
	std::string selectionMessage;
	if (!ResolveActiveNode(nodes, activeIndex, activeNode, selectionMessage))
	{
		m_lastError = selectionMessage;
		LogVpnMessage(m_lastError);
		return false;
	}

	if (!WriteAndReloadConfig(*activeNode, settings, false, m_lastError))
	{
		LogVpnMessage(m_lastError);
		return false;
	}

	const std::filesystem::path configPath = std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"config.yaml";
	if (MihomoApi::ReloadConfig(configPath.wstring(), m_apiPort, m_lastError))
	{
		MihomoApi::FlushConnections(m_apiPort);
		MarkSettingsApplied(settings);
		m_lastError.clear();
		m_statusMessage = "Маршрутизация обновлена.";
		LogVpnMessage(m_statusMessage);
		return true;
	}

	LogVpnMessage("Hot reload недоступен, полный перезапуск VPN...");
	const bool restarted = Start(nodes, activeIndex, settings);
	return restarted;
}

void VpnManager::RunReloadWorker()
{
	for (;;)
	{
		std::vector<VpnNode> nodes;
		int activeIndex = -1;
		VpnStoreSettings settings;
		{
			std::lock_guard<std::mutex> lock(m_reloadQueueMutex);
			if (!m_reloadQueued)
			{
				m_reloadWorkerRunning.store(false);
				if (!m_reloadQueued)
					return;

				m_reloadWorkerRunning.store(true);
				continue;
			}

			m_reloadQueued = false;
			nodes = std::move(m_reloadNodes);
			activeIndex = m_reloadActiveIndex;
			settings = m_reloadSettings;
		}

		{
			std::lock_guard<std::mutex> lock(m_opMutex);
			m_opInFlight.store(true);
			Reload(nodes, activeIndex, settings);
			m_opInFlight.store(false);
		}
	}
}

void VpnManager::RequestReload(
	const std::vector<VpnNode>& nodes,
	int activeIndex,
	const VpnStoreSettings& settings)
{
	{
		std::lock_guard<std::mutex> lock(m_reloadQueueMutex);
		m_reloadNodes = nodes;
		m_reloadActiveIndex = activeIndex;
		m_reloadSettings = settings;
		m_reloadQueued = true;
	}

	bool expected = false;
	if (m_reloadWorkerRunning.compare_exchange_strong(expected, true))
		std::thread([this]() { RunReloadWorker(); }).detach();
}

void VpnManager::RequestReloadFromStore()
{
	if (!IsRunning())
		return;

	VpnStore store;
	std::vector<VpnNode> nodes;
	VpnStoreSettings settings;
	store.Load(nodes, &settings);

	const int activeIndex = ResolveActiveIndex(nodes, settings);
	if (activeIndex < 0)
		return;

	RequestReload(nodes, activeIndex, settings);
}

void VpnManager::Stop()
{
	HANDLE process = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		process = m_process;
		m_process = nullptr;
	}

	if (process)
	{
		TerminateProcess(process, 0);
		WaitForSingleObject(process, 2000);
		CloseHandle(process);
	}

	RestoreSystemProxy();
	m_runStatus = VpnRunStatus::Stopped;
	m_appliedRoutingRevision = -1;
	if (m_statusMessage.empty())
		m_statusMessage = "VPN остановлен.";
	LogVpnMessage(m_statusMessage);
}

void VpnManager::RequestStart(
	const std::vector<VpnNode>& nodes,
	int activeIndex,
	const VpnStoreSettings& settings)
{
	m_runStatus = VpnRunStatus::Starting;
	std::thread([this, nodes, activeIndex, settings]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_opInFlight.store(true);
		Start(nodes, activeIndex, settings);
		m_opInFlight.store(false);
	}).detach();
}

void VpnManager::RequestStop()
{
	std::thread([this]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_opInFlight.store(true);
		Stop();
		m_opInFlight.store(false);
	}).detach();
}

int VpnManager::AllocateFreeTcpPort(int preferred)
{
	return FindFreeTcpPort(preferred);
}

bool VpnManager::StartFromExistingConfig(int mixedPort, int apiPort)
{
	std::lock_guard<std::mutex> lock(m_opMutex);
	m_opInFlight.store(true);

	Stop();

	std::string runtimeError;
	if (!IsRuntimeReady(runtimeError))
	{
		m_lastError = runtimeError;
		m_statusMessage = runtimeError;
		m_opInFlight.store(false);
		return false;
	}

	m_mixedPort = mixedPort > 0 ? mixedPort : FindFreeTcpPort(kDefaultMixedPort);
	m_apiPort = apiPort > 0 ? apiPort : FindFreeTcpPort(kDefaultApiPort);
	if (m_apiPort == m_mixedPort)
		m_apiPort = FindFreeTcpPort(0);

	std::string testError;
	if (!RunMihomoTest(ZapretPaths::GetVpnDirectory(), ZapretPaths::GetCacheDirectory(), testError))
	{
		m_lastError = testError.empty() ? "mihomo -t не прошёл для probe-конфига." : testError;
		LogVpnMessage(m_lastError);
		m_opInFlight.store(false);
		return false;
	}

	m_runStatus = VpnRunStatus::Starting;
	m_statusMessage = "RealPing: запуск mihomo...";
	if (!LaunchProcess())
	{
		m_runStatus = VpnRunStatus::Stopped;
		m_opInFlight.store(false);
		return false;
	}

	// Probe session: never touch system proxy.
	m_runStatus = VpnRunStatus::Running;
	m_lastError.clear();
	m_statusMessage = "RealPing: mihomo готов.";
	m_opInFlight.store(false);
	return true;
}
