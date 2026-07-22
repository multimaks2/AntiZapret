#include "ui/ui_common.h"

#include "gfx/font_manager.h"

#include <Windows.h>
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	ImVec4 Blend(const ImVec4& a, const ImVec4& b, float t)
	{
		return {
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t,
		};
	}
}

ImVec4 UiCommon::StatusColor(const UiAccentColors& accents, bool active, bool ok)
{
	if (!active)
		return WithAlpha(accents.warn, 0.f); // use muted via caller
	if (ok)
		return accents.ok;
	return accents.fail;
}

ImVec4 UiCommon::FixedVersionStatusAccent(ComponentUpdateStatus status)
{
	// Same palette as sidebar status lamps — does not follow theme accents.
	switch (status)
	{
	case ComponentUpdateStatus::UpToDate:
		return FixedStartAccent();
	case ComponentUpdateStatus::Checking:
	case ComponentUpdateStatus::UpdateAvailable:
		return { 230 / 255.f, 170 / 255.f, 50 / 255.f, 1.f };
	case ComponentUpdateStatus::Unknown:
	case ComponentUpdateStatus::Error:
		return FixedStopAccent();
	default:
		return WithAlpha({ 140 / 255.f, 140 / 255.f, 140 / 255.f, 1.f }, 0.75f);
	}
}

ImVec4 UiCommon::FixedStartAccent()
{
	// Classic app green (Dark/Light download/ok) — never follows theme tint.
	return { 33 / 255.f, 176 / 255.f, 77 / 255.f, 1.f };
}

ImVec4 UiCommon::FixedStopAccent()
{
	return { 196 / 255.f, 72 / 255.f, 72 / 255.f, 1.f };
}

ImVec4 UiCommon::FixedDownloadAccent()
{
	return FixedStartAccent();
}

ImVec4 UiCommon::FixedUploadAccent()
{
	// Classic Dark/Light upload blue — readable on tinted theme backgrounds.
	return { 80 / 255.f, 160 / 255.f, 255 / 255.f, 1.f };
}

namespace
{
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

void UiCommon::VersionBadge(const char* version, const ImVec4& accent, const UiThemeColors& colors)
{
	VersionBadge(version, accent, colors, true);
}

void UiCommon::VersionBadge(const char* version, const ImVec4& accent, const UiThemeColors& colors, bool centerInLine)
{
	if (!version || !version[0])
		return;

	ImFont* font = ImGui::GetFont();
	const float versionFontSize = ImGui::GetFontSize() * 0.82f;
	const ImVec2 textSize = font->CalcTextSizeA(versionFontSize, FLT_MAX, 0.f, version);
	const float padX = 5.f;
	const float padY = 1.f;
	const float badgeH = textSize.y + padY * 2.f;
	const float badgeW = textSize.x + padX * 2.f;
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	const float lineH = ImGui::GetTextLineHeight();
	const float badgeY = centerInLine && lineH > badgeH
		? cursor.y + (lineH - badgeH) * 0.5f
		: cursor.y;
	const ImVec2 min = { cursor.x, badgeY };
	const ImVec2 max = { min.x + badgeW, min.y + badgeH };

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	(void)colors;
	drawList->AddRectFilled(
		min,
		max,
		ImGui::ColorConvertFloat4ToU32(WithAlpha(accent, 0.11f)),
		3.f);
	drawList->AddRect(
		min,
		max,
		ImGui::ColorConvertFloat4ToU32(accent),
		3.f,
		0,
		1.25f);
	drawList->AddText(
		font,
		versionFontSize,
		{ min.x + padX, min.y + padY },
		ImGui::ColorConvertFloat4ToU32(accent),
		version);
	ImGui::Dummy({ badgeW, centerInLine ? lineH : badgeH });
}

void UiCommon::PageTitle(
	FontManager& fonts,
	uint32_t iconCode,
	const char* title,
	const char* subtitle,
	const UiThemeColors& colors)
{
	PageTitle(fonts, iconCode, title, subtitle, colors, nullptr, {}, nullptr, false);
}

void UiCommon::PageTitle(
	FontManager& fonts,
	uint32_t iconCode,
	const char* title,
	const char* subtitle,
	const UiThemeColors& colors,
	const char* version,
	const ImVec4& versionAccent)
{
	PageTitle(fonts, iconCode, title, subtitle, colors, version, versionAccent, nullptr, false);
}

bool UiCommon::PageTitle(
	FontManager& fonts,
	uint32_t iconCode,
	const char* title,
	const char* subtitle,
	const UiThemeColors& colors,
	const char* version,
	const ImVec4& versionAccent,
	const char* updateButtonLabel,
	bool updateButtonEnabled)
{
	const std::string glyph = CodepointUtf8(iconCode);
	ImFont* iconFont = fonts.GetIconFont();
	if (iconFont)
		ImGui::PushFont(iconFont);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	if (!glyph.empty())
		ImGui::TextUnformatted(glyph.c_str());
	if (iconFont)
		ImGui::PopFont();
	ImGui::SameLine(0.f, 8.f);
	ImGui::TextUnformatted(title);
	ImGui::PopStyleColor();

	if (version && version[0])
	{
		ImGui::SameLine(0.f, 10.f);
		VersionBadge(version, versionAccent, colors);
	}

	bool updateClicked = false;
	if (updateButtonLabel && updateButtonLabel[0])
	{
		ImGui::SameLine(0.f, 12.f);
		const float btnH = UiMetrics::kSmallBtnHeight;
		const float btnW = ImGui::CalcTextSize(updateButtonLabel).x + 24.f;
		const float lineH = ImGui::GetTextLineHeight();
		if (btnH < lineH)
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (lineH - btnH) * 0.5f);
		updateClicked = SecondaryButton(updateButtonLabel, { btnW, btnH }, colors, updateButtonEnabled);
	}

