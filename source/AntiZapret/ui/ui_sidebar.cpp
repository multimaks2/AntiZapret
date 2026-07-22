#include "ui/ui_sidebar.h"

#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"

#include "imgui_internal.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <string>

namespace
{
	constexpr float kPad = 8.f;
	constexpr float kTop = 12.f;
	constexpr float kBtnHeight = 36.f;
	constexpr float kBottom = 8.f;
	constexpr float kNavStep = 40.f;
	constexpr float kBtnRadius = 6.f;
	constexpr float kIconSize = 14.f;
	constexpr uint32_t kIconChevronLeft = 0xE76B;
	constexpr uint32_t kIconChevronRight = 0xE76C;
	constexpr float kVersionFontScale = 0.78f;
	constexpr float kStatusDotRadius = 3.f;

	float Clamp(float value, float minValue, float maxValue)
	{
		if (value < minValue)
			return minValue;
		if (value > maxValue)
			return maxValue;
		return value;
	}

	float Lerp(float a, float b, float t)
	{
		return a + (b - a) * t;
	}

	float EaseOutCubic(float t)
	{
		const float u = 1.f - t;
		return 1.f - u * u * u;
	}

	std::string CodepointUtf8(uint32_t codepoint)
	{
		wchar_t wide[] = { static_cast<wchar_t>(codepoint), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
		if (len <= 0)
			return {};
		return std::string(utf8, static_cast<size_t>(len));
	}
}

void UiSidebar::Update(float deltaTime)
{
	if (m_animActive)
	{
		m_elapsed += deltaTime;
		const float t = m_elapsed / kAnimSec;
		if (t >= 1.f)
		{
			m_width = m_to;
			m_animActive = false;
		}
		else
		{
			m_width = Lerp(m_from, m_to, EaseOutCubic(t));
		}
	}

	if (m_pageAnimActive)
	{
		m_pageElapsed += deltaTime;
		if (m_pageElapsed >= kPageAnimSec)
		{
			m_pagePrev = m_page;
			m_pageAnimActive = false;
		}
	}
}

float UiSidebar::Draw(
	UiTab& activeTab,
	ThemeManager& theme,
	FontManager& fonts,
	float height,
	const UiSidebarVersionInfo& antiZapretVersion,
	const UiSidebarVersionInfo& tgWsProxyVersion)
{
	m_page = activeTab;
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();
	ImFont* iconFont = fonts.GetIconFont();

	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.sidebarBg);
	ImGui::BeginChild("##Sidebar", { m_width, height }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

	const ImVec2 origin = ImGui::GetWindowPos();
	const float stripRightInset = m_collapsed
		? kRightStripWidth * 0.25f
		: (kRightStripOffsetX < 0.f
			? (kRightStripWidth < -kRightStripOffsetX ? kRightStripWidth : -kRightStripOffsetX)
			: 0.f);
	const float btnWidth = m_width - kPad * 2.f - stripRightInset;
	const float collapseMix = CollapseMix();

	using IconSet = NavItem::IconSet;
	static const NavItem kNavItems[] = {
		{ 0xf015, IconSet::Solid, "Главная", UiTab::Home },
		{ 0xf3ed, IconSet::Solid, "Антизапрет", UiTab::AntiZapret },
		{ 0xf2c6, IconSet::Brands, "TG WS Proxy", UiTab::TgWsProxy },
		{ 0xf0ac, IconSet::Solid, "VPN", UiTab::Vpn },
		{ 0xf4d7, IconSet::Solid, "Маршрутизация", UiTab::Routing },
		{ 0xf120, IconSet::Solid, "Консоль", UiTab::Console },
		{ 0xf013, IconSet::Solid, "Настройки", UiTab::Settings },
		{ 0xf05a, IconSet::Solid, "О приложении", UiTab::About },
	};

	for (int i = 0; i < static_cast<int>(std::size(kNavItems)); ++i)
	{
		const ImVec2 pos = { origin.x + kPad, origin.y + kTop + i * kNavStep };
		const UiSidebarVersionInfo* versionInfo = nullptr;
		if (kNavItems[i].tab == UiTab::AntiZapret)
			versionInfo = &antiZapretVersion;
		else if (kNavItems[i].tab == UiTab::TgWsProxy)
			versionInfo = &tgWsProxyVersion;

		DrawNavButton(kNavItems[i], pos, btnWidth, kBtnHeight, collapseMix, activeTab, colors, accents, fonts, versionInfo);
	}

	const float toggleY = origin.y + height - kBtnHeight - kBottom;
	const ImVec2 togglePos = { origin.x + kPad, toggleY };
	const ImVec2 toggleSize = { 36.f, 32.f };
	ImGui::SetCursorScreenPos(togglePos);
	if (ImGui::InvisibleButton("##sidebar_toggle", toggleSize))
	{
		m_collapsed = !m_collapsed;
		m_from = m_width;
		m_to = m_collapsed ? kCollapsedWidth : kExpandedWidth;
		m_elapsed = 0.f;
		m_animActive = true;
	}

	const bool toggleHovered = ImGui::IsItemHovered();
	if (toggleHovered)
	{
		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, ToU32(colors.navHover), kBtnRadius);
	}

