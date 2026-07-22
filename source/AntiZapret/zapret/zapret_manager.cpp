#include "zapret/zapret_connectivity.h"
#include "zapret/zapret_manager.h"

#include <climits>

#include "app/app_log.h"
#include "app/app_settings.h"
#include "app/process_job.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "zapret/strategy_argument_builder.h"
#include "zapret/strategy_bat_parser.h"
#include "zapret/zapret_paths.h"
#include "zapret/zapret_strategy_probe.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <random>
#include <thread>
#include <vector>

namespace
{
	// Как в interface: ALT2 < ALT10 (цифры сравниваются как числа).
	bool CompareStrategyNamesNatural(const std::string& a, const std::string& b)
	{
		size_t i = 0;
		size_t j = 0;
		while (i < a.size() && j < b.size())
		{
			const unsigned char ca = static_cast<unsigned char>(a[i]);
			const unsigned char cb = static_cast<unsigned char>(b[j]);
			const bool da = std::isdigit(ca) != 0;
			const bool db = std::isdigit(cb) != 0;
			if (da && db)
			{
				size_t ai = i;
				while (ai < a.size() && std::isdigit(static_cast<unsigned char>(a[ai])))
					++ai;
				size_t bj = j;
				while (bj < b.size() && std::isdigit(static_cast<unsigned char>(b[bj])))
					++bj;
				unsigned long long va = 0;
				for (size_t k = i; k < ai; ++k)
					va = va * 10ull + static_cast<unsigned long long>(a[k] - '0');
				unsigned long long vb = 0;
				for (size_t k = j; k < bj; ++k)
					vb = vb * 10ull + static_cast<unsigned long long>(b[k] - '0');
				if (va != vb)
					return va < vb;
				i = ai;
				j = bj;
			}
			else
			{
				const char la = static_cast<char>(std::tolower(ca));
				const char lb = static_cast<char>(std::tolower(cb));
				if (la != lb)
					return la < lb;
				++i;
				++j;
			}
		}
		return a.size() < b.size();
	}

	bool IsWinwsProcessRunning()
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return false;

		PROCESSENTRY32W entry = { sizeof(entry) };
		bool found = false;
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, L"winws.exe") == 0)
				{
					found = true;
					break;
				}
			} while (Process32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);
		return found;
	}

	bool IsZapretServiceRunning()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return false;

		SC_HANDLE service = OpenServiceW(manager, L"zapret", SERVICE_QUERY_STATUS);
		if (!service)
		{
			CloseServiceHandle(manager);
			return false;
		}

		SERVICE_STATUS status = {};
		const bool running = QueryServiceStatus(service, &status)
			&& (status.dwCurrentState == SERVICE_RUNNING || status.dwCurrentState == SERVICE_START_PENDING);

		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		return running;
	}

	bool IsZapretServiceStarting()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return false;

		SC_HANDLE service = OpenServiceW(manager, L"zapret", SERVICE_QUERY_STATUS);
		if (!service)
		{
			CloseServiceHandle(manager);
			return false;
		}

		SERVICE_STATUS status = {};
		const bool starting = QueryServiceStatus(service, &status)
			&& status.dwCurrentState == SERVICE_START_PENDING;

		CloseServiceHandle(service);
		CloseServiceHandle(manager);
		return starting;
	}

	void TerminateAllWinwsProcesses()
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
			return;

		PROCESSENTRY32W entry = { sizeof(entry) };
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, L"winws.exe") != 0)
					continue;

				HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
				if (process)
				{
					TerminateProcess(process, 0);
					CloseHandle(process);
				}
			} while (Process32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);
	}

	void StopZapretService()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		SC_HANDLE service = OpenServiceW(manager, L"zapret", SERVICE_STOP | SERVICE_QUERY_STATUS);
		if (service)
		{
			SERVICE_STATUS status = {};
			ControlService(service, SERVICE_CONTROL_STOP, &status);
			CloseServiceHandle(service);
		}

		CloseServiceHandle(manager);
	}

	void StartWinDivertServices()
	{
		SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
		if (!manager)
			return;

		const wchar_t* names[] = { L"WinDivert", L"WinDivert14" };
		for (const wchar_t* name : names)
		{
			SC_HANDLE service = OpenServiceW(manager, name, SERVICE_START | SERVICE_QUERY_STATUS);
			if (service)
			{
				StartServiceW(service, 0, nullptr);
				CloseServiceHandle(service);
			}
		}

		CloseServiceHandle(manager);
	}

	bool RunHiddenCommand(const wchar_t* command, DWORD timeoutMs)
	{
		STARTUPINFOW si = { sizeof(si) };
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi = {};

		std::wstring cmdLine = L"cmd.exe /C ";
		cmdLine += command;
		std::vector<wchar_t> buffer(cmdLine.begin(), cmdLine.end());
		buffer.push_back(L'\0');

		if (!CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
			return false;

		WaitForSingleObject(pi.hProcess, timeoutMs);
		DWORD exitCode = 1;
		GetExitCodeProcess(pi.hProcess, &exitCode);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return exitCode == 0;
	}

	void EnsureTcpTimestampsEnabled()
	{
		if (RunHiddenCommand(
			L"netsh interface tcp show global | findstr /i \"timestamps\" | findstr /i \"enabled\"",
			8000))
		{
			return;
		}

		RunHiddenCommand(L"netsh interface tcp set global timestamps=enabled", 12000);
	}
}

ZapretManager::ZapretManager()
{
	LoadRuntimeStrategies();
	LoadStore();
}

ZapretManager::~ZapretManager()
{
	m_strategyTestStopRequested.store(true);
	Stop();
}

void ZapretManager::LoadStore()
{
	m_store.Load();
	m_smartStrategy.Load();
	InvalidateStrategyCache();
}

void ZapretManager::ReloadRuntimeStrategies()
{
	const std::string activeKey = GetActiveStrategyStoreKey();
	LoadRuntimeStrategies();
	if (!activeKey.empty())
	{
		for (int i = 0; i < static_cast<int>(m_strategies.size()); ++i)
		{
			if (m_strategies[static_cast<size_t>(i)].id == activeKey)
			{
				m_activeStrategyIndex = i;
				break;
			}
		}
	}
	InvalidateStrategyCache();
}

void ZapretManager::SaveStore()
{
	m_store.Save();
}

void ZapretManager::RememberSelectedStrategy(int strategyIndex)
{
	if (!IsValidStrategyIndex(strategyIndex))
		return;
	m_store.SetLastStrategy(GetStrategyKey(strategyIndex));
	SaveStore();
}

void ZapretManager::RememberSmartStrategySelected()
{
	m_store.SetLastStrategy(SmartStrategyEngine::kLabel);
	SaveStore();
}

void ZapretManager::InvalidateStrategyCache()
{
	m_strategyCacheValid = false;
}