	if (subtitle && subtitle[0])
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted(subtitle);
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	}
	else if (!(version && version[0]) && !(updateButtonLabel && updateButtonLabel[0]))
	{
		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	}

	return updateClicked;
}

void UiCommon::SectionHeader(const char* title, const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::TextUnformatted(title);
	ImGui::PopStyleColor();
}

void UiCommon::CaptionText(const char* text, const UiThemeColors& colors, float wrapWidth)
{
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	if (wrapWidth > 0.f)
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
	ImGui::TextUnformatted(text);
	if (wrapWidth > 0.f)
		ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

void UiCommon::CardGap()
{
	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
}

bool UiCommon::BeginCard(const char* id, float width, const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { UiMetrics::kCardPad, UiMetrics::kCardPad });
	return ImGui::BeginChild(id, { width, 0.f }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
}

void UiCommon::EndCard()
{
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
}

bool UiCommon::AccentButton(
	const char* label,
	ImVec2 size,
	const ImVec4& accent,
	const UiThemeColors& colors,
	bool enabled)
{
	(void)colors;
	if (!enabled)
		ImGui::BeginDisabled();
	ImGui::PushStyleColor(ImGuiCol_Button, accent);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Blend(accent, { 1.f, 1.f, 1.f, 1.f }, 0.12f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Blend(accent, { 0.f, 0.f, 0.f, 1.f }, 0.12f));
	ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 1.f, 1.f, 1.f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
	if (size.y > 0.f)
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, (size.y - ImGui::GetFontSize()) * 0.5f });
	const bool pressed = ImGui::Button(label, size);
	if (size.y > 0.f)
		ImGui::PopStyleVar();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(4);
	if (!enabled)
		ImGui::EndDisabled();
	return pressed && enabled;
}

bool UiCommon::SecondaryButton(const char* label, ImVec2 size, const UiThemeColors& colors, bool enabled)
{
	if (!enabled)
		ImGui::BeginDisabled();

	ImVec4 btn;
	ImVec4 btnHover;
	ImVec4 btnActive;
	float borderAlpha = 0.f;
	if (colors.classicControls)
	{
		btn = WithAlpha(colors.sidebarBg, 0.95f);
		btnHover = colors.navHover;
		btnActive = colors.navActive;
	}
	else
	{
		const bool light = IsLightTheme(colors);
		btn = light ? WithAlpha(colors.sidebarBg, 0.95f) : Blend(colors.tileBg, colors.textPrimary, 0.14f);
		btnHover = light ? colors.navHover : Blend(colors.tileBg, colors.textPrimary, 0.22f);
		btnActive = light ? colors.navActive : Blend(colors.tileBg, colors.textPrimary, 0.28f);
		borderAlpha = light ? 0.55f : 0.70f;
	}

	ImGui::PushStyleColor(ImGuiCol_Button, btn);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	if (borderAlpha > 0.f)
		ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(colors.tileBorder, borderAlpha));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
	if (borderAlpha > 0.f)
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
	else
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
	if (size.y > 0.f)
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, (size.y - ImGui::GetFontSize()) * 0.5f });
	const bool pressed = ImGui::Button(label, size);
	if (size.y > 0.f)
		ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(borderAlpha > 0.f ? 5 : 4);
	if (!enabled)
		ImGui::EndDisabled();
	return pressed && enabled;
}

