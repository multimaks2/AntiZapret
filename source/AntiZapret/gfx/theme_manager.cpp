#include "gfx/theme_manager.h"

#include <cstring>
#include <dwmapi.h>
#include <cmath>

namespace
{
	constexpr UiThemeInfo kThemeInfos[] = {
		{ UiThemeId::Dark, "dark", "Тёмная", { 0.45f, 0.45f, 0.48f, 1.f }, false },
		{ UiThemeId::Light, "light", "Светлая", { 0.86f, 0.86f, 0.88f, 1.f }, true },
		{ UiThemeId::Crimson, "crimson", "Багровая", { 0.85f, 0.25f, 0.25f, 1.f }, false },
		{ UiThemeId::Ocean, "ocean", "Океан", { 0.43f, 0.67f, 0.85f, 1.f }, false },
		{ UiThemeId::Emerald, "emerald", "Изумруд", { 0.55f, 0.68f, 0.40f, 1.f }, false },
		{ UiThemeId::Violet, "violet", "Фиолет", { 0.68f, 0.29f, 0.88f, 1.f }, false },
		{ UiThemeId::Amber, "amber", "Янтарь", { 1.f, 0.40f, 0.f, 1.f }, false },
		{ UiThemeId::Teal, "teal", "Бирюза", { 0.f, 0.68f, 0.71f, 1.f }, false },
		{ UiThemeId::Rose, "rose", "Роза", { 0.94f, 0.53f, 0.68f, 1.f }, false },
		{ UiThemeId::Nord, "nord", "Nord", { 0.27f, 0.41f, 0.51f, 1.f }, false },
		{ UiThemeId::Forest, "forest", "Лес", { 0.12f, 0.49f, 0.33f, 1.f }, false },
		{ UiThemeId::Mocha, "mocha", "Мокко", { 0.64f, 0.48f, 0.36f, 1.f }, false },
		{ UiThemeId::Matrix, "matrix", "Матрица", { 0.f, 1.f, 0.25f, 1.f }, false },
	};
	static_assert(sizeof(kThemeInfos) / sizeof(kThemeInfos[0]) == static_cast<int>(UiThemeId::Count));
}

const UiThemeInfo& ThemeManager::Info(UiThemeId id)
{
	const int index = static_cast<int>(id);
	if (index < 0 || index >= static_cast<int>(UiThemeId::Count))
		return kThemeInfos[0];
	return kThemeInfos[index];
}

UiThemeId ThemeManager::ThemeFromKey(const char* key)
{
	if (!key || !key[0])
		return UiThemeId::Dark;
	for (const UiThemeInfo& info : kThemeInfos)
	{
		if (std::strcmp(info.key, key) == 0)
			return info.id;
	}
	return UiThemeId::Dark;
}

const char* ThemeManager::ThemeKey(UiThemeId id)
{
	return Info(id).key;
}

void ThemeManager::Update(float deltaTime)
{
	if (m_animPaused)
		return;

	constexpr float kAnimSpeed = 10.f;
	const float k = 1.f - expf(-deltaTime * kAnimSpeed);
	m_mix += (1.f - m_mix) * k;
	if (m_mix > 0.999f)
		m_mix = 1.f;

	const Palette& from = PaletteFor(m_fromTheme);
	const Palette& to = PaletteFor(m_theme);
	const UiThemeColors targetColors = LerpThemeColors(from.colors, to.colors, m_mix);
	const UiAccentColors targetAccents = LerpAccents(from.accents, to.accents, m_mix);

	if (!m_displayInit)
	{
		m_displayColors = targetColors;
		m_displayAccents = targetAccents;
		m_displayInit = true;
		return;
	}

	m_displayColors = LerpThemeColors(m_displayColors, targetColors, k);
	m_displayAccents = LerpAccents(m_displayAccents, targetAccents, k);
}

void ThemeManager::SetTheme(UiThemeId id)
{
	if (id < UiThemeId::Dark || id >= UiThemeId::Count)
		id = UiThemeId::Dark;
	if (id == m_theme && m_mix >= 0.999f)
		return;

	if (m_mix < 0.999f)
	{
		// Capture mid-transition as new start.
		m_fromTheme = m_theme;
	}
	else
	{
		m_fromTheme = m_theme;
	}
	m_theme = id;
	m_mix = 0.f;
}