void ZapretManager::LoadRuntimeStrategies()
{
	m_strategies.clear();
	const std::filesystem::path root(ZapretPaths::GetAntiZapretDirectory());
	std::error_code error;
	if (std::filesystem::is_directory(root, error))
	{
		for (const auto& entry : std::filesystem::directory_iterator(root, error))
		{
			if (error || !entry.is_regular_file(error))
				continue;
			const std::filesystem::path path = entry.path();
			std::wstring extension = path.extension().wstring();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
			std::wstring stem = path.stem().wstring();
			if (extension != L".bat" || stem.size() < 7
				|| _wcsnicmp(stem.c_str(), L"general", 7) != 0)
				continue;

			RuntimeStrategy strategy;
			strategy.id = ZapretPaths::WideToUtf8(stem);
			strategy.label = strategy.id;
			strategy.fileName = ZapretPaths::WideToUtf8(path.filename().wstring());
			strategy.batPath = path.wstring();
			m_strategies.push_back(std::move(strategy));
		}
	}

	std::sort(m_strategies.begin(), m_strategies.end(),
		[](const RuntimeStrategy& left, const RuntimeStrategy& right)
		{
			return CompareStrategyNamesNatural(left.label, right.label);
		});

	// If Flowseal bats are not deployed yet, fall back to compiled-in general strategies.
	if (m_strategies.empty())
	{
		for (const ZapretStrategies::StrategyDefinition& strategy : ZapretStrategies::kStrategies)
		{
			if (strategy.isExtra)
				continue;
			RuntimeStrategy entry;
			entry.id = std::string(strategy.id);
			entry.label = std::string(strategy.label);
			entry.fileName = std::string(strategy.file);
			entry.staticDefinition = &strategy;
			entry.isExtra = false;
			m_strategies.push_back(std::move(entry));
		}
	}

	for (const ZapretStrategies::StrategyDefinition& strategy : ZapretStrategies::kStrategies)
	{
		if (!strategy.isExtra)
			continue;
		RuntimeStrategy extra;
		extra.id = std::string(strategy.id);
		extra.label = std::string(strategy.label);
		extra.fileName = std::string(strategy.file);
		extra.staticDefinition = &strategy;
		extra.isExtra = true;
		m_strategies.push_back(std::move(extra));
	}
	m_strategyResultCache.assign(m_strategies.size(), nullptr);
	InvalidateStrategyCache();
}

bool ZapretManager::IsValidStrategyIndex(int strategyIndex) const
{
	return strategyIndex >= 0 && strategyIndex < static_cast<int>(m_strategies.size());
}

const std::string& ZapretManager::GetStrategyKey(int strategyIndex) const
{
	static const std::string empty;
	return IsValidStrategyIndex(strategyIndex) ? m_strategies[static_cast<size_t>(strategyIndex)].id : empty;
}

int ZapretManager::GetVisibleStrategyCount(bool showExtraStrategies) const
{
	int count = 0;
	for (const RuntimeStrategy& strategy : m_strategies)
		if (showExtraStrategies || !strategy.isExtra)
			++count;
	return count;
}

int ZapretManager::GetVisibleStrategyAt(int visibleIndex, bool showExtraStrategies) const
{
	int seen = 0;
	for (size_t i = 0; i < m_strategies.size(); ++i)
	{
		if (!showExtraStrategies && m_strategies[i].isExtra)
			continue;
		if (seen++ == visibleIndex)
			return static_cast<int>(i);
	}
	return -1;
}

bool ZapretManager::IsStrategyVisible(int strategyIndex, bool showExtraStrategies) const
{
	return IsValidStrategyIndex(strategyIndex)
		&& (showExtraStrategies || !m_strategies[static_cast<size_t>(strategyIndex)].isExtra);
}

bool ZapretManager::IsBatchStrategy(int strategyIndex) const
{
	return IsValidStrategyIndex(strategyIndex)
		&& !m_strategies[static_cast<size_t>(strategyIndex)].staticDefinition;
}

const std::string& ZapretManager::GetStrategyLabel(int strategyIndex) const
{
	static const std::string empty;
	return IsValidStrategyIndex(strategyIndex) ? m_strategies[static_cast<size_t>(strategyIndex)].label : empty;
}

const std::string& ZapretManager::GetStrategyFileName(int strategyIndex) const
{
	static const std::string empty;
	return IsValidStrategyIndex(strategyIndex) ? m_strategies[static_cast<size_t>(strategyIndex)].fileName : empty;
}

bool ZapretManager::ShowExtraStrategies() const
{
	return m_appSettings && m_appSettings->GetShowExtraStrategies();
}

int ZapretManager::ClampStrategyIndexToVisible(int strategyIndex) const
{
	const bool showExtra = ShowExtraStrategies();
	if (IsStrategyVisible(strategyIndex, showExtra))
		return strategyIndex;

	const int fallback = GetVisibleStrategyAt(0, showExtra);
	return fallback >= 0 ? fallback : 0;
}

int ZapretManager::GetBestVisibleStrategyIndex() const
{
	const bool showExtra = ShowExtraStrategies();
	int bestIndex = -1;
	int maxScore = -1;
	int bestPingMs = -1;

	for (std::size_t i = 0; i < m_strategies.size(); ++i)
	{
		if (!IsStrategyVisible(static_cast<int>(i), showExtra))
			continue;

		const StrategyTestEntry* entry = m_strategyResultCache[static_cast<std::size_t>(i)];
		if (!entry)
			continue;

		int score = 0;
		if (entry->discordOk)
			++score;
		if (entry->youtubeOk)
			++score;
		if (entry->telegramOk)
			++score;

		bool wins = false;
		if (score > maxScore)
			wins = true;
		else if (score == maxScore)
		{
			if (entry->pingMs >= 0 && bestPingMs < 0)
				wins = true;
			else if (entry->pingMs >= 0 && bestPingMs >= 0 && entry->pingMs < bestPingMs)
				wins = true;
		}

		if (wins)
		{
			maxScore = score;
			bestPingMs = entry->pingMs;
			bestIndex = static_cast<int>(i);
		}
	}

	return bestIndex;
}

void ZapretManager::EnsureStrategyCache() const
{
	if (m_strategyCacheValid)
		return;

	for (std::size_t i = 0; i < m_strategies.size(); ++i)
		m_strategyResultCache[i] = m_store.GetResult(m_strategies[i].id);

	m_cachedPreferredBestIndex = GetBestVisibleStrategyIndex();
	m_strategyCacheValid = true;
}

int ZapretManager::GetPreferredStrategyIndex(bool autoSelectBest) const
{
	EnsureStrategyCache();

	// Всегда предпочитаем последнюю запущенную/выбранную стратегию.
	// autoSelectBest влияет на failover (FindFallbackStrategyIndex), а не на «забыть last».
	const std::string& lastStrategy = m_store.GetLastStrategy();
	if (!lastStrategy.empty() && lastStrategy != SmartStrategyEngine::kLabel)
	{
		for (size_t i = 0; i < m_strategies.size(); ++i)
		{
			if (m_strategies[i].id == lastStrategy)
				return ClampStrategyIndexToVisible(static_cast<int>(i));
		}
	}

	if (autoSelectBest)
	{
		const int statsBest = GetBestVisibleStrategyByStats(-1);
		if (statsBest >= 0)
			return statsBest;
	}

	return ClampStrategyIndexToVisible(GetVisibleStrategyAt(0, ShowExtraStrategies()));
}

