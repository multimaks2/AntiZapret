#include "gfx/theme_manager.h"

#include <dwmapi.h>
#include <cmath>

void ThemeManager::Update(float deltaTime)
{
	if (m_animPaused)
		return;

	constexpr float kAnimSpeed = 10.f;
	const float target = m_light ? 1.f : 0.f;
	const float k = 1.f - expf(-deltaTime * kAnimSpeed);
	m_mix += (target - m_mix) * k;
	if (m_mix < 0.001f) m_mix = 0.f;
	if (m_mix > 0.999f) m_mix = 1.f;
}

void ThemeManager::SetLight(bool light)
{
	m_light = light;
}

void ThemeManager::SetMix(float mix)
{
	if (mix < 0.f) mix = 0.f;
	if (mix > 1.f) mix = 1.f;
	m_mix = mix;
	m_light = mix >= 0.5f;
}

void ThemeManager::SetAnimationPaused(bool paused)
{
	m_animPaused = paused;
}

UiThemeColors ThemeManager::GetColors() const
{
	return BuildColors(m_mix);
}

UiAccentColors ThemeManager::GetAccents() const
{
	(void)m_mix;
	return BuildAccents();
}

void ThemeManager::ApplySystemTheme(HWND hwnd) const
{
	if (!hwnd)
		return;
	const BOOL dark = m_mix < 0.5f ? TRUE : FALSE;
	DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

float ThemeManager::GetClearColorRGBA(int channel) const
{
	const UiThemeColors colors = GetColors();
	switch (channel)
	{
	case 0: return colors.clearColor.x;
	case 1: return colors.clearColor.y;
	case 2: return colors.clearColor.z;
	default: return colors.clearColor.w;
	}
}

ImVec4 ThemeManager::LerpColor(const ImVec4& a, const ImVec4& b, float t)
{
	return {
		a.x + (b.x - a.x) * t,
		a.y + (b.y - a.y) * t,
		a.z + (b.z - a.z) * t,
		a.w + (b.w - a.w) * t,
	};
}

ImVec4 ThemeManager::Rgb(int r, int g, int b, float a)
{
	return { r / 255.f, g / 255.f, b / 255.f, a };
}

UiThemeColors ThemeManager::BuildColors(float mix)
{
	const ImVec4 darkBg = { 0.06275f, 0.06275f, 0.06275f, 1.f };
	const ImVec4 lightBg = { 0.96f, 0.96f, 0.97f, 1.f };
	const ImVec4 darkSidebar = { 0.07843f, 0.07843f, 0.07843f, 1.f };
	const ImVec4 lightSidebar = { 0.93f, 0.93f, 0.94f, 1.f };
	const ImVec4 darkNavActive = { 0.16f, 0.16f, 0.16f, 1.f };
	const ImVec4 lightNavActive = { 0.86f, 0.86f, 0.88f, 1.f };
	const ImVec4 darkNavHover = { 0.12f, 0.12f, 0.12f, 1.f };
	const ImVec4 lightNavHover = { 0.90f, 0.90f, 0.92f, 1.f };
	const ImVec4 darkText = { 0.92f, 0.92f, 0.92f, 1.f };
	const ImVec4 lightText = { 0.12f, 0.12f, 0.14f, 1.f };
	const ImVec4 darkMuted = { 0.55f, 0.55f, 0.55f, 1.f };
	const ImVec4 lightMuted = { 0.42f, 0.42f, 0.45f, 1.f };
	const ImVec4 darkTile = { 0.10f, 0.10f, 0.10f, 1.f };
	const ImVec4 lightTile = { 1.f, 1.f, 1.f, 1.f };
	const ImVec4 darkTileBorder = { 0.18f, 0.18f, 0.18f, 1.f };
	const ImVec4 lightTileBorder = { 0.84f, 0.84f, 0.86f, 1.f };
	const ImVec4 darkBadgeBg = { 0.07f, 0.11f, 0.13f, 1.f };
	const ImVec4 lightBadgeBg = { 0.92f, 0.96f, 0.97f, 1.f };
	const ImVec4 darkInputBg = { 0.07843f, 0.07843f, 0.07843f, 0.65f };
	const ImVec4 lightInputBg = { 0.93f, 0.93f, 0.94f, 0.65f };

	UiThemeColors colors = {};
	colors.bg = LerpColor(darkBg, lightBg, mix);
	colors.sidebarBg = LerpColor(darkSidebar, lightSidebar, mix);
	colors.navActive = LerpColor(darkNavActive, lightNavActive, mix);
	colors.navHover = LerpColor(darkNavHover, lightNavHover, mix);
	colors.textPrimary = LerpColor(darkText, lightText, mix);
	colors.textMuted = LerpColor(darkMuted, lightMuted, mix);
	colors.tileBg = LerpColor(darkTile, lightTile, mix);
	colors.tileBorder = LerpColor(darkTileBorder, lightTileBorder, mix);
	colors.badgeBg = LerpColor(darkBadgeBg, lightBadgeBg, mix);
	colors.inputBg = LerpColor(darkInputBg, lightInputBg, mix);
	colors.clearColor = colors.bg;
	return colors;
}

UiAccentColors ThemeManager::BuildAccents()
{
	UiAccentColors accents = {};
	accents.ok = Rgb(33, 176, 77);
	accents.warn = Rgb(230, 170, 50);
	accents.fail = Rgb(196, 72, 72);
	accents.download = Rgb(33, 176, 77);
	accents.upload = Rgb(80, 160, 255);
	accents.discord = Rgb(88, 101, 242);
	accents.youtube = Rgb(255, 0, 0);
	accents.telegram = Rgb(42, 171, 238);
	return accents;
}