void ThemeManager::SetLight(bool light)
{
	SetTheme(light ? UiThemeId::Light : UiThemeId::Dark);
}

void ThemeManager::SetMix(float mix)
{
	if (mix < 0.f)
		mix = 0.f;
	if (mix > 1.f)
		mix = 1.f;
	SetTheme(mix >= 0.5f ? UiThemeId::Light : UiThemeId::Dark);
	m_mix = 1.f;
	m_fromTheme = m_theme;
	m_displayInit = false;
}

void ThemeManager::SetAnimationPaused(bool paused)
{
	m_animPaused = paused;
}

bool ThemeManager::IsLight() const
{
	return PaletteFor(m_theme).isLight;
}

UiThemeColors ThemeManager::GetColors() const
{
	UiThemeColors colors = m_displayInit ? m_displayColors : PaletteFor(m_theme).colors;
	colors.classicControls = (m_theme == UiThemeId::Dark || m_theme == UiThemeId::Light);
	return colors;
}

UiAccentColors ThemeManager::GetAccents() const
{
	if (m_displayInit)
		return m_displayAccents;
	return PaletteFor(m_theme).accents;
}

void ThemeManager::ApplySystemTheme(HWND hwnd) const
{
	if (!hwnd)
		return;
	const BOOL dark = IsLight() ? FALSE : TRUE;
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

UiThemeColors ThemeManager::LerpThemeColors(const UiThemeColors& a, const UiThemeColors& b, float t)
{
	UiThemeColors c = {};
	c.bg = LerpColor(a.bg, b.bg, t);
	c.sidebarBg = LerpColor(a.sidebarBg, b.sidebarBg, t);
	c.navActive = LerpColor(a.navActive, b.navActive, t);
	c.navHover = LerpColor(a.navHover, b.navHover, t);
	c.textPrimary = LerpColor(a.textPrimary, b.textPrimary, t);
	c.textMuted = LerpColor(a.textMuted, b.textMuted, t);
	c.tileBg = LerpColor(a.tileBg, b.tileBg, t);
	c.tileBorder = LerpColor(a.tileBorder, b.tileBorder, t);
	c.badgeBg = LerpColor(a.badgeBg, b.badgeBg, t);
	c.inputBg = LerpColor(a.inputBg, b.inputBg, t);
	c.clearColor = c.bg;
	return c;
}

UiAccentColors ThemeManager::LerpAccents(const UiAccentColors& a, const UiAccentColors& b, float t)
{
	UiAccentColors c = {};
	c.ok = LerpColor(a.ok, b.ok, t);
	c.warn = LerpColor(a.warn, b.warn, t);
	c.fail = LerpColor(a.fail, b.fail, t);
	c.download = LerpColor(a.download, b.download, t);
	c.upload = LerpColor(a.upload, b.upload, t);
	c.discord = LerpColor(a.discord, b.discord, t);
	c.youtube = LerpColor(a.youtube, b.youtube, t);
	c.telegram = LerpColor(a.telegram, b.telegram, t);
	return c;
}

const ThemeManager::Palette& ThemeManager::PaletteFor(UiThemeId id)
{
	// Brand accents stay recognizable across themes.
	const ImVec4 brandDiscord = Rgb(88, 101, 242);
	const ImVec4 brandYoutube = Rgb(255, 0, 0);
	const ImVec4 brandTelegram = Rgb(42, 171, 238);
	const ImVec4 warnAmber = Rgb(230, 170, 50);
	const ImVec4 failRed = Rgb(196, 72, 72);

	// Palettes sourced from Color Hunt (colorhunt.co) 4-swatch sets,
	// expanded to UI roles: bg / sidebar / nav / text / tiles / accent.
	static const Palette kPalettes[] = {
		// Dark — original app graphite (untouched)
		{
			{
				Rgb(16, 16, 16), Rgb(20, 20, 20), Rgb(41, 41, 41), Rgb(31, 31, 31),
				Rgb(235, 235, 235), Rgb(140, 140, 140), Rgb(26, 26, 26), Rgb(46, 46, 46),
				Rgb(18, 28, 33), Rgb(16, 16, 16), { 20 / 255.f, 20 / 255.f, 20 / 255.f, 0.65f }
			},
			{ Rgb(33, 176, 77), warnAmber, failRed, Rgb(33, 176, 77), Rgb(80, 160, 255), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Light — original app light (stronger text contrast)
		{
			{
				Rgb(245, 245, 247), Rgb(237, 237, 240), Rgb(219, 219, 224), Rgb(230, 230, 234),
				Rgb(12, 12, 14), Rgb(78, 78, 86), Rgb(255, 255, 255), Rgb(214, 214, 219),
				Rgb(235, 245, 247), Rgb(245, 245, 247), { 237 / 255.f, 237 / 255.f, 240 / 255.f, 0.65f }
			},
			{ Rgb(33, 176, 77), warnAmber, failRed, Rgb(33, 176, 77), Rgb(50, 120, 220), brandDiscord, brandYoutube, brandTelegram },
			true
		},
		// Crimson — colorhunt.co/palette/1d16168e1616d84040eeeeee
		{
			{
				Rgb(29, 22, 22), Rgb(40, 28, 28), Rgb(142, 22, 22), Rgb(60, 32, 32),
				Rgb(238, 238, 238), Rgb(180, 120, 120), Rgb(40, 28, 28), Rgb(100, 40, 40),
				Rgb(50, 30, 30), Rgb(29, 22, 22), { 40 / 255.f, 28 / 255.f, 28 / 255.f, 0.65f }
			},
			{ Rgb(216, 64, 64), warnAmber, Rgb(255, 100, 100), Rgb(216, 64, 64), Rgb(230, 140, 140), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Ocean — colorhunt.co/palette/02152603346e6eacdae2e2b6
		{
			{
				Rgb(2, 21, 38), Rgb(6, 36, 70), Rgb(3, 52, 110), Rgb(8, 42, 80),
				Rgb(226, 226, 182), Rgb(110, 172, 218), Rgb(6, 36, 70), Rgb(20, 70, 130),
				Rgb(8, 42, 80), Rgb(2, 21, 38), { 6 / 255.f, 36 / 255.f, 70 / 255.f, 0.65f }
			},
			{ Rgb(110, 172, 218), warnAmber, failRed, Rgb(80, 180, 150), Rgb(110, 172, 218), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Emerald — colorhunt.co/palette/1b211a6281418bae66ebd5ab
		{
			{
				Rgb(27, 33, 26), Rgb(40, 50, 38), Rgb(98, 129, 65), Rgb(55, 70, 50),
				Rgb(235, 213, 171), Rgb(139, 174, 102), Rgb(40, 50, 38), Rgb(80, 110, 60),
				Rgb(45, 55, 40), Rgb(27, 33, 26), { 40 / 255.f, 50 / 255.f, 38 / 255.f, 0.65f }
			},
			{ Rgb(139, 174, 102), warnAmber, failRed, Rgb(139, 174, 102), Rgb(160, 200, 140), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Violet — colorhunt.co/palette/2e073f7a1cacad49e1ebd3f8
		{
			{
				Rgb(46, 7, 63), Rgb(70, 20, 100), Rgb(122, 28, 172), Rgb(90, 28, 140),
				Rgb(235, 211, 248), Rgb(173, 73, 225), Rgb(70, 20, 100), Rgb(130, 50, 180),
				Rgb(80, 25, 120), Rgb(46, 7, 63), { 70 / 255.f, 20 / 255.f, 100 / 255.f, 0.65f }
			},
			{ Rgb(173, 73, 225), warnAmber, failRed, Rgb(120, 200, 140), Rgb(190, 140, 255), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Amber — colorhunt.co/palette/ff65001e3e620b192c000000
		{
			{
				Rgb(0, 0, 0), Rgb(11, 25, 44), Rgb(30, 62, 98), Rgb(18, 40, 70),
				Rgb(255, 220, 190), Rgb(200, 140, 90), Rgb(11, 25, 44), Rgb(40, 70, 100),
				Rgb(18, 40, 70), Rgb(0, 0, 0), { 11 / 255.f, 25 / 255.f, 44 / 255.f, 0.65f }
			},
			{ Rgb(255, 101, 0), warnAmber, failRed, Rgb(80, 180, 110), Rgb(255, 140, 60), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Teal — colorhunt.co/palette/222831393e4600adb5eeeeee
		{
			{
				Rgb(34, 40, 49), Rgb(45, 50, 58), Rgb(57, 62, 70), Rgb(50, 55, 63),
				Rgb(238, 238, 238), Rgb(140, 170, 175), Rgb(45, 50, 58), Rgb(70, 76, 84),
				Rgb(40, 55, 58), Rgb(34, 40, 49), { 45 / 255.f, 50 / 255.f, 58 / 255.f, 0.65f }
			},
			{ Rgb(0, 173, 181), warnAmber, failRed, Rgb(0, 173, 181), Rgb(80, 200, 210), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Rose — colorhunt.co/palette/3a0519670d2fa53860ef88ad
		{
			{
				Rgb(58, 5, 25), Rgb(80, 15, 40), Rgb(103, 13, 47), Rgb(90, 20, 50),
				Rgb(239, 136, 173), Rgb(200, 100, 140), Rgb(80, 15, 40), Rgb(130, 40, 70),
				Rgb(90, 20, 50), Rgb(58, 5, 25), { 80 / 255.f, 15 / 255.f, 40 / 255.f, 0.65f }
			},
			{ Rgb(165, 56, 96), warnAmber, failRed, Rgb(90, 180, 120), Rgb(239, 136, 173), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Nord — colorhunt.co/palette/1b3c53234c6a456882d2c1b6
		{
			{
				Rgb(27, 60, 83), Rgb(35, 76, 106), Rgb(69, 104, 130), Rgb(50, 88, 115),
				Rgb(210, 193, 182), Rgb(140, 165, 180), Rgb(35, 76, 106), Rgb(80, 115, 140),
				Rgb(40, 80, 110), Rgb(27, 60, 83), { 35 / 255.f, 76 / 255.f, 106 / 255.f, 0.65f }
			},
			{ Rgb(110, 172, 180), Rgb(235, 203, 139), Rgb(191, 97, 106), Rgb(140, 180, 130), Rgb(129, 161, 193), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Forest — colorhunt.co/palette/18230f27391c255f381f7d53
		{
			{
				Rgb(24, 35, 15), Rgb(39, 57, 28), Rgb(37, 95, 56), Rgb(40, 70, 40),
				Rgb(200, 230, 200), Rgb(100, 160, 120), Rgb(39, 57, 28), Rgb(50, 100, 70),
				Rgb(40, 70, 40), Rgb(24, 35, 15), { 39 / 255.f, 57 / 255.f, 28 / 255.f, 0.65f }
			},
			{ Rgb(31, 125, 83), warnAmber, failRed, Rgb(31, 125, 83), Rgb(100, 180, 130), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Mocha — colorhunt.co/palette/2c39303f4f44a27b5cdcd7c9
		{
			{
				Rgb(44, 57, 48), Rgb(55, 70, 60), Rgb(63, 79, 68), Rgb(58, 74, 64),
				Rgb(220, 215, 201), Rgb(162, 123, 92), Rgb(55, 70, 60), Rgb(90, 100, 85),
				Rgb(58, 74, 64), Rgb(44, 57, 48), { 55 / 255.f, 70 / 255.f, 60 / 255.f, 0.65f }
			},
			{ Rgb(162, 123, 92), warnAmber, failRed, Rgb(120, 160, 100), Rgb(200, 160, 120), brandDiscord, brandYoutube, brandTelegram },
			false
		},
		// Matrix — black void + phosphor green (film look)
		{
			{
				Rgb(0, 0, 0), Rgb(4, 12, 4), Rgb(0, 48, 16), Rgb(0, 28, 10),
				Rgb(0, 255, 65), Rgb(0, 140, 40), Rgb(4, 14, 6), Rgb(0, 70, 24),
				Rgb(0, 24, 8), Rgb(0, 0, 0), { 4 / 255.f, 12 / 255.f, 4 / 255.f, 0.65f }
			},
			{ Rgb(0, 255, 65), Rgb(180, 220, 40), Rgb(255, 60, 60), Rgb(0, 220, 70), Rgb(40, 255, 120), brandDiscord, brandYoutube, brandTelegram },
			false
		},
	};
	static_assert(sizeof(kPalettes) / sizeof(kPalettes[0]) == static_cast<int>(UiThemeId::Count));

	const int index = static_cast<int>(id);
	if (index < 0 || index >= static_cast<int>(UiThemeId::Count))
		return kPalettes[0];
	return kPalettes[index];
}