int ZapretManager::GetBestVisibleStrategyByStats(int excludeIndex) const
{
	EnsureStrategyCache();
	const bool showExtra = ShowExtraStrategies();
	const int visibleCount = GetVisibleStrategyCount(showExtra);

	int bestIndex = -1;
	int bestRuntime = 0;
	int bestOutages = INT_MAX;
	int bestOrder = INT_MAX;

	for (int pass = 0; pass < visibleCount; ++pass)
	{
		const int idx = GetVisibleStrategyAt(pass, showExtra);
		if (idx < 0 || idx == excludeIndex)
			continue;

		const StrategyTestEntry* entry = m_strategyResultCache[static_cast<std::size_t>(idx)];
		const int runtime = entry ? entry->runtimeSec : 0;
		if (runtime <= 0)
			continue;

		const int outages = entry ? entry->providerDualOutageCount : INT_MAX;
		const bool wins = bestIndex < 0
			|| runtime > bestRuntime
			|| (runtime == bestRuntime && outages < bestOutages)
			|| (runtime == bestRuntime && outages == bestOutages && pass < bestOrder);
		if (!wins)
			continue;

		bestIndex = idx;
		bestRuntime = runtime;
		bestOutages = outages;
		bestOrder = pass;
	}

	return bestIndex;
}

int ZapretManager::FindFallbackStrategyIndex(int failedIndex) const
{
	const int statsBest = GetBestVisibleStrategyByStats(failedIndex);
	if (statsBest >= 0)
		return statsBest;

	const bool showExtra = ShowExtraStrategies();
	const int visibleCount = GetVisibleStrategyCount(showExtra);
	if (visibleCount <= 1)
		return failedIndex;

	int failedPos = -1;
	for (int pass = 0; pass < visibleCount; ++pass)
		if (GetVisibleStrategyAt(pass, showExtra) == failedIndex)
		{
			failedPos = pass;
			break;
		}
	if (failedPos < 0)
		failedPos = 0;

	for (int step = 1; step <= visibleCount; ++step)
	{
		const int pass = (failedPos + step) % visibleCount;
		const int idx = GetVisibleStrategyAt(pass, showExtra);
		if (idx >= 0 && idx != failedIndex)
			return idx;
	}

	return failedIndex;
}

void ZapretManager::MaybeAutoFailover(bool discordOk, bool youtubeOk)
{
	if (!m_appSettings || !m_appSettings->GetAutoSelectBestStrategy())
		return;
	if (m_activeSmartStrategy)
		return;
	if (m_strategyTestState.load() == StrategyTestState::Running
		|| m_smartTuneState.load() == SmartStrategyTuneState::Running)
		return;
	if (!IsRunning() || m_activeStrategyIndex < 0)
		return;
	if (m_opInFlight.load())
		return;

	const bool dualOutage = !discordOk && !youtubeOk;
	if (dualOutage)
		++m_consecutiveDualFailures;
	else
		m_consecutiveDualFailures = 0;

	if (m_consecutiveDualFailures < kAutoFailoverConfirmChecks)
		return;

	m_consecutiveDualFailures = 0;

	const int currentIndex = m_activeStrategyIndex;
	const int fallbackIndex = FindFallbackStrategyIndex(currentIndex);
	if (fallbackIndex < 0 || fallbackIndex == currentIndex)
		return;

	char msg[256] = {};
	snprintf(
		msg,
		sizeof msg,
		"Автопереключение (сбой Discord+YouTube): %s -> %s",
		GetStrategyLabel(currentIndex).c_str(),
		GetStrategyLabel(fallbackIndex).c_str());
	AppLog::Instance().Append(LogSource::Zapret, msg);

	m_connectivityObserved = false;
	m_dualOutageActive = false;
	RequestStart(fallbackIndex, m_activeGameFilterMode, true);
}

int ZapretManager::GetCachedBestStrategyIndex() const
{
	EnsureStrategyCache();
	return m_cachedPreferredBestIndex;
}

const StrategyTestEntry* ZapretManager::GetStrategyResult(int strategyIndex) const
{
	if (!IsValidStrategyIndex(strategyIndex))
		return nullptr;

	EnsureStrategyCache();
	return m_strategyResultCache[static_cast<std::size_t>(strategyIndex)];
}

std::vector<StrategyTargetResultView> ZapretManager::GetStrategyTargetResults(int strategyIndex) const
{
	if (!IsValidStrategyIndex(strategyIndex))
		return {};
	const StrategyTestEntry* entry = m_store.GetResult(GetStrategyKey(strategyIndex));
	if (!entry)
		return {};
	return entry->targets;
}

void ZapretManager::SetSmartStrategyEnabled(bool enabled)
{
	m_smartStrategy.SetEnabled(enabled);
	if (enabled)
	{
		const SmartStrategyStatus status = GetSmartStrategyStatus(IsTelegramViaMtproto());
		char msg[384] = {};
		snprintf(msg, sizeof msg, "[Smart] %s", status.explanation.c_str());
		AppLog::Instance().Append(LogSource::Zapret, msg);
	}
}

SmartStrategyStatus ZapretManager::GetSmartStrategyStatus(bool telegramViaMtproto) const
{
	return m_smartStrategy.BuildStatus(
		m_smartTuneState.load(),
		m_smartTuneCurrent.load(),
		m_smartTuneTotal.load());
}

bool ZapretManager::IsTelegramViaMtproto() const
{
	return m_tgProxyManager && m_tgProxyManager->IsRunning();
}

std::string ZapretManager::GetActiveStrategyStoreKey() const
{
	if (m_activeSmartStrategy)
		return SmartStrategyEngine::kLabel;

	if (IsValidStrategyIndex(m_activeStrategyIndex))
		return GetStrategyKey(m_activeStrategyIndex);

	return {};
}

StrategyTestEntry ZapretManager::LoadStrategyEntryOrDefault(const std::string& strategyKey) const
{
	StrategyTestEntry entry;
	if (const StrategyTestEntry* existing = m_store.GetResult(strategyKey))
		entry = *existing;
	return entry;
}

void ZapretManager::FlushRuntimeTracking(bool saveToDisk)
{
	if (m_runtimeTrackingKey.empty())
		return;

	const int wholeSeconds = static_cast<int>(m_runtimePendingSec);
	m_runtimePendingSec -= static_cast<float>(wholeSeconds);
	if (wholeSeconds <= 0)
		return;

	StrategyTestEntry entry = LoadStrategyEntryOrDefault(m_runtimeTrackingKey);
	entry.runtimeSec += wholeSeconds;
	m_store.SetResult(m_runtimeTrackingKey, entry);
	InvalidateStrategyCache();

	if (saveToDisk)
		SaveStore();
}

void ZapretManager::BeginRuntimeTracking(const std::string& strategyKey)
{
	if (strategyKey.empty())
		return;

	if (strategyKey == m_runtimeTrackingKey)
		return;

	EndRuntimeTracking();
	m_runtimeTrackingKey = strategyKey;
	m_runtimePendingSec = 0.f;
	m_connectivityObserved = false;
	m_dualOutageActive = false;
	m_consecutiveDualFailures = 0;
}

void ZapretManager::EndRuntimeTracking()
{
	FlushRuntimeTracking(true);
	m_runtimeTrackingKey.clear();
	m_runtimePendingSec = 0.f;
	m_connectivityObserved = false;
	m_dualOutageActive = false;
	m_consecutiveDualFailures = 0;
}

void ZapretManager::AccumulateRuntime(float deltaTime)
{
	if (m_runtimeTrackingKey.empty() || deltaTime <= 0.f)
		return;

	m_runtimePendingSec += deltaTime;
	if (m_runtimePendingSec >= 60.f)
		FlushRuntimeTracking(true);
}