void UiCommon::StatusBadge(const char* label, ImVec4 color, const UiThemeColors& colors)
{
	(void)colors;
	const ImVec2 textSize = ImGui::CalcTextSize(label);
	const ImVec2 pad = { 10.f, 4.f };
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const ImVec2 size = { textSize.x + pad.x * 2.f, textSize.y + pad.y * 2.f };
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, ImGui::GetColorU32(WithAlpha(color, 0.18f)), 999.f);
	drawList->AddRect(pos, { pos.x + size.x, pos.y + size.y }, ImGui::GetColorU32(WithAlpha(color, 0.55f)), 999.f);
	drawList->AddText({ pos.x + pad.x, pos.y + pad.y }, ImGui::GetColorU32(color), label);
	ImGui::Dummy(size);
}

float UiCommon::DrawServiceInline(
	const char* name,
	bool online,
	const ImVec4& brandColor,
	const UiThemeColors& colors,
	const UiAccentColors& accents,
	const char* statusOverride)
{
	const char* statusText = statusOverride
		? statusOverride
		: (online ? "Работает" : "Недоступен");
	// Fixed status colors — never follow theme accents (e.g. red theme making "Работает" red).
	constexpr ImVec4 kOk = { 33 / 255.f, 176 / 255.f, 77 / 255.f, 1.f };
	constexpr ImVec4 kMuted = { 140 / 255.f, 140 / 255.f, 140 / 255.f, 1.f };
	const ImVec4 statusColor = online ? kOk : kMuted;
	(void)accents;
	const float startX = ImGui::GetCursorPosX();

	ImGui::PushStyleColor(ImGuiCol_Text, brandColor);
	ImGui::TextUnformatted(name);
	ImGui::PopStyleColor();
	ImGui::SameLine(0.f, 0.f);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::TextUnformatted(": ");
	ImGui::PopStyleColor();
	ImGui::SameLine(0.f, 0.f);
	ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
	ImGui::TextUnformatted(statusText);
	ImGui::PopStyleColor();

	return ImGui::GetCursorPosX() - startX;
}

void UiCommon::InfoLine(const char* label, const char* value, const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();
	ImGui::SameLine(0.f, 4.f);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::TextUnformatted(value);
	ImGui::PopStyleColor();
}

ImVec4 UiCommon::WithAlpha(const ImVec4& color, float alpha)
{
	return { color.x, color.y, color.z, alpha };
}

bool UiCommon::IsLightTheme(const UiThemeColors& colors)
{
	return colors.bg.x > 0.5f;
}

void UiCommon::SyncImGuiStyle(const UiThemeColors& colors)
{
	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_Text] = colors.textPrimary;
	style.Colors[ImGuiCol_TextDisabled] = colors.textMuted;
	if (IsLightTheme(colors))
	{
		style.Colors[ImGuiCol_PopupBg] = { 0.86f, 0.86f, 0.88f, 0.98f };
		style.Colors[ImGuiCol_Border] = WithAlpha(colors.tileBorder, 0.55f);
		style.Colors[ImGuiCol_SliderGrab] = { 0.38f, 0.38f, 0.41f, 1.f };
		style.Colors[ImGuiCol_SliderGrabActive] = { 0.28f, 0.28f, 0.30f, 1.f };
		style.Colors[ImGuiCol_FrameBg] = { 0.74f, 0.74f, 0.77f, 0.98f };
		style.Colors[ImGuiCol_FrameBgHovered] = { 0.68f, 0.68f, 0.71f, 0.98f };
		style.Colors[ImGuiCol_FrameBgActive] = { 0.62f, 0.62f, 0.65f, 0.98f };
		style.Colors[ImGuiCol_ChildBg] = colors.tileBg;
	}
	else if (colors.classicControls)
	{
		style.Colors[ImGuiCol_PopupBg] = WithAlpha(colors.tileBg, 0.98f);
		style.Colors[ImGuiCol_Border] = WithAlpha(colors.tileBorder, 0.35f);
		style.Colors[ImGuiCol_SliderGrab] = { 0.62f, 0.62f, 0.65f, 1.f };
		style.Colors[ImGuiCol_SliderGrabActive] = colors.textPrimary;
		style.Colors[ImGuiCol_FrameBg] = colors.inputBg;
		style.Colors[ImGuiCol_FrameBgHovered] = WithAlpha(colors.navHover, 0.95f);
		style.Colors[ImGuiCol_FrameBgActive] = WithAlpha(colors.navActive, 0.95f);
		style.Colors[ImGuiCol_ChildBg] = colors.tileBg;
	}
	else
	{
		const ImVec4 raised = Blend(colors.tileBg, colors.textPrimary, 0.12f);
		const ImVec4 raisedMore = Blend(colors.tileBg, colors.textPrimary, 0.18f);
		style.Colors[ImGuiCol_PopupBg] = WithAlpha(raisedMore, 0.98f);
		style.Colors[ImGuiCol_Border] = WithAlpha(colors.tileBorder, 0.65f);
		style.Colors[ImGuiCol_SliderGrab] = { 0.62f, 0.62f, 0.65f, 1.f };
		style.Colors[ImGuiCol_SliderGrabActive] = colors.textPrimary;
		style.Colors[ImGuiCol_FrameBg] = WithAlpha(raised, 0.98f);
		style.Colors[ImGuiCol_FrameBgHovered] = WithAlpha(raisedMore, 0.98f);
		style.Colors[ImGuiCol_FrameBgActive] = WithAlpha(Blend(colors.tileBg, colors.textPrimary, 0.24f), 0.98f);
		style.Colors[ImGuiCol_ChildBg] = colors.tileBg;
	}
	style.GrabRounding = 8.f;
	style.PopupRounding = UiMetrics::kCardRadius;
	style.FrameBorderSize = colors.classicControls ? 0.f : 1.f;
	ConfigureTooltips(style);
}

