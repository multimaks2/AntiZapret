#pragma once

#include "app/app_log.h"
#include "ui/ui_types.h"

class FontManager;
class ThemeManager;

class UiConsolePage
{
public:
	void SetFilterFromTab(UiTab tab);
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	LogFilter m_filter = LogFilter::All;
	bool m_autoScroll = true;
};
