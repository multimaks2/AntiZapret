#pragma once

#ifdef ANTIZAPRET_LAN
class ThemeManager;
class FontManager;

void UpdateLanPage(bool active);
void DrawLanPage(ThemeManager& theme, FontManager& fonts);
#endif