void UiCommon::ConfigureTooltips(ImGuiStyle& style)
{
	style.HoverDelayNormal = 2.5f;
	style.HoverFlagsForTooltipMouse =
		ImGuiHoveredFlags_Stationary
		| ImGuiHoveredFlags_DelayNormal
		| ImGuiHoveredFlags_AllowWhenDisabled;
	style.HoverFlagsForTooltipNav =
		ImGuiHoveredFlags_NoSharedDelay
		| ImGuiHoveredFlags_DelayNormal;
	style.PopupRounding = UiMetrics::kCardRadius;
}

void UiCommon::ShowTooltip(const char* fmt, ...)
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
	va_list args;
	va_start(args, fmt);
	ImGui::SetTooltipV(fmt, args);
	va_end(args);
	ImGui::PopStyleVar(2);
}

void UiCommon::SetItemTooltip(const char* fmt, ...)
{
	if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		return;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
	va_list args;
	va_start(args, fmt);
	ImGui::SetTooltipV(fmt, args);
	va_end(args);
	ImGui::PopStyleVar(2);
}

float UiCommon::ExpSmooth(float current, float target, float deltaTime, float speed)
{
	const float k = 1.f - expf(-deltaTime * speed);
	return current + (target - current) * k;
}

float UiCommon::AnimateMix(float current, bool expanded, float deltaTime, float speed)
{
	const float target = expanded ? 1.f : 0.f;
	float mix = ExpSmooth(current, target, deltaTime, speed);
	if (mix < 0.002f)
		return 0.f;
	if (mix > 0.998f)
		return 1.f;
	return mix;
}

void UiCommon::PushInputStyle(const UiThemeColors& colors)
{
	const bool light = IsLightTheme(colors);
	ImVec4 controlBg;
	ImVec4 controlHover;
	ImVec4 controlActive;
	ImVec4 popupBg;
	ImVec4 popupHeader;
	ImVec4 popupHeaderHover;
	ImVec4 popupHeaderActive;
	float borderAlpha;

	if (light)
	{
		controlBg = { 0.74f, 0.74f, 0.77f, 0.98f };
		controlHover = { 0.68f, 0.68f, 0.71f, 0.98f };
		controlActive = { 0.62f, 0.62f, 0.65f, 0.98f };
		popupBg = { 0.84f, 0.84f, 0.86f, 0.98f };
		popupHeader = { 0.78f, 0.78f, 0.81f, 0.98f };
		popupHeaderHover = { 0.72f, 0.72f, 0.75f, 0.98f };
		popupHeaderActive = { 0.66f, 0.66f, 0.69f, 0.98f };
		borderAlpha = 0.55f;
	}
	else if (colors.classicControls)
	{
		controlBg = colors.inputBg;
		controlHover = WithAlpha(colors.navHover, 0.95f);
		controlActive = WithAlpha(colors.navActive, 0.95f);
		popupBg = WithAlpha(colors.tileBg, 0.98f);
		popupHeader = WithAlpha(colors.navHover, 0.55f);
		popupHeaderHover = WithAlpha(colors.navHover, 0.85f);
		popupHeaderActive = WithAlpha(colors.navActive, 0.95f);
		borderAlpha = 0.35f;
	}
	else
	{
		const ImVec4 raised = Blend(colors.tileBg, colors.textPrimary, 0.14f);
		const ImVec4 raisedHover = Blend(colors.tileBg, colors.textPrimary, 0.22f);
		const ImVec4 raisedActive = Blend(colors.tileBg, colors.textPrimary, 0.28f);
		controlBg = WithAlpha(raised, 0.98f);
		controlHover = WithAlpha(raisedHover, 0.98f);
		controlActive = WithAlpha(raisedActive, 0.98f);
		popupBg = WithAlpha(Blend(colors.tileBg, colors.textPrimary, 0.18f), 0.98f);
		popupHeader = WithAlpha(raisedHover, 0.75f);
		popupHeaderHover = WithAlpha(raisedActive, 0.95f);
		popupHeaderActive = WithAlpha(raisedActive, 1.f);
		borderAlpha = 0.70f;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, controlBg);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, controlHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, controlActive);
	ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(colors.tileBorder, borderAlpha));
	ImGui::PushStyleColor(ImGuiCol_Button, controlBg);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, controlHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, controlActive);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
	ImGui::PushStyleColor(ImGuiCol_Header, popupHeader);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, popupHeaderHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, popupHeaderActive);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, 7.f });
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, colors.classicControls ? 0.f : 1.f);
}