	const uint32_t toggleIcon = m_collapsed ? kIconChevronRight : kIconChevronLeft;
	DrawGlyphIcon(
		ImGui::GetWindowDrawList(),
		iconFont,
		togglePos.x + 10.f,
		togglePos.y + 10.f,
		toggleIcon,
		ToU32(toggleHovered ? colors.textPrimary : colors.textMuted),
		kIconSize);

	const ImVec2 stripAnchor = { origin.x + m_width, origin.y };
	const ImVec2 sidebarMin = origin;

	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::SameLine(0.f, 0.f);
	const float stripLayoutW = kRightStripWidth + kRightStripOffsetX > 0.f
		? kRightStripWidth + kRightStripOffsetX
		: 0.f;
	if (stripLayoutW > 0.f)
		ImGui::Dummy({ stripLayoutW, height });

	const ImVec2 stripMin = { stripAnchor.x + kRightStripOffsetX, stripAnchor.y };
	const ImVec2 stripMax = { stripMin.x + kRightStripWidth, stripAnchor.y + height };
	// Foreground so the strip sits above MainArea; when a *modal* is open, also paint
	// ModalWindowDimBg on top so the strip matches the dimmed workspace (otherwise it
	// stays bright and "cuts" through the overlay). Context menus / combo popups must
	// not dim — they are not modals.
	ImDrawList* overlayDrawList = ImGui::GetForegroundDrawList();
	overlayDrawList->PushClipRect(sidebarMin, stripMax, true);
	overlayDrawList->AddRectFilled(
		stripMin,
		stripMax,
		ToU32(colors.bg),
		kRightStripRadius,
		ImDrawFlags_RoundCornersLeft);
	if (ImGui::GetTopMostPopupModal() != nullptr)
	{
		overlayDrawList->AddRectFilled(
			stripMin,
			stripMax,
			ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg),
			kRightStripRadius,
			ImDrawFlags_RoundCornersLeft);
	}
	overlayDrawList->PopClipRect();

	activeTab = m_page;
	return m_width + stripLayoutW;
}

void UiSidebar::SelectPage(UiTab tab, UiTab& activeTab)
{
	if (tab == m_page)
		return;

	m_pagePrev = m_page;
	m_page = tab;
	activeTab = tab;
	m_pageElapsed = 0.f;
	m_pageAnimActive = true;
}

float UiSidebar::CollapseMix() const
{
	const float range = kExpandedWidth - kCollapsedWidth;
	if (range <= 0.f)
		return m_collapsed ? 1.f : 0.f;
	return Clamp((kExpandedWidth - m_width) / range, 0.f, 1.f);
}

float UiSidebar::PageHighlightAlpha(UiTab page, UiTab activeTab) const
{
	(void)activeTab;
	if (!m_pageAnimActive)
		return m_page == page ? 1.f : 0.f;

	const float t = EaseOutCubic(Clamp(m_pageElapsed / kPageAnimSec, 0.f, 1.f));
	if (page == m_page)
		return t;
	if (page == m_pagePrev)
		return 1.f - t;
	return 0.f;
}

