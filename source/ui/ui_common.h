#pragma once

#include <cstdint>

#include "gfx/theme_manager.h"
#include "imgui.h"
#include "ui/ui_types.h"
#include "zapret/zapret_update_check.h"

class FontManager;

namespace UiCommon
{
	ImVec4 StatusColor(const UiAccentColors& accents, bool active, bool ok = true);
	// Theme-independent ok/warn/fail for version badges (sidebar, page titles, title bar).
	ImVec4 FixedVersionStatusAccent(ComponentUpdateStatus status);
	// Start / Stop action buttons — always green / red across themes.
	ImVec4 FixedStartAccent();
	ImVec4 FixedStopAccent();
	// Traffic graph / rate cards — always green download + blue upload.
	ImVec4 FixedDownloadAccent();
	ImVec4 FixedUploadAccent();
	void PageTitle(
		FontManager& fonts,
		uint32_t iconCode,
		const char* title,
		const char* subtitle,
		const UiThemeColors& colors);
	// Optional version chip next to the title (colored frame by update status).
	void PageTitle(
		FontManager& fonts,
		uint32_t iconCode,
		const char* title,
		const char* subtitle,
		const UiThemeColors& colors,
		const char* version,
		const ImVec4& versionAccent);
	// Same as above, plus optional update button immediately after the version badge.
	// Returns true if the update button was clicked.
	bool PageTitle(
		FontManager& fonts,
		uint32_t iconCode,
		const char* title,
		const char* subtitle,
		const UiThemeColors& colors,
		const char* version,
		const ImVec4& versionAccent,
		const char* updateButtonLabel,
		bool updateButtonEnabled);
	void VersionBadge(const char* version, const ImVec4& accent, const UiThemeColors& colors);
	// When centerInLine is false, badge top aligns to the current cursor (use after SameLine with text).
	void VersionBadge(const char* version, const ImVec4& accent, const UiThemeColors& colors, bool centerInLine);
	void SectionHeader(const char* title, const UiThemeColors& colors);
	void CaptionText(const char* text, const UiThemeColors& colors, float wrapWidth = 0.f);
	void CardGap();
	bool BeginCard(const char* id, float width, const UiThemeColors& colors);
	void EndCard();
	bool AccentButton(const char* label, ImVec2 size, const ImVec4& accent, const UiThemeColors& colors, bool enabled = true);
	bool SecondaryButton(const char* label, ImVec2 size, const UiThemeColors& colors, bool enabled = true);
	void StatusBadge(const char* label, ImVec4 color, const UiThemeColors& colors);
	float DrawServiceInline(
		const char* name,
		bool online,
		const ImVec4& brandColor,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		const char* statusOverride = nullptr);
	void InfoLine(const char* label, const char* value, const UiThemeColors& colors);
	ImVec4 WithAlpha(const ImVec4& color, float alpha);
	bool IsLightTheme(const UiThemeColors& colors);
	void SyncImGuiStyle(const UiThemeColors& colors);
	float ExpSmooth(float current, float target, float deltaTime, float speed);
	float AnimateMix(float current, bool expanded, float deltaTime, float speed);
	bool ToggleSwitch(const char* id, float mix, const UiThemeColors& colors);
	void PushInputStyle(const UiThemeColors& colors);
	void PopInputStyle();
	void PushSliderStyle(const UiThemeColors& colors);
	void PopSliderStyle();
	bool InputText(const char* id, char* buffer, int bufferSize, const UiThemeColors& colors, ImVec2 size = { 0.f, 34.f });
	bool StyledCheckbox(const char* label, bool* value, const UiThemeColors& colors);
	bool SettingRow(const char* label, float width, const UiThemeColors& colors, float mix);
	bool StrategyButton(
		int widgetId,
		const char* label,
		bool selected,
		ImVec2 size,
		const UiThemeColors& colors,
		const ImVec4& accent);

	struct StrategyIndicatorState
	{
		bool hasResult = false;
		bool discordOk = false;
		bool youtubeOk = false;
		bool telegramOk = false;
		bool isBest = false;
	};

	void DrawStrategyIndicators(
		ImVec2 buttonMin,
		const StrategyIndicatorState& state,
		const UiThemeColors& colors);

	enum class UiTableAlign
	{
		Left,
		Center,
		Right
	};

	inline constexpr float kTableHeight = 280.f;

	ImGuiTableFlags DefaultTableFlags(bool sortable = false);
	ImGuiTableFlags StretchableTableFlags(bool sortable = false);
	void PushTableStyle(const UiThemeColors& colors);
	void PopTableStyle();
	void TableHeadersRow(const UiThemeColors& colors);
	void TableHeadersRowCentered(const UiThemeColors& colors);
	void TableTextAligned(const char* text, UiTableAlign align);
	float TableRowInputHeight(const UiThemeColors& colors);
	float TableRowMinHeight(float contentHeight);
	void TableAlignTextY(float contentHeight);
	void TableAlignFrameY(float contentHeight);
	bool TableRowSelectable(const char* label, bool selected, float contentHeight);
	bool IconToolButton(
		FontManager& fonts,
		uint32_t iconCode,
		const char* id,
		const char* tooltip,
		const UiThemeColors& colors,
		ImVec2 size = { 30.f, 30.f },
		bool enabled = true);

	// Tooltips: 2.5s hover delay + rounded corners (WindowRounding for ImGui tooltip windows).
	void ConfigureTooltips(ImGuiStyle& style);
	void ShowTooltip(const char* fmt, ...) IM_FMTARGS(1);
	void SetItemTooltip(const char* fmt, ...) IM_FMTARGS(1);
}
