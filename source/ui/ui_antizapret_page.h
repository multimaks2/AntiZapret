#pragma once

#include <cstdint>

#include "ui/ui_types.h"
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
	void AddSmartStrategy();
	void TryAutoStartTgProxy();
	void ApplyAutoSelectStrategyChange(bool running, int activeStrategy);
	void ClampSelectedStrategy();

	ZapretManager* m_manager = nullptr;
	TgWsProxyManager* m_tgProxyManager = nullptr;
	AppSettings* m_appSettings = nullptr;
	bool m_preferencesLoaded = false;
	int m_selectedStrategy = 0;
	int m_selectedSmartIndex = -1;
	int m_gameFilterMode = 0;
	bool m_autoSelect = false;
	bool m_showExtraStrategies = false;
	bool m_hasSmartStrategy = false;
	bool m_strategyDetailsOpen = false;
	StrategyTestState m_prevStrategyTestState = StrategyTestState::Idle;
	ZapretRunStatus m_prevRunStatus = ZapretRunStatus::Stopped;
	bool m_startupAutostartBypass = false;
};