void ZapretManager::ApplyDualOutageDetection(bool discordOk, bool youtubeOk, StrategyTestEntry& entry)
{
	const bool dualOutage = !discordOk && !youtubeOk;
	if (m_connectivityObserved && dualOutage && !m_dualOutageActive)
		++entry.providerDualOutageCount;

	m_connectivityObserved = true;
	m_dualOutageActive = dualOutage;
}

void ZapretManager::RecordStrategyResult(int strategyIndex, bool discord, bool youtube, bool telegram, int pingMs)
{
	if (!IsValidStrategyIndex(strategyIndex))
		return;

	const std::string& name = GetStrategyKey(strategyIndex);
	StrategyTestEntry entry = LoadStrategyEntryOrDefault(name);
	entry.discordOk = discord;
	entry.youtubeOk = youtube;
	entry.telegramOk = telegram;
	entry.pingMs = pingMs;
	entry.fullTest = false;
	entry.httpOk = 0;
	entry.httpErr = 0;
	entry.pingOk = pingMs >= 0 ? 1 : 0;
	entry.pingFail = pingMs >= 0 ? 0 : 1;

	entry.targets.clear();
	entry.targets.push_back({ "Discord", discord ? "OK" : "FAIL", discord, false, -1 });
	entry.targets.push_back({ "YouTube", youtube ? "OK" : "FAIL", youtube, false, -1 });
	entry.targets.push_back({ "Telegram", telegram ? "OK" : "FAIL", telegram, false, -1 });
	{
		StrategyTargetResultView pingRow;
		pingRow.name = "ICMP Ping";
		pingRow.isPing = true;
		pingRow.pingMs = pingMs;
		pingRow.ok = pingMs >= 0;
		if (pingMs >= 0)
			pingRow.detail = std::to_string(pingMs) + " ms";
		else
			pingRow.detail = "FAIL";
		entry.targets.push_back(std::move(pingRow));
	}

	m_store.SetResult(name, entry);
	SaveStore();
	InvalidateStrategyCache();
}

void ZapretManager::RecordStrategyFullResult(int strategyIndex, const ZapretStrategyProbe::FullProbeResult& probe)
{
	if (!IsValidStrategyIndex(strategyIndex))
		return;

	const std::string& name = GetStrategyKey(strategyIndex);
	StrategyTestEntry entry = ZapretStrategyProbe::ToStoreEntry(probe, LoadStrategyEntryOrDefault(name));
	m_store.SetResult(name, entry);
	SaveStore();
	InvalidateStrategyCache();
}

void ZapretManager::RecordActiveStrategyResult(bool discord, bool youtube, bool telegram, int pingMs)
{
	FlushRuntimeTracking(false);

	std::string strategyKey;
	if (m_activeSmartStrategy)
	{
		strategyKey = SmartStrategyEngine::kLabel;
	}
	else
	{
		const int strategyIndex = m_pendingConnectivityStrategyIndex >= 0
			? m_pendingConnectivityStrategyIndex
			: m_activeStrategyIndex;
		if (!IsValidStrategyIndex(strategyIndex))
			return;
		strategyKey = GetStrategyKey(strategyIndex);
	}

	StrategyTestEntry entry = LoadStrategyEntryOrDefault(strategyKey);
	entry.discordOk = discord;
	entry.youtubeOk = youtube;
	entry.telegramOk = telegram;
	entry.pingMs = pingMs;
	ApplyDualOutageDetection(discord, youtube, entry);

	m_store.SetResult(strategyKey, entry);
	if (m_activeSmartStrategy)
	{
		const float score = SmartStrategyEngine::ScoreFromProbe(
			discord,
			youtube,
			telegram,
			pingMs,
			IsTelegramViaMtproto());
		m_smartStrategy.ConsiderCandidate(m_smartStrategy.GetActiveGenome(), score);
	}

	SaveStore();
	m_pendingConnectivityStrategyIndex = -1;
	InvalidateStrategyCache();
	MaybeAutoFailover(discord, youtube);
}

void ZapretManager::Update(float deltaTime)
{
	if (m_strategyTestState.load() == StrategyTestState::Completed)
	{
		m_strategyTestCompleteTimer -= deltaTime;
		if (m_strategyTestCompleteTimer <= 0.f)
		{
			m_strategyTestState.store(StrategyTestState::Idle);
			m_strategyTestCurrent.store(0);
		}
	}

	if (m_smartTuneState.load() == SmartStrategyTuneState::Completed)
	{
		m_smartTuneCompleteTimer -= deltaTime;
		if (m_smartTuneCompleteTimer <= 0.f)
		{
			m_smartTuneState.store(SmartStrategyTuneState::Idle);
			m_smartTuneCurrent.store(0);
		}
	}

	m_statusPollTimer += deltaTime;
	if (m_statusPollTimer >= 1.f)
	{
		m_statusPollTimer = 0.f;
		RefreshRunStatus();
	}

	if (!m_connectivityCheckRunning.load())
	{
		const bool trackRuntime = IsRunning()
			&& m_strategyTestState.load() != StrategyTestState::Running
			&& m_smartTuneState.load() != SmartStrategyTuneState::Running;
		if (trackRuntime)
			AccumulateRuntime(deltaTime);

		m_connectivityPollTimer += deltaTime;
		if (m_connectivityPollTimer >= kConnectivityIntervalSec
			&& trackRuntime)
		{
			m_connectivityPollTimer = 0.f;
			RequestConnectivityCheck(m_activeSmartStrategy ? -1 : m_activeStrategyIndex);
		}
	}

	if (!IsRunning()
		&& !m_runtimeTrackingKey.empty()
		&& m_strategyTestState.load() != StrategyTestState::Running
		&& m_smartTuneState.load() != SmartStrategyTuneState::Running)
	{
		EndRuntimeTracking();
	}
}

float ZapretManager::GetConnectivityCountdownSeconds() const
{
	if (!IsRunning() || m_connectivityCheckRunning.load())
		return kConnectivityIntervalSec;

	const float remaining = kConnectivityIntervalSec - m_connectivityPollTimer;
	return remaining > 0.f ? remaining : 0.f;
}

int ZapretManager::GetConnectivityCountdownSecondsCeil() const
{
	const float remaining = GetConnectivityCountdownSeconds();
	return static_cast<int>(remaining + 0.999f);
}

const char* ZapretManager::GetStrategyTestButtonLabel() const
{
	switch (m_strategyTestState.load())
	{
	case StrategyTestState::Running:
		return "Остановить подбор";
	case StrategyTestState::Paused:
		return "Продолжить";
	case StrategyTestState::Idle:
	default:
		return "Подбор стратегий";
	}
}

float ZapretManager::GetStrategyTestProgress() const
{
	const int total = m_strategyTestTotal.load();
	const int current = m_strategyTestCurrent.load();
	if (total <= 0)
		return 0.f;
	if (m_strategyTestState.load() == StrategyTestState::Completed)
		return 1.f;
	return static_cast<float>(current) / static_cast<float>(total);
}

int ZapretManager::GetStrategyTestCurrent() const
{
	return m_strategyTestCurrent.load();
}

int ZapretManager::GetStrategyTestTotal() const
{
	return m_strategyTestTotal.load();
}

bool ZapretManager::IsStrategySelectionInProgress() const
{
	if (m_strategyTestState.load() == StrategyTestState::Running
		|| m_strategyTestState.load() == StrategyTestState::Paused)
		return true;

	return m_smartTuneState.load() == SmartStrategyTuneState::Running;
}

