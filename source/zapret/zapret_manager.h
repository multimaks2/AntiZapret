#pragma once

#include "zapret/strategies.hpp"
#include "zapret/smart_strategy_engine.h"
#include "zapret/zapret_store.h"
#include "zapret/zapret_types.h"

#include <Windows.h>

#include <atomic>
#include <array>
#include <mutex>
#include <string>

class AppSettings;

class ZapretManager
{
public:
	static constexpr float kConnectivityIntervalSec = 10.f;

	ZapretManager();
	~ZapretManager();

	void Update(float deltaTime);

	void SetTgProxyManager(class TgWsProxyManager* manager) { m_tgProxyManager = manager; }
	void SetAppSettings(AppSettings* settings) { m_appSettings = settings; }

	bool Start(int strategyIndex, ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy = true);
	bool StartSmartStrategy(ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy = true);
	void RequestStart(int strategyIndex, ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy = true);
	void RequestStartSmartStrategy(ZapretStrategies::GameFilterMode gameFilterMode, bool saveLastStrategy = true);
	bool Restart();
	void Stop();
	void RequestStop();

	ZapretRunStatus GetRunStatus() const;
	ZapretRunStatus GetCachedRunStatus() const;
	bool IsOperationInFlight() const { return m_opInFlight.load(); }
	bool IsRunning() const;
	int GetActiveStrategyIndex() const { return m_activeStrategyIndex; }
	bool IsActiveSmartStrategy() const { return m_activeSmartStrategy; }
	bool IsSmartStrategyEnabled() const { return m_smartStrategy.IsEnabled(); }
	void SetSmartStrategyEnabled(bool enabled);
	SmartStrategyStatus GetSmartStrategyStatus(bool telegramViaMtproto) const;
	ZapretStrategies::GameFilterMode GetActiveGameFilterMode() const { return m_activeGameFilterMode; }

	const std::string& GetErrorMessage() const { return m_lastError; }

	void LoadStore();
	void SaveStore();
	int GetPreferredStrategyIndex(bool autoSelectBest) const;
	int GetCachedBestStrategyIndex() const;
	const ZapretStore& GetStore() const { return m_store; }
	const StrategyTestEntry* GetStrategyResult(int strategyIndex) const;

	void RequestConnectivityCheck(int strategyIndex = -1);
	bool IsCheckingConnectivity() const { return m_connectivityCheckRunning.load(); }
	bool IsStrategyTestCheckingServices() const { return m_strategyTestCheckingServices.load(); }
	bool IsDiscordOnline() const { return m_discordOnline.load(); }
	bool IsYouTubeOnline() const { return m_youtubeOnline.load(); }
	bool IsTelegramOnline() const { return m_telegramOnline.load(); }
	float GetConnectivityCountdownSeconds() const;
	int GetConnectivityCountdownSecondsCeil() const;

	void HandleStrategyTestButton(ZapretStrategies::GameFilterMode gameFilterMode);
	StrategyTestState GetStrategyTestState() const { return m_strategyTestState.load(); }
	const char* GetStrategyTestButtonLabel() const;
	float GetStrategyTestProgress() const;
	int GetStrategyTestCurrent() const;
	int GetStrategyTestTotal() const;
	int GetStrategyTestActiveIndex() const { return m_strategyTestActiveIndex.load(); }
	bool IsStrategySelectionInProgress() const;

	void HandleSmartStrategyTuneButton(ZapretStrategies::GameFilterMode gameFilterMode);
	SmartStrategyTuneState GetSmartStrategyTuneState() const { return m_smartTuneState.load(); }
	const char* GetSmartStrategyTuneButtonLabel() const;
	float GetSmartStrategyTuneProgress() const;
	int GetSmartStrategyTuneCurrent() const;
	int GetSmartStrategyTuneTotal() const;

private:
	bool LaunchProcess(int strategyIndex, ZapretStrategies::GameFilterMode gameFilterMode);
	bool LaunchProcessGenome(const SmartStrategyGenome& genome, ZapretStrategies::GameFilterMode gameFilterMode);
	void PrepareEnvironment();
	void WaitForStopped(int maxWaitMs);
	void WaitForStoppedInterruptible(int maxWaitMs);
	void RefreshRunStatus();
	void RunConnectivityCheckAsync();
	void RecordStrategyResult(int strategyIndex, bool discord, bool youtube, bool telegram, int pingMs);
	void RecordActiveStrategyResult(bool discord, bool youtube, bool telegram, int pingMs);
	void BeginStrategyTest(ZapretStrategies::GameFilterMode gameFilterMode, int startIndex, bool clearResults);
	void PauseStrategyTest();
	void ResumeStrategyTest();
	void RunStrategyTestLoop(int startIndex);
	void BeginSmartStrategyTune(ZapretStrategies::GameFilterMode gameFilterMode);
	void RunSmartStrategyTuneLoop();
	void InterruptibleSleepMs(int totalMs);
	void RestoreStrategyAfterTest(int strategyIndex);
	void InvalidateStrategyCache();
	void EnsureStrategyCache() const;
	bool ShowExtraStrategies() const;
	int GetBestVisibleStrategyIndex() const;
	int ClampStrategyIndexToVisible(int strategyIndex) const;
	bool ProbeTelegramConnectivity() const;
	bool IsTelegramViaMtproto() const;

