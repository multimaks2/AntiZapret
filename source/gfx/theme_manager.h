#pragma once

#include <Windows.h>
#include "imgui.h"

enum class UiThemeId : int
{
	Dark = 0,
	Light,
	Crimson,
	Ocean,
	Emerald,
	Violet,
	Amber,
	Teal,
	Rose,
	Nord,
	Forest,
	Mocha,
	Matrix,
	Count
};

struct UiAccentColors
{
	ImVec4 ok;
	ImVec4 warn;
	ImVec4 fail;
	ImVec4 download;
	ImVec4 upload;
	ImVec4 discord;
	ImVec4 youtube;
	ImVec4 telegram;
};

struct UiThemeColors
{
	ImVec4 bg;
	ImVec4 sidebarBg;
	ImVec4 navActive;
	ImVec4 navHover;
	ImVec4 textPrimary;
	ImVec4 textMuted;
	ImVec4 tileBg;
	ImVec4 tileBorder;
	ImVec4 badgeBg;
	ImVec4 clearColor;
	ImVec4 inputBg;
	// Dark/Light keep original control look; tinted themes raise contrast.
	bool classicControls = true;
};

struct UiThemeInfo
{
	UiThemeId id;
	const char* key;
	const char* name;
	ImVec4 swatch; // preview accent for settings
	bool isLight;
};

class ThemeManager
{
public:
	static int ThemeCount() { return static_cast<int>(UiThemeId::Count); }
	static const UiThemeInfo& Info(UiThemeId id);
	static UiThemeId ThemeFromKey(const char* key);
	static const char* ThemeKey(UiThemeId id);

	void Update(float deltaTime);
	void SetTheme(UiThemeId id);
	void SetLight(bool light); // compat: Dark <-> Light
	void SetMix(float mix);    // compat: <0.5 Dark, else Light
	void SetAnimationPaused(bool paused);

	UiThemeId GetTheme() const { return m_theme; }
	float GetMix() const { return m_mix; }
	bool IsLight() const;
	UiThemeColors GetColors() const;
	UiAccentColors GetAccents() const;
	void ApplySystemTheme(HWND hwnd) const;
	float GetClearColorRGBA(int channel) const;

private:
	struct Palette
	{
		UiThemeColors colors;
		UiAccentColors accents;
		bool isLight;
	};

	static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);
	static ImVec4 Rgb(int r, int g, int b, float a = 1.f);
	static const Palette& PaletteFor(UiThemeId id);
	static UiThemeColors LerpThemeColors(const UiThemeColors& a, const UiThemeColors& b, float t);
	static UiAccentColors LerpAccents(const UiAccentColors& a, const UiAccentColors& b, float t);

	UiThemeId m_theme = UiThemeId::Dark;
	UiThemeId m_fromTheme = UiThemeId::Dark;
	bool m_animPaused = false;
	float m_mix = 0.f; // 0 = fromTheme, 1 = m_theme (transition)
	bool m_displayInit = false;
	UiThemeColors m_displayColors {};
	UiAccentColors m_displayAccents {};
};