void ZapretManager::HandleStrategyTestButton(ZapretStrategies::GameFilterMode gameFilterMode)
{
	switch (m_strategyTestState.load())
	{
	case StrategyTestState::Idle:
	case StrategyTestState::Completed:
		BeginStrategyTest(gameFilterMode, 0, true);
		break;
	case StrategyTestState::Running:
		if (!CanStopStrategyTest())
			break;
		PauseStrategyTest();
		break;
	case StrategyTestState::Paused:
		ResumeStrategyTest();
		break;
	}
}

void ZapretManager::BeginStrategyTest(ZapretStrategies::GameFilterMode gameFilterMode, int startIndex, bool clearResults)
{
	if (m_strategyTestState.load() == StrategyTestState::Running
		|| m_smartTuneState.load() == SmartStrategyTuneState::Running)
		return;

	m_strategyTestQuickOverride.store(-1);
	m_strategyTestOnlyIndex.store(-1);

	if (clearResults)
	{
		m_store.ClearResults();
		SaveStore();
		InvalidateStrategyCache();
	}

	m_strategyTestCompleteTimer = 0.f;
	m_strategyTestActiveIndex.store(-1);

	if (startIndex == 0)
		m_strategyTestRestoreIndex = m_activeStrategyIndex;

	m_strategyTestGameFilterMode = gameFilterMode;
	m_strategyTestStopRequested.store(false);
	m_strategyTestFullStopRequested.store(false);
	m_strategyTestStartTick.store(GetTickCount64());
	m_strategyTestTotal.store(GetVisibleStrategyCount(ShowExtraStrategies()));
	m_strategyTestCurrent.store(startIndex);
	m_strategyTestState.store(StrategyTestState::Running);

	std::thread([this, startIndex]() { RunStrategyTestLoop(startIndex); }).detach();
}

void ZapretManager::RequestSingleStrategyTest(
	int strategyIndex,
	ZapretStrategies::GameFilterMode gameFilterMode,
	bool quickTest)
{
	if (m_strategyTestState.load() == StrategyTestState::Running
		|| m_smartTuneState.load() == SmartStrategyTuneState::Running)
		return;
	if (!IsValidStrategyIndex(strategyIndex))
		return;

	m_strategyTestQuickOverride.store(quickTest ? 1 : 0);
	m_strategyTestOnlyIndex.store(strategyIndex);
	m_strategyTestCompleteTimer = 0.f;
	m_strategyTestActiveIndex.store(-1);
	m_strategyTestRestoreIndex = m_activeStrategyIndex;
	m_strategyTestGameFilterMode = gameFilterMode;
	m_strategyTestStopRequested.store(false);
	m_strategyTestFullStopRequested.store(false);
	m_strategyTestStartTick.store(GetTickCount64());
	m_strategyTestTotal.store(1);
	m_strategyTestCurrent.store(0);
	m_strategyTestState.store(StrategyTestState::Running);

	std::thread([this]() { RunStrategyTestLoop(0); }).detach();
}

void ZapretManager::PauseStrategyTest()
{
	if (m_strategyTestState.load() != StrategyTestState::Running)
		return;
	m_strategyTestStopRequested.store(true);
}

void ZapretManager::ResumeStrategyTest()
{
	if (m_strategyTestState.load() != StrategyTestState::Paused)
		return;
	BeginStrategyTest(m_strategyTestGameFilterMode, m_strategyTestResumeIndex.load(), false);
}

bool ZapretManager::CanStopStrategyTest() const
{
	if (m_strategyTestState.load() != StrategyTestState::Running)
		return true;
	const ULONGLONG elapsed = GetTickCount64() - m_strategyTestStartTick.load();
	return elapsed >= static_cast<ULONGLONG>(kStrategyTestStopDelayMs);
}

void ZapretManager::RequestStopStrategyTest()
{
	const StrategyTestState state = m_strategyTestState.load();
	if (state == StrategyTestState::Paused)
	{
		m_strategyTestResumeIndex.store(0);
		m_strategyTestCurrent.store(0);
		m_strategyTestState.store(StrategyTestState::Idle);
		AppLog::Instance().Append(LogSource::Zapret, "Подбор стратегий остановлен");
		return;
	}

	if (state != StrategyTestState::Running)
		return;

	m_strategyTestFullStopRequested.store(true);
	m_strategyTestStopRequested.store(true);
}

void ZapretManager::InterruptibleSleepMs(int totalMs)
{
	for (int elapsed = 0; elapsed < totalMs; elapsed += 50)
	{
		if (m_strategyTestStopRequested.load() || m_smartTuneStopRequested.load())
			return;
		Sleep(50);
	}
}

void ZapretManager::WaitForStoppedInterruptible(int maxWaitMs)
{
	for (int elapsed = 0; elapsed < maxWaitMs; elapsed += 100)
	{
		if (m_strategyTestStopRequested.load() || m_smartTuneStopRequested.load())
			return;
		if (!IsWinwsProcessRunning() && !IsZapretServiceRunning())
			return;
		Sleep(100);
	}
}

void ZapretManager::RestoreStrategyAfterTest(int strategyIndex)
{
	int index = strategyIndex;
	if (index < 0)
		index = GetPreferredStrategyIndex(true);
	if (index < 0)
		index = 0;

	if (!IsValidStrategyIndex(index))
		return;

	char msg[160] = {};
	snprintf(
		msg,
		sizeof msg,
		"Подбор завершён, восстановление стратегии: %s",
		GetStrategyLabel(index).c_str());
	AppLog::Instance().Append(LogSource::Zapret, msg);
	Start(index, m_strategyTestGameFilterMode, true);
}

