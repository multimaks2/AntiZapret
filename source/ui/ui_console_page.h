#pragma once

#include "app/app_log.h"
#include "ui/ui_types.h"

#include <string>
#include <vector>

class FontManager;
class ThemeManager;
struct ImGuiWindow;

class UiConsolePage
{
public:
	void SetFilterFromTab(UiTab tab);
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	void RebuildDisplayBuffer(const std::vector<LogEntry>& entries);
	void ApplyLogInertiaScroll(ImGuiWindow* logWindow, float wheelCaptured, float wheelMultiplier);

	LogFilter m_filter = LogFilter::All;
	bool m_autoScroll = true;
	std::vector<char> m_displayBuf;
	size_t m_cachedCount = 0;
	LogFilter m_cachedFilter = LogFilter::All;
	std::string m_cachedTail;

	float m_logScrollY = 0.f;
	float m_logScrollDisplay = 0.f;
	float m_logScrollVelocity = 0.f;
	bool m_logScrollReady = false;
};