void UiCommon::PopInputStyle()
{
	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(12);
}

void UiCommon::PushSliderStyle(const UiThemeColors& colors)
{
	const bool light = IsLightTheme(colors);
	ImVec4 trackBg;
	ImVec4 trackHover;
	if (light)
	{
		trackBg = { 0.80f, 0.80f, 0.83f, 1.f };
		trackHover = { 0.74f, 0.74f, 0.77f, 1.f };
	}
	else if (colors.classicControls)
	{
		trackBg = { colors.inputBg.x, colors.inputBg.y, colors.inputBg.z, 1.f };
		trackHover = { colors.navHover.x, colors.navHover.y, colors.navHover.z, 1.f };
	}
	else
	{
		trackBg = Blend(colors.tileBg, colors.textPrimary, 0.14f);
		trackHover = Blend(colors.tileBg, colors.textPrimary, 0.22f);
	}
	const ImVec4 grab = light ? ImVec4(0.36f, 0.36f, 0.39f, 1.f) : ImVec4(0.55f, 0.55f, 0.58f, 1.f);
	const ImVec4 grabActive = light ? ImVec4(0.24f, 0.24f, 0.26f, 1.f) : colors.textPrimary;

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, trackBg);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, trackHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, trackBg);
	ImGui::PushStyleColor(ImGuiCol_SliderGrab, grab);
	ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, grabActive);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.f);
}

void UiCommon::PopSliderStyle()
{
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(6);
}

bool UiCommon::InputText(const char* id, char* buffer, int bufferSize, const UiThemeColors& colors, ImVec2 size)
{
	PushInputStyle(colors);
	if (size.x > 0.f)
		ImGui::SetNextItemWidth(size.x);
	if (size.y > 0.f)
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, (size.y - ImGui::GetFontSize()) * 0.5f });
	const bool changed = ImGui::InputText(id, buffer, static_cast<size_t>(bufferSize));
	if (size.y > 0.f)
		ImGui::PopStyleVar();
	PopInputStyle();
	return changed;
}

bool UiCommon::StrategyButton(
	int widgetId,
	const char* label,
	bool selected,
	ImVec2 size,
	const UiThemeColors& colors,
	const ImVec4& accent)
{
	ImGui::PushID(widgetId);
	bool pressed = false;
	if (selected)
		pressed = AccentButton(label, size, accent, colors);
	else
		pressed = SecondaryButton(label, size, colors);
	ImGui::PopID();
	return pressed;
}