void ZapretManager::RunStrategyTestLoop(int startIndex)
{
	m_strategyTestStopRequested.store(false);

	const int quickOverride = m_strategyTestQuickOverride.exchange(-1);
	const int onlyIndex = m_strategyTestOnlyIndex.exchange(-1);
	const bool quickTest = quickOverride >= 0
		? (quickOverride == 1)
		: (m_appSettings && m_appSettings->GetQuickStrategyTest());
	const int settleMs = quickTest ? 2000 : 5000;

	Stop();
	WaitForStoppedInterruptible(5000);

	const bool showExtra = ShowExtraStrategies();
	const int totalVisible = GetVisibleStrategyCount(showExtra);
	const bool singleMode = onlyIndex >= 0;
	const int total = singleMode ? 1 : totalVisible;
	int resumeFrom = startIndex;

	{
		char msg[192] = {};
		if (singleMode)
		{
			snprintf(
				msg,
				sizeof msg,
				"Тест стратегии: %s (%s)",
				GetStrategyLabel(onlyIndex).c_str(),
				quickTest ? "быстрый" : "полный");
		}
		else
		{
			snprintf(
				msg,
				sizeof msg,
				"Подбор стратегий: режим %s",
				quickTest ? "быстрый" : "полный (Run Tests / standard)");
		}
		AppLog::Instance().Append(LogSource::Zapret, msg);
	}

	for (int pass = startIndex; pass < total; ++pass)
	{
		if (m_strategyTestStopRequested.load())
		{
			resumeFrom = pass;
			break;
		}

		const int idx = singleMode ? onlyIndex : GetVisibleStrategyAt(pass, showExtra);
		if (idx < 0)
			break;

		m_strategyTestActiveIndex.store(idx);

		{
			char msg[160] = {};
			snprintf(
				msg,
				sizeof msg,
				"%s: %d/%d — %s",
				singleMode ? "Тест стратегии" : "Подбор стратегий",
				pass + 1,
				total,
				GetStrategyLabel(idx).c_str());
			AppLog::Instance().Append(LogSource::Zapret, msg);
		}

		if (!Start(idx, m_strategyTestGameFilterMode, false))
		{
			RecordStrategyResult(idx, false, false, false, -1);
			m_strategyTestCurrent.store(pass + 1);
			resumeFrom = pass + 1;
			Stop();
			WaitForStoppedInterruptible(5000);
			continue;
		}

		if (m_strategyTestStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			resumeFrom = pass + 1;
			break;
		}

		InterruptibleSleepMs(settleMs);

		if (m_strategyTestStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			resumeFrom = pass + 1;
			break;
		}

		m_strategyTestCheckingServices.store(true);
		if (quickTest)
		{
			const bool discord = ZapretConnectivity::ProbeDiscord();
			const bool youtube = ZapretConnectivity::ProbeYouTube();
			const bool telegram = ProbeTelegramConnectivity();
			int pingMs = -1;
			if (!m_strategyTestStopRequested.load())
				pingMs = ZapretConnectivity::MeasureIcmpPingMs();
			m_strategyTestCheckingServices.store(false);

			if (m_strategyTestStopRequested.load())
			{
				Stop();
				WaitForStoppedInterruptible(5000);
				resumeFrom = pass + 1;
				break;
			}

			m_discordOnline.store(discord);
			m_youtubeOnline.store(youtube);
			m_telegramOnline.store(telegram);
			RecordStrategyResult(idx, discord, youtube, telegram, pingMs);
		}
		else
		{
			size_t targetCount = 0;
			ZapretStrategyProbe::GetDefaultTargets(targetCount);
			{
				char msg[160] = {};
				snprintf(msg, sizeof msg, "[Подбор] standard: %zu целей…", targetCount);
				AppLog::Instance().Append(LogSource::Zapret, msg);
			}

			const ZapretStrategyProbe::FullProbeResult probe =
				ZapretStrategyProbe::RunFullStandardProbe(&m_strategyTestStopRequested);
			m_strategyTestCheckingServices.store(false);

			if (m_strategyTestStopRequested.load())
			{
				Stop();
				WaitForStoppedInterruptible(5000);
				resumeFrom = pass + 1;
				break;
			}

			m_discordOnline.store(probe.discordOk);
			m_youtubeOnline.store(probe.youtubeOk);
			m_telegramOnline.store(probe.telegramOk);
			RecordStrategyFullResult(idx, probe);
		}
		m_strategyTestCurrent.store(pass + 1);

		Stop();
		WaitForStoppedInterruptible(5000);
		m_strategyTestActiveIndex.store(-1);
		resumeFrom = pass + 1;
	}

	const bool wasStopped = m_strategyTestStopRequested.load();
	const bool allDone = resumeFrom >= total;
	const bool fullStop = m_strategyTestFullStopRequested.exchange(false);

	m_strategyTestActiveIndex.store(-1);

	if (wasStopped && !allDone)
	{
		if (fullStop || singleMode)
		{
			m_strategyTestResumeIndex.store(0);
			m_strategyTestCurrent.store(0);
			m_strategyTestState.store(StrategyTestState::Idle);
			AppLog::Instance().Append(LogSource::Zapret, singleMode ? "Тест стратегии остановлен" : "Подбор стратегий остановлен");
			RestoreStrategyAfterTest(m_strategyTestRestoreIndex);
			return;
		}

		m_strategyTestResumeIndex.store(resumeFrom);
		m_strategyTestState.store(StrategyTestState::Paused);
		RestoreStrategyAfterTest(m_strategyTestRestoreIndex);
		return;
	}

	m_strategyTestResumeIndex.store(0);
	m_strategyTestCurrent.store(total);
	m_strategyTestCompleteTimer = 3.f;
	m_strategyTestState.store(StrategyTestState::Completed);
	RestoreStrategyAfterTest(m_strategyTestRestoreIndex);
}

const char* ZapretManager::GetSmartStrategyTuneButtonLabel() const
{
	switch (m_smartTuneState.load())
	{
	case SmartStrategyTuneState::Running:
		return "Остановить подбор";
	case SmartStrategyTuneState::Idle:
	case SmartStrategyTuneState::Completed:
	default:
		return "Умная стратегия";
	}
}

float ZapretManager::GetSmartStrategyTuneProgress() const
{
	const int total = m_smartTuneTotal.load();
	const int current = m_smartTuneCurrent.load();
	if (total <= 0)
		return 0.f;
	if (m_smartTuneState.load() == SmartStrategyTuneState::Completed)
		return 1.f;
	return static_cast<float>(current) / static_cast<float>(total);
}

int ZapretManager::GetSmartStrategyTuneCurrent() const
{
	return m_smartTuneCurrent.load();
}

int ZapretManager::GetSmartStrategyTuneTotal() const
{
	return m_smartTuneTotal.load();
}

void ZapretManager::HandleSmartStrategyTuneButton(ZapretStrategies::GameFilterMode gameFilterMode)
{
	if (m_smartTuneState.load() == SmartStrategyTuneState::Running)
	{
		m_smartTuneStopRequested.store(true);
		return;
	}

	if (m_smartTuneState.load() == SmartStrategyTuneState::Idle
		|| m_smartTuneState.load() == SmartStrategyTuneState::Completed)
	{
		BeginSmartStrategyTune(gameFilterMode);
	}
}

void ZapretManager::BeginSmartStrategyTune(ZapretStrategies::GameFilterMode gameFilterMode)
{
	if (m_smartTuneState.load() == SmartStrategyTuneState::Running
		|| m_strategyTestState.load() == StrategyTestState::Running)
		return;

	if (!m_smartStrategy.IsEnabled())
	{
		AppLog::Instance().Append(LogSource::Zapret, "Сначала добавьте «Умную стратегию» в список.");
		return;
	}

	m_smartTuneCompleteTimer = 0.f;
	m_smartTuneGameFilterMode = gameFilterMode;
	m_smartTuneStopRequested.store(false);
	m_smartTuneTotal.store(SmartStrategyEngine::kDefaultTuneIterations);
	m_smartTuneCurrent.store(0);
	m_smartTuneState.store(SmartStrategyTuneState::Running);
	m_smartStrategy.ResetFromTemplate();

	std::thread([this]() { RunSmartStrategyTuneLoop(); }).detach();
}

