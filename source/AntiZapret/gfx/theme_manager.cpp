#include "gfx/theme_manager.h"

#include <cstring>
#include <dwmapi.h>
#include <cmath>

namespace
{
	constexpr UiThemeInfo kThemeInfos[] = {
		{ UiThemeId::Dark, "dark", "Тёмная", { 0.45f, 0.45f, 0.48f, 1.f }, false },
		{ UiThemeId::Light, "light", "Светлая", { 0.86f, 0.86f, 0.88f, 1.f }, true },
		{ UiThemeId::Crimson, "dracula", "Дракула", { 189 / 255.f, 147 / 255.f, 249 / 255.f, 1.f }, false },
		{ UiThemeId::Ocean, "tokyo-night", "Ночь в Токио", { 122 / 255.f, 162 / 255.f, 247 / 255.f, 1.f }, false },
		{ UiThemeId::Emerald, "everforest-dark", "Вечнозелёный лес", { 127 / 255.f, 187 / 255.f, 179 / 255.f, 1.f }, false },
		{ UiThemeId::Violet, "kanagawa-wave", "Волна в Канагаве", { 126 / 255.f, 156 / 255.f, 216 / 255.f, 1.f }, false },
		{ UiThemeId::Amber, "soft-orange", "Мягкий апельсин", { 211 / 255.f, 147 / 255.f, 104 / 255.f, 1.f }, false },
		{ UiThemeId::Teal, "teal", "Бирюза", { 0.f, 0.68f, 0.71f, 1.f }, false },
		{ UiThemeId::Rose, "rose-pine", "Розовая сосна", { 235 / 255.f, 188 / 255.f, 186 / 255.f, 1.f }, false },
		{ UiThemeId::Nord, "one-dark-pro", "Ван Дарк Про", { 97 / 255.f, 175 / 255.f, 239 / 255.f, 1.f }, false },
		{ UiThemeId::Forest, "monokai", "Монокай", { 249 / 255.f, 38 / 255.f, 114 / 255.f, 1.f }, false },
		{ UiThemeId::Mocha, "catppuccin-mocha", "Катпучино Мокко", { 203 / 255.f, 166 / 255.f, 247 / 255.f, 1.f }, false },
		{ UiThemeId::Matrix, "solarized-light", "Солнечная светлая", { 38 / 255.f, 139 / 255.f, 210 / 255.f, 1.f }, true },
		{ UiThemeId::Peach, "peach-light", "Персик", { 156 / 255.f, 83 / 255.f, 72 / 255.f, 1.f }, true },
		{ UiThemeId::Pear, "pear-light", "Груша", { 97 / 255.f, 116 / 255.f, 58 / 255.f, 1.f }, true },
		{ UiThemeId::Blueberry, "blueberry-light", "Черника", { 98 / 255.f, 91 / 255.f, 150 / 255.f, 1.f }, true },
		{ UiThemeId::Raspberry, "raspberry-light", "Малина", { 153 / 255.f, 71 / 255.f, 105 / 255.f, 1.f }, true },
		{ UiThemeId::Apricot, "apricot-light", "Абрикос", { 146 / 255.f, 98 / 255.f, 47 / 255.f, 1.f }, true },
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
	// Preserve saved selections from previous versions.
	static constexpr struct { const char* key; UiThemeId id; } legacy[] = {
		{ "crimson", UiThemeId::Crimson },
		{ "ocean", UiThemeId::Ocean },
		{ "emerald", UiThemeId::Emerald },
		{ "violet", UiThemeId::Violet },
		{ "amber", UiThemeId::Amber },
		{ "rose", UiThemeId::Rose },
		{ "nord", UiThemeId::Nord },
		{ "forest", UiThemeId::Forest },
		{ "mocha", UiThemeId::Mocha },
		{ "matrix", UiThemeId::Matrix },
		{ "gruvbox-dark", UiThemeId::Amber },
	};
	for (const auto& item : legacy)
		if (std::strcmp(item.key, key) == 0)
			return item.id;
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

UiThemeColors ThemeManager::PaletteColors(UiThemeId id)
{
	UiThemeColors colors = PaletteFor(id).colors;
	colors.classicControls = (id == UiThemeId::Dark || id == UiThemeId::Light);
	return colors;
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
	c.workspaceBg = LerpColor(a.workspaceBg, b.workspaceBg, t);
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

	// Adapt the editor palettes to the application's UI roles.
	const auto hex = [](unsigned rgb) {
		return Rgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
	};
	const auto makePalette = [&](unsigned base, unsigned panel, unsigned text,
		unsigned down, unsigned up, bool light = false) -> Palette {
		const ImVec4 bg = hex(base), tile = hex(panel), fg = hex(text);
		const ImVec4 download = hex(down), upload = hex(up);
		return {
			{ bg, LerpColor(bg, tile, 0.55f), LerpColor(tile, upload, 0.28f), LerpColor(tile, upload, 0.13f),
			  fg, LerpColor(fg, tile, 0.28f), tile, LerpColor(tile, fg, 0.20f),
			  LerpColor(tile, upload, 0.15f), bg, LerpColor(tile, fg, 0.10f) },
			{ download, warnAmber, failRed, download, upload, brandDiscord, brandYoutube, brandTelegram }, light
		};
	};
	static const Palette kPalettes[] = {
		// Dark — graphite workspace with a matching sidebar seam.
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
		makePalette(0x282a36, 0x44475a, 0xf8f8f2, 0x50fa7b, 0xbd93f9), // Dracula
		makePalette(0x1a1b26, 0x24283b, 0xc0caf5, 0x9ece6a, 0x7aa2f7), // Tokyo Night
		makePalette(0x2d353b, 0x343f44, 0xd3c6aa, 0xa7c080, 0x7fbbb3), // Everforest Dark
		makePalette(0x1f1f28, 0x2a2a37, 0xdcd7ba, 0x98bb6c, 0x7e9cd8), // Kanagawa Wave
		makePalette(0x191818, 0x282422, 0xeee4da, 0x9cb88e, 0xd39368),
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
		makePalette(0x191724, 0x1f1d2e, 0xe0def4, 0x9ccfd8, 0xebbcba), // Rosé Pine
		makePalette(0x282c34, 0x21252b, 0xabb2bf, 0x98c379, 0x61afef), // One Dark Pro
		makePalette(0x272822, 0x333428, 0xf8f8f2, 0xa6e22e, 0xf92672), // Monokai
		makePalette(0x1e1e2e, 0x313244, 0xcdd6f4, 0xa6e3a1, 0xcba6f7), // Catppuccin Mocha
		makePalette(0xfdf6e3, 0xeee8d5, 0x073642, 0x859900, 0x268bd2, true), // Solarized Light
		makePalette(0xfff4ef, 0xf7e6df, 0x3e2b28, 0x4e7465, 0x9c5348, true),
		makePalette(0xf7faef, 0xeaf0dd, 0x303825, 0x48746e, 0x61743a, true),
		makePalette(0xf6f5fd, 0xeae8f6, 0x302d47, 0x486f87, 0x625b96, true),
		makePalette(0xfdf3f7, 0xf4e3eb, 0x422733, 0x537568, 0x994769, true),
		makePalette(0xfff8ec, 0xf5ead4, 0x3e3021, 0x647743, 0x92622f, true),
	};
	static_assert(sizeof(kPalettes) / sizeof(kPalettes[0]) == static_cast<int>(UiThemeId::Count));

	const int index = static_cast<int>(id);
	if (index < 0 || index >= static_cast<int>(UiThemeId::Count))
		return kPalettes[0];
	return kPalettes[index];
}