void UiCommon::DrawStrategyIndicators(
	ImVec2 buttonMin,
	const StrategyIndicatorState& state,
	const UiThemeColors& colors)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const float radius = 4.f;
	const float gap = 2.f;
	// Pixel-center snap keeps the disc stable between frames.
	float cx = std::floor(buttonMin.x + radius + 2.5f) + 0.5f;
	const float cy = std::floor(buttonMin.y - radius + 10.5f) + 0.5f;
	// Explicit segments — ArcFast auto-step undersamples tiny radii into an octagon.
	constexpr int kCircleSegs = 32;

	const ImU32 serviceColors[] = {
		IM_COL32(88, 101, 242, 255),
		IM_COL32(255, 0, 0, 255),
		IM_COL32(0, 136, 204, 255),
	};

	// Idle lamps must stay readable on tinted SecondaryButton fills (Matrix/Ocean/…).
	const bool light = IsLightTheme(colors);
	const ImU32 idleColor = light
		? ImGui::GetColorU32(WithAlpha(colors.textMuted, 0.85f))
		: (colors.classicControls
			? IM_COL32(110, 110, 115, 230)
			: ImGui::GetColorU32(Blend(colors.tileBg, colors.textPrimary, 0.58f)));
	const ImU32 failedColor = light
		? ImGui::GetColorU32(WithAlpha(colors.textMuted, 0.45f))
		: (colors.classicControls
			? IM_COL32(80, 80, 80, 200)
			: ImGui::GetColorU32(Blend(colors.tileBg, colors.textPrimary, 0.32f)));
	const ImU32 ringColor = light
		? ImGui::GetColorU32(WithAlpha(colors.textMuted, 0.55f))
		: ImGui::GetColorU32(WithAlpha(colors.textPrimary, colors.classicControls ? 0.25f : 0.40f));

	auto mulAlpha = [](ImU32 col, float a) -> ImU32
	{
		const int alpha = static_cast<int>(std::lround(static_cast<float>((col >> 24) & 0xFFu) * a));
		return (col & 0x00FFFFFFu) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << 24);
	};

	const bool serviceOk[] = { state.discordOk, state.youtubeOk, state.telegramOk };
	for (int service = 0; service < 3; ++service)
	{
		const bool lit = state.hasResult && serviceOk[service];
		const ImU32 color = lit
			? serviceColors[service]
			: (state.hasResult ? failedColor : idleColor);

		const ImVec2 c = { cx, cy };
		if (!lit)
			drawList->AddCircleFilled(c, radius + 0.85f, ringColor, kCircleSegs);
		// Soft halo hides residual polygon edges at ~8px diameter.
		drawList->AddCircleFilled(c, radius + 0.45f, mulAlpha(color, 0.40f), kCircleSegs);
		drawList->AddCircleFilled(c, radius, color, kCircleSegs);
		cx = std::floor(cx + radius * 2.f + gap) + 0.5f;
	}

	ImFont* font = ImGui::GetFont();
	const float belowY = cy + radius + 2.f;
	const float circlesCenterX = std::floor(buttonMin.x + radius + 2.5f) + 0.5f + radius * 2.f + gap;

	if (state.isBest)
	{
		const char* label = "Лучший*";
		const float labelFontSize = ImGui::GetFontSize() * 0.65f;
		const ImVec2 labelSize = font->CalcTextSizeA(labelFontSize, FLT_MAX, 0.f, label);
		const ImU32 labelColor = light
			? IM_COL32(120, 85, 10, 255)
			: IM_COL32(212, 175, 55, 255);
		drawList->AddText(
			font,
			labelFontSize,
			{ circlesCenterX - labelSize.x * 0.5f, belowY },
			labelColor,
			label);
	}
}

bool UiCommon::StyledCheckbox(const char* label, bool* value, const UiThemeColors& colors)
{
	const float boxSize = 18.f;
	const float labelGap = 8.f;
	const bool hasVisibleLabel = label && label[0] && !(label[0] == '#' && label[1] == '#');
	const float textW = hasVisibleLabel ? ImGui::CalcTextSize(label).x : 0.f;
	const float textH = ImGui::GetTextLineHeight();
	const float hitW = boxSize + (hasVisibleLabel ? labelGap + textW : 0.f);

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::PushID(label);
	ImGui::InvisibleButton("##styled_cb", { hitW, boxSize });
	const bool hovered = ImGui::IsItemHovered();
	bool changed = false;
	if (ImGui::IsItemClicked())
	{
		*value = !*value;
		changed = true;
	}
	ImGui::PopID();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const bool light = IsLightTheme(colors);
	ImVec4 boxBg;
	float borderAlpha;
	if (light)
	{
		boxBg = hovered ? WithAlpha(colors.navHover, 0.95f) : colors.inputBg;
		borderAlpha = 0.55f;
	}
	else if (colors.classicControls)
	{
		boxBg = hovered ? WithAlpha(colors.navHover, 0.95f) : colors.inputBg;
		borderAlpha = 0.35f;
	}
	else
	{
		boxBg = Blend(colors.tileBg, colors.textPrimary, hovered ? 0.22f : 0.14f);
		borderAlpha = 0.70f;
	}
	drawList->AddRectFilled(pos, { pos.x + boxSize, pos.y + boxSize }, ImGui::GetColorU32(boxBg), UiMetrics::kCardRadius);
	drawList->AddRect(
		pos,
		{ pos.x + boxSize, pos.y + boxSize },
		ImGui::GetColorU32(WithAlpha(colors.tileBorder, borderAlpha)),
		UiMetrics::kCardRadius);

	if (*value)
	{
		const float pad = 4.f;
		drawList->AddLine(
			{ pos.x + pad, pos.y + boxSize * 0.55f },
			{ pos.x + boxSize * 0.42f, pos.y + boxSize - pad },
			ImGui::GetColorU32(colors.textPrimary),
			2.f);
		drawList->AddLine(
			{ pos.x + boxSize * 0.42f, pos.y + boxSize - pad },
			{ pos.x + boxSize - pad, pos.y + pad + 1.f },
			ImGui::GetColorU32(colors.textPrimary),
			2.f);
	}

	if (hasVisibleLabel)
	{
		// textY = boxY + boxH/2 - textH/2
		const float textY = pos.y + (boxSize - textH) * 0.5f;
		drawList->AddText(
			ImVec2(pos.x + boxSize + labelGap, textY),
			ImGui::GetColorU32(colors.textPrimary),
			label);
	}
	return changed;
}