void ZapretManager::RunSmartStrategyTuneLoop()
{
	m_smartTuneStopRequested.store(false);

	Stop();
	WaitForStoppedInterruptible(5000);

	const int total = SmartStrategyEngine::kDefaultTuneIterations;
	std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));

	for (int idx = 0; idx < total; ++idx)
	{
		if (m_smartTuneStopRequested.load())
			break;

		const SmartStrategyGenome genome = m_smartStrategy.CreateMutation(rng);

		{
			char msg[256] = {};
			snprintf(
				msg,
				sizeof msg,
				"Подбор умной: %d/%d — %s",
				idx + 1,
				total,
				genome.BuildSummary().c_str());
			AppLog::Instance().Append(LogSource::Zapret, msg);
		}

		if (!LaunchProcessGenome(genome, m_smartTuneGameFilterMode))
		{
			m_smartTuneCurrent.store(idx + 1);
			Stop();
			WaitForStoppedInterruptible(5000);
			continue;
		}

		if (m_smartTuneStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			break;
		}

		InterruptibleSleepMs(2000);

		if (m_smartTuneStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			break;
		}

		const bool discord = ZapretConnectivity::ProbeDiscord();
		const bool youtube = ZapretConnectivity::ProbeYouTube();
		const bool telegram = ProbeTelegramConnectivity();
		int pingMs = -1;
		if (!m_smartTuneStopRequested.load())
			pingMs = ZapretConnectivity::MeasureIcmpPingMs();

		if (m_smartTuneStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			break;
		}

		const bool mtproto = IsTelegramViaMtproto();
		const float score = SmartStrategyEngine::ScoreFromProbe(discord, youtube, telegram, pingMs, mtproto);
		m_discordOnline.store(discord);
		m_youtubeOnline.store(youtube);
		m_telegramOnline.store(telegram);

		StrategyTestEntry entry;
		entry.discordOk = discord;
		entry.youtubeOk = youtube;
		entry.telegramOk = telegram;
		entry.pingMs = pingMs;
		m_store.SetResult(SmartStrategyEngine::kLabel, entry);

		if (m_smartStrategy.ConsiderCandidate(genome, score))
		{
			char msg[384] = {};
			snprintf(
				msg,
				sizeof msg,
				"[Smart] Новый лучший конфиг: %s (score %.1f)",
				genome.BuildSummary().c_str(),
				score);
			AppLog::Instance().Append(LogSource::Zapret, msg);
		}

		m_smartTuneCurrent.store(idx + 1);
		Stop();
		WaitForStoppedInterruptible(5000);
	}

	SaveStore();
	InvalidateStrategyCache();

	const bool wasStopped = m_smartTuneStopRequested.load();
	if (wasStopped)
	{
		m_smartTuneState.store(SmartStrategyTuneState::Idle);
		AppLog::Instance().Append(LogSource::Zapret, "Подбор умной стратегии остановлен.");
	}
	else
	{
		m_smartTuneCompleteTimer = 3.f;
		m_smartTuneState.store(SmartStrategyTuneState::Completed);
		AppLog::Instance().Append(LogSource::Zapret, "Подбор умной стратегии завершён.");
	}

	StartSmartStrategy(m_smartTuneGameFilterMode, true);
}

bool ZapretManager::Start(int strategyIndex, ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy)
{
	if (!IsValidStrategyIndex(strategyIndex))
	{
		m_lastError = "Некорректная стратегия";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	Stop();
	WaitForStopped(2000);

	if (!LaunchProcess(strategyIndex, gameFilterMode))
		return false;

	m_activeSmartStrategy = false;
	m_activeStrategyIndex = strategyIndex;
	m_activeGameFilterMode = gameFilterMode;
	BeginRuntimeTracking(GetStrategyKey(strategyIndex));
	if (saveLastStrategy)
	{
		m_store.SetLastStrategy(GetStrategyKey(strategyIndex));
		SaveStore();
	}
	RefreshRunStatus();
	m_connectivityPollTimer = 0.f;
	return true;
}

bool ZapretManager::StartSmartStrategy(ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy)
{
	const SmartStrategyGenome& genome = m_smartStrategy.GetActiveGenome();
	if (genome.args.empty())
	{
		m_lastError = "Умная стратегия не настроена";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	const SmartStrategyStatus status = GetSmartStrategyStatus(IsTelegramViaMtproto());
	char msg[384] = {};
	snprintf(msg, sizeof msg, "[Smart] %s", status.explanation.c_str());
	AppLog::Instance().Append(LogSource::Zapret, msg);

	Stop();
	WaitForStopped(2000);

	if (!LaunchProcessGenome(genome, gameFilterMode))
		return false;

	m_activeSmartStrategy = true;
	m_activeStrategyIndex = -1;
	m_activeGameFilterMode = gameFilterMode;
	BeginRuntimeTracking(SmartStrategyEngine::kLabel);
	if (saveLastStrategy)
	{
		m_store.SetLastStrategy(SmartStrategyEngine::kLabel);
		SaveStore();
	}
	RefreshRunStatus();
	m_connectivityPollTimer = 0.f;
	return true;
}

bool ZapretManager::Restart()
{
	if (m_activeSmartStrategy)
		return StartSmartStrategy(m_activeGameFilterMode, true);

	if (m_activeStrategyIndex < 0)
		return Start(0, m_activeGameFilterMode);

	return Start(m_activeStrategyIndex, m_activeGameFilterMode);
}

void ZapretManager::Stop()
{
	EndRuntimeTracking();
	m_connectivityPollTimer = 0.f;
	m_connectivityCheckRunning.store(false);

	TerminateAllWinwsProcesses();
	StopZapretService();

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			TerminateProcess(m_process, 0);
			CloseHandle(m_process);
			m_process = nullptr;
		}
	}

	m_activeStrategyIndex = -1;
	m_activeSmartStrategy = false;
	m_runStatus = ZapretRunStatus::Stopped;
	AppLog::Instance().Append(LogSource::Zapret, "Антизапрет остановлен.");
}

void ZapretManager::RequestStart(
	int strategyIndex,
	ZapretStrategies::GameFilterMode gameFilterMode,
	bool saveLastStrategy)
{
	m_runStatus = ZapretRunStatus::Starting;
	std::thread([this, strategyIndex, gameFilterMode, saveLastStrategy]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_opInFlight.store(true);
		Start(strategyIndex, gameFilterMode, saveLastStrategy);
		m_opInFlight.store(false);
		RefreshRunStatus();
	}).detach();
}

void ZapretManager::RequestStartSmartStrategy(
	ZapretStrategies::GameFilterMode gameFilterMode,
	bool saveLastStrategy)
{
	m_runStatus = ZapretRunStatus::Starting;
	std::thread([this, gameFilterMode, saveLastStrategy]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_opInFlight.store(true);
		StartSmartStrategy(gameFilterMode, saveLastStrategy);
		m_opInFlight.store(false);
		RefreshRunStatus();
	}).detach();
}

void ZapretManager::RequestStop()
{
	std::thread([this]()
	{
		std::lock_guard<std::mutex> lock(m_opMutex);
		m_opInFlight.store(true);
		Stop();
		m_opInFlight.store(false);
	}).detach();
}

ZapretRunStatus ZapretManager::GetCachedRunStatus() const
{
	if (m_opInFlight.load())
		return ZapretRunStatus::Starting;
	return m_runStatus;
}

bool ZapretManager::IsRunning() const
{
	return m_runStatus == ZapretRunStatus::Running;
}

ZapretRunStatus ZapretManager::GetRunStatus() const
{
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			DWORD exitCode = 0;
			if (GetExitCodeProcess(m_process, &exitCode) && exitCode == STILL_ACTIVE)
				return ZapretRunStatus::Running;
		}
	}

	if (IsWinwsProcessRunning())
		return ZapretRunStatus::Running;

	if (IsZapretServiceStarting())
		return ZapretRunStatus::Starting;

	if (IsZapretServiceRunning())
		return ZapretRunStatus::Running;

	return ZapretRunStatus::Stopped;
}

void ZapretManager::RequestConnectivityCheck(int strategyIndex)
{
	if (m_connectivityCheckRunning.exchange(true))
		return;

	m_connectivityPollTimer = 0.f;
	m_pendingConnectivityStrategyIndex = strategyIndex;

	std::thread([this]() { RunConnectivityCheckAsync(); }).detach();
}