	std::string GetActiveStrategyStoreKey() const;
	void BeginRuntimeTracking(const std::string& strategyKey);
	void EndRuntimeTracking();
	void AccumulateRuntime(float deltaTime);
	void FlushRuntimeTracking(bool saveToDisk);
	void ApplyDualOutageDetection(bool discordOk, bool youtubeOk, StrategyTestEntry& entry);
	StrategyTestEntry LoadStrategyEntryOrDefault(const std::string& strategyKey) const;
	int GetBestVisibleStrategyByStats(int excludeIndex) const;
	int FindFallbackStrategyIndex(int failedIndex) const;
	void MaybeAutoFailover(bool discordOk, bool youtubeOk);

	static constexpr int kAutoFailoverConfirmChecks = 2;

	HANDLE m_process = nullptr;
	mutable std::mutex m_processMutex;

	int m_activeStrategyIndex = -1;
	bool m_activeSmartStrategy = false;
	ZapretStrategies::GameFilterMode m_activeGameFilterMode = ZapretStrategies::GameFilterMode::Disabled;
	ZapretRunStatus m_runStatus = ZapretRunStatus::Stopped;
	std::string m_lastError;

	std::mutex m_opMutex;
	std::atomic<bool> m_opInFlight { false };

	float m_statusPollTimer = 0.f;
	float m_connectivityPollTimer = 0.f;

	std::atomic<bool> m_connectivityCheckRunning { false };
	std::atomic<bool> m_discordOnline { false };
	std::atomic<bool> m_youtubeOnline { false };
	std::atomic<bool> m_telegramOnline { false };
	int m_pendingConnectivityStrategyIndex = -1;

	std::atomic<StrategyTestState> m_strategyTestState { StrategyTestState::Idle };
	std::atomic<bool> m_strategyTestStopRequested { false };
	std::atomic<int> m_strategyTestCurrent { 0 };
	std::atomic<int> m_strategyTestTotal { 0 };
	std::atomic<int> m_strategyTestResumeIndex { 0 };
	std::atomic<int> m_strategyTestActiveIndex { -1 };
	std::atomic<bool> m_strategyTestCheckingServices { false };
	float m_strategyTestCompleteTimer = 0.f;
	int m_strategyTestRestoreIndex = -1;
	ZapretStrategies::GameFilterMode m_strategyTestGameFilterMode = ZapretStrategies::GameFilterMode::Disabled;

	std::atomic<SmartStrategyTuneState> m_smartTuneState { SmartStrategyTuneState::Idle };
	std::atomic<bool> m_smartTuneStopRequested { false };
	std::atomic<int> m_smartTuneCurrent { 0 };
	std::atomic<int> m_smartTuneTotal { 0 };
	float m_smartTuneCompleteTimer = 0.f;
	ZapretStrategies::GameFilterMode m_smartTuneGameFilterMode = ZapretStrategies::GameFilterMode::Disabled;

	ZapretStore m_store;
	SmartStrategyEngine m_smartStrategy;
	mutable std::array<const StrategyTestEntry*, ZapretStrategies::kStrategyCount> m_strategyResultCache {};
	mutable int m_cachedPreferredBestIndex = -1;
	mutable bool m_strategyCacheValid = false;

	std::string m_runtimeTrackingKey;
	float m_runtimePendingSec = 0.f;
	bool m_connectivityObserved = false;
	bool m_dualOutageActive = false;
	int m_consecutiveDualFailures = 0;

	class TgWsProxyManager* m_tgProxyManager = nullptr;
	AppSettings* m_appSettings = nullptr;
};