namespace
{
	ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t)
	{
		return {
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t,
		};
	}
}

bool UiCommon::ToggleSwitch(const char* id, float mix, const UiThemeColors& colors)
{
	const ImVec2 size = { 40.f, 22.f };
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	ImGui::PushID(id);
	ImGui::InvisibleButton("##toggle", size);
	const bool pressed = ImGui::IsItemClicked();
	const bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();

	const ImVec4 offBg = { 51.f / 255.f, 54.f / 255.f, 64.f / 255.f, hovered ? 1.f : 0.95f };
	const ImVec4 onBg = { 33.f / 255.f, 176.f / 255.f, 77.f / 255.f, 1.f };
	const ImVec4 bg = LerpColor(offBg, onBg, mix);
	drawList->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, ImGui::GetColorU32(bg), 11.f);

	const float knobSize = 18.f;
	const float knobX = pos.x + 2.f + (size.x - knobSize - 4.f) * mix;
	drawList->AddRectFilled(
		{ knobX, pos.y + 2.f },
		{ knobX + knobSize, pos.y + 2.f + knobSize },
		IM_COL32(255, 255, 255, 255),
		9.f);

	return pressed;
}

bool UiCommon::SettingRow(const char* label, float width, const UiThemeColors& colors, float mix)
{
	const float rowH = 44.f;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(pos, { pos.x + width, pos.y + rowH }, ImGui::GetColorU32(colors.tileBg), UiMetrics::kCardRadius);

	ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
	ImGui::SetCursorScreenPos({ pos.x + 14.f, pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f });
	ImGui::TextUnformatted(label);
	ImGui::PopStyleColor();

	ImGui::SetCursorScreenPos({ pos.x + width - 54.f, pos.y + 11.f });
	const bool toggled = ToggleSwitch(label, mix, colors);
	ImGui::SetCursorScreenPos({ pos.x, pos.y + rowH });
	ImGui::Dummy({ width, UiMetrics::kCardGap });
	return toggled;
}

void UiCommon::PushTableStyle(const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_Header, WithAlpha(colors.navActive, 0.42f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.navHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.navActive);
	ImGui::PushStyleColor(ImGuiCol_TableRowBg, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, IM_COL32(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_TableBorderLight, WithAlpha(colors.tileBorder, 0.28f));
	ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, WithAlpha(colors.tileBorder, 0.42f));
	ImGui::PushStyleColor(ImGuiCol_Separator, WithAlpha(colors.textPrimary, 0.35f));
	ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, colors.textPrimary);
	ImGui::PushStyleColor(ImGuiCol_SeparatorActive, colors.textPrimary);
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.f, 2.f));
}

void UiCommon::PopTableStyle()
{
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(11);
}

ImGuiTableFlags UiCommon::DefaultTableFlags(bool sortable)
{
	ImGuiTableFlags flags =
		ImGuiTableFlags_Borders |
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_Resizable |
		ImGuiTableFlags_ScrollX |
		ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_SizingFixedFit;
	if (sortable)
		flags |= ImGuiTableFlags_Sortable;
	return flags;
}

ImGuiTableFlags UiCommon::StretchableTableFlags(bool sortable)
{
	ImGuiTableFlags flags = DefaultTableFlags(sortable);
	flags &= ~ImGuiTableFlags_SizingFixedFit;
	flags &= ~ImGuiTableFlags_ScrollX;
	flags &= ~ImGuiTableFlags_ScrollY;
	flags |= ImGuiTableFlags_SizingStretchProp;
	return flags;
}

void UiCommon::TableHeadersRow(const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_Header, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TableHeadersRow();
	ImGui::PopStyleColor(4);
}

