#pragma once

#include <Windows.h>
#include "imgui.h"

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
};

class ThemeManager
{
public:
	void Update(float deltaTime);
	void SetLight(bool light);
	void SetMix(float mix);
	void SetAnimationPaused(bool paused);

	float GetMix() const { return m_mix; }
	bool IsLight() const { return m_light; }
	UiThemeColors GetColors() const;
	UiAccentColors GetAccents() const;
	void ApplySystemTheme(HWND hwnd) const;
	float GetClearColorRGBA(int channel) const;

private:
	static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t);
	static ImVec4 Rgb(int r, int g, int b, float a = 1.f);
	static UiThemeColors BuildColors(float mix);
	static UiAccentColors BuildAccents();

	bool m_light = false;
	bool m_animPaused = false;
	float m_mix = 0.f;
};
