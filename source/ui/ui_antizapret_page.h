#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ui/ui_types.h"
#include "zapret/zapret_diagnostics.h"
#include "zapret/zapret_types.h"

class FontManager;
class ThemeManager;
class ZapretManager;
class TgWsProxyManager;
class AppSettings;

class UiAntiZapretPage
{
public:
	void SetManager(ZapretManager* manager) { m_manager = manager; }
	void SetTgProxyManager(TgWsProxyManager* manager) { m_tgProxyManager = manager; }
	void SetAppSettings(AppSettings* settings) { m_appSettings = settings; }
	void SetStartupAutostartBypass(bool value) { m_startupAutostartBypass = value; }
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	enum class View
	{
		List,
		Detail
	};

	void TryAutoStartTgProxy();
	void ApplyAutoSelectStrategyChange(bool running, int activeStrategy);
	void ClampSelectedStrategy();
	void StartDiagnostics();
	void ApplyPendingDiagnostics();
	void DrawDetailView(ThemeManager& theme, FontManager& fonts, float width);

	ZapretManager* m_manager = nullptr;
	TgWsProxyManager* m_tgProxyManager = nullptr;
	AppSettings* m_appSettings = nullptr;
	bool m_preferencesLoaded = false;
	int m_selectedStrategy = 0;
	int m_gameFilterMode = 0;
	bool m_autoSelect = false;
	bool m_showExtraStrategies = false;
	bool m_quickStrategyTest = false;
	bool m_strategyDetailsOpen = false;
	View m_view = View::List;
	int m_detailStrategyIndex = -1;
	bool m_detailQuickTest = false;
	StrategyTestState m_prevStrategyTestState = StrategyTestState::Idle;
	ZapretRunStatus m_prevRunStatus = ZapretRunStatus::Stopped;
	bool m_startupAutostartBypass = false;

	std::atomic<bool> m_diagnosticsRunning { false };
	std::mutex m_diagnosticsMutex;
	bool m_diagnosticsPending = false;
	ZapretDiagnostics::Report m_diagnosticsReport {};
	std::string m_diagnosticsStatus;
	bool m_askClearDiscordCache = false;
	bool m_askRemoveConflicts = false;
	std::vector<std::string> m_pendingConflictServices;
};
