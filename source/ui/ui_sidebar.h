#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"
#include "ui/ui_types.h"
#include "zapret/zapret_update_check.h"

class FontManager;
class ThemeManager;
struct UiThemeColors;
struct UiAccentColors;

// Version/update-status info for a nav tab (AntiZapret or TG WS Proxy).
struct UiSidebarVersionInfo
{
	ComponentUpdateStatus status = ComponentUpdateStatus::Unknown;
	std::string version;
};

class UiSidebar
{
public:
	void Update(float deltaTime);
	float Draw(
		UiTab& activeTab,
		ThemeManager& theme,
		FontManager& fonts,
		float height,
		const UiSidebarVersionInfo& antiZapretVersion = {},
		const UiSidebarVersionInfo& tgWsProxyVersion = {});
	float Width() const { return m_width; }

private:
	struct NavItem
	{
		enum class IconSet : uint8_t
		{
			Mdl2 = 0,
			Solid,
			Brands,
		};

		uint32_t iconCode = 0;
		IconSet iconSet = IconSet::Mdl2;
		const char* label = nullptr;
		UiTab tab = UiTab::Home;
	};

	void SelectPage(UiTab tab, UiTab& activeTab);
	float CollapseMix() const;
	float PageHighlightAlpha(UiTab page, UiTab activeTab) const;
	void DrawNavButton(
		const NavItem& item,
		ImVec2 pos,
		float btnWidth,
		float btnHeight,
		float collapseMix,
		UiTab activeTab,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		FontManager& fonts,
		const UiSidebarVersionInfo* versionInfo);
	void DrawGlyphIcon(
		ImDrawList* drawList,
		ImFont* iconFont,
		float x,
		float y,
		uint32_t codepoint,
		ImU32 color,
		float size) const;
	static ImU32 WithAlpha(ImU32 color, float alpha);
	static ImU32 ToU32(const ImVec4& color, float alpha = 1.f);
	static ImU32 StatusColor(ComponentUpdateStatus status, const UiThemeColors& colors, const UiAccentColors& accents);

	static constexpr float kExpandedWidth = 160.f;
	static constexpr float kCollapsedWidth = 52.f;
	static constexpr float kAnimSec = 0.22f;
	static constexpr float kPageAnimSec = 0.28f;
	static constexpr float kRightStripWidth = 10.f;
	static constexpr float kRightStripRadius = 12.f;
	static constexpr float kRightStripOffsetX = -3.5f;

	bool m_collapsed = false;
	float m_width = kExpandedWidth;
	float m_from = kExpandedWidth;
	float m_to = kExpandedWidth;
	float m_elapsed = 0.f;
	bool m_animActive = false;

	UiTab m_page = UiTab::Home;
	UiTab m_pagePrev = UiTab::Home;
	float m_pageElapsed = 0.f;
	bool m_pageAnimActive = false;
};