void UiSidebar::DrawNavButton(
	const NavItem& item,
	ImVec2 pos,
	float btnWidth,
	float btnHeight,
	float collapseMix,
	UiTab activeTab,
	const UiThemeColors& colors,
	const UiAccentColors& accents,
	FontManager& fonts,
	const UiSidebarVersionInfo* versionInfo)
{
	ImGui::SetCursorScreenPos(pos);
	const std::string buttonId = std::string("##nav_") + item.label;
	if (ImGui::InvisibleButton(buttonId.c_str(), { btnWidth, btnHeight }))
		SelectPage(item.tab, activeTab);

	const bool hovered = ImGui::IsItemHovered();
	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	const float highlight = PageHighlightAlpha(item.tab, activeTab);
	if (highlight > 0.001f)
	{
		drawList->AddRectFilled(
			rectMin,
			rectMax,
			WithAlpha(ToU32(colors.navActive), highlight),
			kBtnRadius);
	}
	else if (hovered)
	{
		drawList->AddRectFilled(rectMin, rectMax, ToU32(colors.navHover), kBtnRadius);
	}

	const bool active = m_page == item.tab;
	const ImVec4 textColor = active ? colors.textPrimary : colors.textMuted;
	ImFont* glyphFont = fonts.GetIconFont();
	switch (item.iconSet)
	{
	case NavItem::IconSet::Solid:
		glyphFont = fonts.GetSolidFont();
		break;
	case NavItem::IconSet::Brands:
		glyphFont = fonts.GetBrandFont();
		break;
	case NavItem::IconSet::Mdl2:
	default:
		break;
	}
	DrawGlyphIcon(drawList, glyphFont, rectMin.x + 10.f, rectMin.y + 11.f, item.iconCode, ToU32(textColor), kIconSize);

	const float labelAlpha = 1.f - collapseMix;
	if (labelAlpha > 0.01f)
	{
		const ImVec2 textPos = { rectMin.x + 32.f, rectMin.y + (btnHeight - ImGui::GetTextLineHeight()) * 0.5f };
		drawList->AddText(
			ImGui::GetFont(),
			ImGui::GetFontSize(),
			textPos,
			WithAlpha(ToU32(textColor), labelAlpha),
			item.label);
	}

	if (versionInfo)
	{
		const ImU32 statusColor = StatusColor(versionInfo->status, colors, accents);

		// Collapsed sidebar: status lamp only (top-left).
		if (collapseMix > 0.01f)
		{
			const ImVec2 lampCenter = {
				rectMin.x + 5.f,
				rectMin.y + 4.f + kStatusDotRadius
			};
			drawList->AddCircleFilled(lampCenter, kStatusDotRadius, WithAlpha(statusColor, collapseMix));
		}

		// Expanded sidebar: version text only (top-right). Hide when this tab is open — page title shows it.
		if (labelAlpha > 0.01f && !versionInfo->version.empty() && item.tab != activeTab)
		{
			ImFont* font = ImGui::GetFont();
			const float versionFontSize = ImGui::GetFontSize() * kVersionFontScale;
			const ImVec2 textSize = font->CalcTextSizeA(versionFontSize, FLT_MAX, 0.f, versionInfo->version.c_str());
			const ImVec2 versionPos = {
				rectMax.x - 8.f - textSize.x,
				rectMin.y + 3.f
			};
			drawList->AddText(
				font,
				versionFontSize,
				versionPos,
				WithAlpha(statusColor, labelAlpha),
				versionInfo->version.c_str());
		}
	}
}

void UiSidebar::DrawGlyphIcon(
	ImDrawList* drawList,
	ImFont* iconFont,
	float x,
	float y,
	uint32_t codepoint,
	ImU32 color,
	float size) const
{
	if (!drawList)
		return;

	const std::string glyph = CodepointUtf8(codepoint);
	if (glyph.empty())
		return;

	ImFont* font = iconFont ? iconFont : ImGui::GetFont();
	const float fontSize = iconFont ? size : ImGui::GetFontSize();
	drawList->AddText(font, fontSize, { x, y }, color, glyph.c_str());
}

ImU32 UiSidebar::WithAlpha(ImU32 color, float alpha)
{
	const ImU32 a = static_cast<ImU32>(Clamp(alpha, 0.f, 1.f) * 255.f);
	return (color & 0x00FFFFFFu) | (a << 24);
}

ImU32 UiSidebar::ToU32(const ImVec4& color, float alpha)
{
	return ImGui::GetColorU32({ color.x, color.y, color.z, color.w * alpha });
}

ImU32 UiSidebar::StatusColor(ComponentUpdateStatus status, const UiThemeColors& colors, const UiAccentColors& accents)
{
	(void)colors;
	(void)accents;
	return ToU32(UiCommon::FixedVersionStatusAccent(status));
}
