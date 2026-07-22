#pragma once

class AppSettings;
class FontManager;
class ThemeManager;
class TgWsProxyManager;

class UiTgFixPage
{
public:
	void SetManager(TgWsProxyManager* manager) { m_manager = manager; }
	void SetSettings(AppSettings* settings) { m_settings = settings; }
	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	TgWsProxyManager* m_manager = nullptr;
	AppSettings* m_settings = nullptr;
};