void UiCommon::TableHeadersRowCentered(const UiThemeColors& colors)
{
	ImGui::PushStyleColor(ImGuiCol_Header, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.inputBg);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);

	const int columnsCount = ImGui::TableGetColumnCount();
	ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
	for (int column = 0; column < columnsCount; ++column)
	{
		if (!ImGui::TableSetColumnIndex(column))
			continue;

		const ImGuiTableColumnFlags columnFlags = ImGui::TableGetColumnFlags(column);
		const char* label = (columnFlags & ImGuiTableColumnFlags_NoHeaderLabel) ? "" : ImGui::TableGetColumnName(column);
		if (!label)
			label = "";

		const ImVec2 labelSize = ImGui::CalcTextSize(label);
		const float colW = ImGui::GetColumnWidth();
		const float padX = ImGui::GetStyle().CellPadding.x;
		float reserveRight = 0.f;
		if (!(columnFlags & ImGuiTableColumnFlags_NoSort))
			reserveRight = ImGui::GetFontSize() * 0.65f + ImGui::GetStyle().FramePadding.x;

		const float availW = colW - padX * 2.f - reserveRight;
		if (label[0] != '\0' && availW > labelSize.x)
		{
			const float x = ImGui::GetCursorPosX() + padX + (availW - labelSize.x) * 0.5f;
			ImGui::SetCursorPosX(x);
		}

		ImGui::PushID(column);
		ImGui::TableHeader(label);
		ImGui::PopID();
	}

	ImGui::PopStyleColor(4);
}

void UiCommon::TableTextAligned(const char* text, UiTableAlign align)
{
	if (align == UiTableAlign::Center || align == UiTableAlign::Right)
	{
		const float colW = ImGui::GetColumnWidth();
		const ImVec2 size = ImGui::CalcTextSize(text);
		float x = ImGui::GetCursorPosX();
		if (align == UiTableAlign::Center)
			x += (colW - size.x) * 0.5f;
		else
			x += colW - size.x - ImGui::GetStyle().CellPadding.x;
		ImGui::SetCursorPosX(x);
	}
	ImGui::TextUnformatted(text);
}

float UiCommon::TableRowInputHeight(const UiThemeColors& colors)
{
	PushInputStyle(colors);
	const float height = ImGui::GetFrameHeight();
	PopInputStyle();
	return height;
}

float UiCommon::TableRowMinHeight(float contentHeight)
{
	return contentHeight + ImGui::GetStyle().CellPadding.y * 2.f;
}

void UiCommon::TableAlignTextY(float contentHeight)
{
	const float y = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(y + (contentHeight - ImGui::GetTextLineHeight()) * 0.5f);
}

void UiCommon::TableAlignFrameY(float contentHeight)
{
	const float frameHeight = ImGui::GetFrameHeight();
	const float y = ImGui::GetCursorPosY();
	ImGui::SetCursorPosY(y + (contentHeight - frameHeight) * 0.5f);
	ImGui::AlignTextToFramePadding();
}

bool UiCommon::TableRowSelectable(const char* label, bool selected, float contentHeight)
{
	const float highlightH = contentHeight + ImGui::GetStyle().CellPadding.y * 2.f;
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
	const bool pressed = ImGui::Selectable(
		label,
		selected,
		ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
		ImVec2(0.f, highlightH));
	ImGui::PopStyleVar(2);
	return pressed;
}

bool UiCommon::IconToolButton(
	FontManager& fonts,
	uint32_t iconCode,
	const char* id,
	const char* tooltip,
	const UiThemeColors& colors,
	ImVec2 size,
	bool enabled)
{
	if (!enabled)
		ImGui::BeginDisabled();

	ImGui::PushID(id);
	ImVec4 btn;
	ImVec4 btnHover;
	ImVec4 btnActive;
	float borderAlpha = 0.f;
	if (colors.classicControls || IsLightTheme(colors))
	{
		btn = WithAlpha(colors.inputBg, 0.95f);
		btnHover = colors.navHover;
		btnActive = colors.navActive;
	}
	else
	{
		btn = Blend(colors.tileBg, colors.textPrimary, 0.14f);
		btnHover = Blend(colors.tileBg, colors.textPrimary, 0.22f);
		btnActive = Blend(colors.tileBg, colors.textPrimary, 0.28f);
		borderAlpha = 0.70f;
	}
	ImGui::PushStyleColor(ImGuiCol_Button, btn);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
	if (borderAlpha > 0.f)
		ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(colors.tileBorder, borderAlpha));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, borderAlpha > 0.f ? 1.f : 0.f);

	wchar_t wide[] = { static_cast<wchar_t>(iconCode), 0 };
	char utf8[8] = {};
	const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);

	ImFont* iconFont = fonts.GetIconFont();
	if (iconFont)
		ImGui::PushFont(iconFont);
	ImGui::PushStyleColor(ImGuiCol_Text, enabled ? colors.textPrimary : colors.textMuted);
	const bool pressed = ImGui::Button(len > 0 ? utf8 : "?", size);
	ImGui::PopStyleColor();
	if (iconFont)
		ImGui::PopFont();

	if (tooltip)
		SetItemTooltip("%s", tooltip);

	ImGui::PopStyleVar();
	ImGui::PopStyleColor(borderAlpha > 0.f ? 4 : 3);
	ImGui::PopID();

	if (!enabled)
		ImGui::EndDisabled();
	return pressed;
}