bool ZapretManager::ProbeTelegramConnectivity() const
{
	if (m_tgProxyManager && m_tgProxyManager->IsRunning())
		return m_tgProxyManager->ProbeTelegramConnectivity();
	return ZapretConnectivity::ProbeTelegram();
}

void ZapretManager::RunConnectivityCheckAsync()
{
	const bool discord = ZapretConnectivity::ProbeDiscord();
	const bool youtube = ZapretConnectivity::ProbeYouTube();
	const bool telegram = ProbeTelegramConnectivity();
	const int pingMs = ZapretConnectivity::MeasureIcmpPingMs();

	m_discordOnline.store(discord);
	m_youtubeOnline.store(youtube);
	m_telegramOnline.store(telegram);
	RecordActiveStrategyResult(discord, youtube, telegram, pingMs);
	m_connectivityPollTimer = 0.f;
	m_connectivityCheckRunning.store(false);
}

void ZapretManager::PrepareEnvironment()
{
	const std::wstring root = ZapretPaths::GetAntiZapretDirectory();
	EnsureTcpTimestampsEnabled();
	ZapretPaths::EnsureUserListsFiles(root);
	StartWinDivertServices();
}

void ZapretManager::WaitForStopped(int maxWaitMs)
{
	for (int elapsed = 0; elapsed < maxWaitMs; elapsed += 100)
	{
		if (!IsWinwsProcessRunning() && !IsZapretServiceRunning())
			return;
		Sleep(100);
	}
}

bool ZapretManager::LaunchProcess(int strategyIndex, ZapretStrategies::GameFilterMode gameFilterMode)
{
	m_lastError.clear();

	const std::wstring root = ZapretPaths::GetAntiZapretDirectory();
	if (!ZapretPaths::IsValidLayout(root))
	{
		m_lastError = "Не найдены bin\\winws.exe и папка lists рядом с AntiZapret.exe";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	PrepareEnvironment();

	if (!IsValidStrategyIndex(strategyIndex))
	{
		m_lastError = "Некорректная стратегия";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}
	const RuntimeStrategy& strategy = m_strategies[static_cast<size_t>(strategyIndex)];
	const std::wstring binDir = ZapretPaths::GetBinDirectory();
	const std::wstring listsDir = ZapretPaths::GetListsDirectory();
	const ZapretStrategies::GameFilterValues filters = ZapretStrategies::GetGameFilterValues(gameFilterMode);
	const std::string binPath = ZapretPaths::WideToUtf8(binDir) + "\\";
	const std::string listsPath = ZapretPaths::WideToUtf8(listsDir) + "\\";
	const std::string argsUtf8 = strategy.staticDefinition
		? StrategyArgumentBuilder::BuildCommandLine(
			*strategy.staticDefinition, gameFilterMode, ZapretPaths::WideToUtf8(binDir), ZapretPaths::WideToUtf8(listsDir))
		: StrategyBatParser::BuildExpandedArgsFromBat(
			strategy.batPath, binPath, listsPath,
			std::string(filters.gameFilterTcp), std::string(filters.gameFilterUdp));
	if (argsUtf8.empty())
	{
		m_lastError = "Не удалось собрать аргументы стратегии";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	const std::wstring winwsPath = binDir + L"\\winws.exe";
	const std::wstring argsWide = ZapretPaths::Utf8ToWide(argsUtf8);
	std::wstring commandLine = L"\"" + winwsPath + L"\" " + argsWide;
	std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
	commandBuffer.push_back(L'\0');

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};

	if (!ProcessJob::CreateInJob(
		nullptr,
		commandBuffer.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		binDir.c_str(),
		&si,
		&pi))
	{
		m_lastError = "Не удалось запустить winws.exe (код " + std::to_string(::GetLastError()) + ")";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	CloseHandle(pi.hThread);

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
			CloseHandle(m_process);
		m_process = pi.hProcess;
	}

	Sleep(300);
	DWORD exitCode = 0;
	if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
	{
		m_lastError = "winws.exe завершился сразу после запуска (код " + std::to_string(exitCode) + ")";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		{
			std::lock_guard<std::mutex> lock(m_processMutex);
			CloseHandle(m_process);
			m_process = nullptr;
		}
		return false;
	}

	AppLog::Instance().Append(
		LogSource::Zapret,
		std::string("winws.exe запущен: ") + strategy.label);
	return true;
}

bool ZapretManager::LaunchProcessGenome(
	const SmartStrategyGenome& genome,
	ZapretStrategies::GameFilterMode gameFilterMode)
{
	m_lastError.clear();

	const std::wstring root = ZapretPaths::GetAntiZapretDirectory();
	if (!ZapretPaths::IsValidLayout(root))
	{
		m_lastError = "Не найдены bin\\winws.exe и папка lists рядом с AntiZapret.exe";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	PrepareEnvironment();

	const std::wstring binDir = ZapretPaths::GetBinDirectory();
	const std::wstring listsDir = ZapretPaths::GetListsDirectory();
	const std::string argsUtf8 = StrategyArgumentBuilder::BuildCommandLine(
		genome.args,
		gameFilterMode,
		ZapretPaths::WideToUtf8(binDir),
		ZapretPaths::WideToUtf8(listsDir));
	if (argsUtf8.empty())
	{
		m_lastError = "Не удалось собрать аргументы умной стратегии";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	const std::wstring winwsPath = binDir + L"\\winws.exe";
	const std::wstring argsWide = ZapretPaths::Utf8ToWide(argsUtf8);
	std::wstring commandLine = L"\"" + winwsPath + L"\" " + argsWide;
	std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
	commandBuffer.push_back(L'\0');

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};

	if (!ProcessJob::CreateInJob(
		nullptr,
		commandBuffer.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_NO_WINDOW,
		nullptr,
		binDir.c_str(),
		&si,
		&pi))
	{
		m_lastError = "Не удалось запустить winws.exe (код " + std::to_string(::GetLastError()) + ")";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		return false;
	}

	CloseHandle(pi.hThread);

	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
			CloseHandle(m_process);
		m_process = pi.hProcess;
	}

	Sleep(300);
	DWORD exitCode = 0;
	if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
	{
		m_lastError = "winws.exe завершился сразу после запуска (код " + std::to_string(exitCode) + ")";
		AppLog::Instance().Append(LogSource::Zapret, m_lastError);
		{
			std::lock_guard<std::mutex> lock(m_processMutex);
			CloseHandle(m_process);
			m_process = nullptr;
		}
		return false;
	}

	AppLog::Instance().Append(
		LogSource::Zapret,
		std::string("winws.exe запущен: Умная стратегия (") + genome.BuildSummary() + ")");
	return true;
}

void ZapretManager::RefreshRunStatus()
{
	if (m_opInFlight.load())
		return;

	m_runStatus = GetRunStatus();
	if (m_runStatus == ZapretRunStatus::Stopped)
	{
		std::lock_guard<std::mutex> lock(m_processMutex);
		if (m_process)
		{
			DWORD exitCode = 0;
			if (GetExitCodeProcess(m_process, &exitCode) && exitCode != STILL_ACTIVE)
			{
				CloseHandle(m_process);
				m_process = nullptr;
				m_activeStrategyIndex = -1;
			}
		}
	}
}
