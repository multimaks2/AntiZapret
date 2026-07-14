#include "zapret/zapret_connectivity.h"
#include "zapret/zapret_manager.h"

#include <climits>

#include "app/app_log.h"
#include "app/app_settings.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "zapret/strategy_argument_builder.h"
#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#include <vector>

namespace
{
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

void ZapretManager::SaveStore()
{
	m_store.Save();
}

void ZapretManager::InvalidateStrategyCache()
{
	m_strategyCacheValid = false;
}

bool ZapretManager::ShowExtraStrategies() const
{
	return m_appSettings && m_appSettings->GetShowExtraStrategies();
}

int ZapretManager::ClampStrategyIndexToVisible(int strategyIndex) const
{
	const bool showExtra = ShowExtraStrategies();
	if (ZapretStrategies::IsStrategyVisible(strategyIndex, showExtra))
		return strategyIndex;

	const int fallback = ZapretStrategies::GetVisibleStrategyAt(0, showExtra);
	return fallback >= 0 ? fallback : 0;
}

int ZapretManager::GetBestVisibleStrategyIndex() const
{
	const bool showExtra = ShowExtraStrategies();
	int bestIndex = -1;
	int maxScore = -1;
	int bestPingMs = -1;

	for (std::size_t i = 0; i < ZapretStrategies::kStrategyCount; ++i)
	{
		if (!ZapretStrategies::IsStrategyVisible(static_cast<int>(i), showExtra))
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

	for (std::size_t i = 0; i < ZapretStrategies::kStrategyCount; ++i)
	{
		const std::string name(ZapretStrategies::kStrategies[i].id);
		m_strategyResultCache[i] = m_store.GetResult(name);
	}

	m_cachedPreferredBestIndex = GetBestVisibleStrategyIndex();
	m_strategyCacheValid = true;
}

int ZapretManager::GetPreferredStrategyIndex(bool autoSelectBest) const
{
	EnsureStrategyCache();
	if (autoSelectBest)
	{
		const int statsBest = GetBestVisibleStrategyByStats(-1);
		if (statsBest >= 0)
			return statsBest;
	}

	return ClampStrategyIndexToVisible(m_store.GetPreferredStrategyIndex(false));
}

int ZapretManager::GetBestVisibleStrategyByStats(int excludeIndex) const
{
	EnsureStrategyCache();
	const bool showExtra = ShowExtraStrategies();
	const int visibleCount = ZapretStrategies::CountVisibleStrategies(showExtra);

	int bestIndex = -1;
	int bestRuntime = 0;
	int bestOutages = INT_MAX;
	int bestOrder = INT_MAX;

	for (int pass = 0; pass < visibleCount; ++pass)
	{
		const int idx = ZapretStrategies::GetVisibleStrategyAt(pass, showExtra);
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
	const int visibleCount = ZapretStrategies::CountVisibleStrategies(showExtra);
	if (visibleCount <= 1)
		return failedIndex;

	int failedPos = ZapretStrategies::FindVisibleStrategyPosition(failedIndex, showExtra);
	if (failedPos < 0)
		failedPos = 0;

	for (int step = 1; step <= visibleCount; ++step)
	{
		const int pass = (failedPos + step) % visibleCount;
		const int idx = ZapretStrategies::GetVisibleStrategyAt(pass, showExtra);
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
		ZapretStrategies::GetStrategyLabel(currentIndex).data(),
		ZapretStrategies::GetStrategyLabel(fallbackIndex).data());
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
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return nullptr;

	EnsureStrategyCache();
	return m_strategyResultCache[static_cast<std::size_t>(strategyIndex)];
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

	if (m_activeStrategyIndex >= 0
		&& m_activeStrategyIndex < static_cast<int>(ZapretStrategies::kStrategyCount))
	{
		return std::string(ZapretStrategies::kStrategies[m_activeStrategyIndex].id);
	}

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
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return;

	const std::string name(ZapretStrategies::kStrategies[strategyIndex].id);
	StrategyTestEntry entry = LoadStrategyEntryOrDefault(name);
	entry.discordOk = discord;
	entry.youtubeOk = youtube;
	entry.telegramOk = telegram;
	entry.pingMs = pingMs;

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
		if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
			return;
		strategyKey = std::string(ZapretStrategies::kStrategies[strategyIndex].id);
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
	m_strategyTestTotal.store(ZapretStrategies::CountVisibleStrategies(ShowExtraStrategies()));
	m_strategyTestCurrent.store(startIndex);
	m_strategyTestState.store(StrategyTestState::Running);

	std::thread([this, startIndex]() { RunStrategyTestLoop(startIndex); }).detach();
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

	if (index < 0 || index >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return;

	char msg[160] = {};
	snprintf(
		msg,
		sizeof msg,
		"Подбор завершён, восстановление стратегии: %s",
		ZapretStrategies::GetStrategyLabel(index).data());
	AppLog::Instance().Append(LogSource::Zapret, msg);
	Start(index, m_strategyTestGameFilterMode, true);
}

void ZapretManager::RunStrategyTestLoop(int startIndex)
{
	m_strategyTestStopRequested.store(false);

	Stop();
	WaitForStoppedInterruptible(5000);

	const bool showExtra = ShowExtraStrategies();
	const int total = ZapretStrategies::CountVisibleStrategies(showExtra);
	int resumeFrom = startIndex;

	for (int pass = startIndex; pass < total; ++pass)
	{
		if (m_strategyTestStopRequested.load())
		{
			resumeFrom = pass;
			break;
		}

		const int idx = ZapretStrategies::GetVisibleStrategyAt(pass, showExtra);
		if (idx < 0)
			break;

		m_strategyTestActiveIndex.store(idx);

		{
			char msg[160] = {};
			snprintf(
				msg,
				sizeof msg,
				"Подбор стратегий: %d/%d — %s",
				pass + 1,
				total,
				ZapretStrategies::GetStrategyLabel(idx).data());
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

		InterruptibleSleepMs(2000);

		if (m_strategyTestStopRequested.load())
		{
			Stop();
			WaitForStoppedInterruptible(5000);
			resumeFrom = pass + 1;
			break;
		}

		m_strategyTestCheckingServices.store(true);
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
		m_strategyTestCurrent.store(pass + 1);

		Stop();
		WaitForStoppedInterruptible(5000);
		m_strategyTestActiveIndex.store(-1);
		resumeFrom = pass + 1;
	}

	const bool wasStopped = m_strategyTestStopRequested.load();
	const bool allDone = resumeFrom >= total;

	m_strategyTestActiveIndex.store(-1);

	if (wasStopped && !allDone)
	{
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
		return "Подбор умной";
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
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
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
	BeginRuntimeTracking(std::string(ZapretStrategies::kStrategies[strategyIndex].id));
	if (saveLastStrategy)
	{
		m_store.SetLastStrategy(std::string(ZapretStrategies::kStrategies[strategyIndex].id));
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

	const auto& strategy = ZapretStrategies::kStrategies[strategyIndex];
	const std::wstring binDir = ZapretPaths::GetBinDirectory();
	const std::wstring listsDir = ZapretPaths::GetListsDirectory();
	const std::string argsUtf8 = StrategyArgumentBuilder::BuildCommandLine(
		strategy,
		gameFilterMode,
		ZapretPaths::WideToUtf8(binDir),
		ZapretPaths::WideToUtf8(listsDir));
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

	if (!CreateProcessW(
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
		std::string("winws.exe запущен: ") + std::string(strategy.label));
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

	if (!CreateProcessW(
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
