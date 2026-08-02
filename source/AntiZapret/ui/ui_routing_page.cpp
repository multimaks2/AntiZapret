#include "ui/ui_routing_page.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_domain_routes.h"
#include "vpn/vpn_manager.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	const char* kDnsModes[] = { "Системный DNS", "Встроенный DNS" };
	const char* kBootstrapDns[] = {
		"Cloudflare 1.1.1.1",
		"Google 8.8.8.8",
		"Quad9 9.9.9.9",
		"Яндекс 77.88.8.8",
		"OpenDNS 208.67.222.222",
	};
	const char* kBootstrapType[] = { "UDP", "TCP", "DoT (TLS)", "DoH (HTTPS)" };
	const char* kProxyDns[] = {
		"Google 8.8.8.8",
		"Cloudflare 1.1.1.1",
		"Quad9 9.9.9.9",
		"OpenDNS 208.67.222.222",
	};
	const char* kProxyType[] = { "TCP", "DoT (TLS)", "DoH (HTTPS)" };
	const char* kActions[] = { "Прямой", "Прокси", "Блокировка" };
	const char* kServiceRouteModes[] = {
		"Напрямую",
		"VPN",
	};

	int ServiceModeToUi(ServiceRouteMode mode)
	{
		switch (mode)
		{
		case ServiceRouteMode::VpnTunnel:
		case ServiceRouteMode::VpnProxy:
			return 1;
		default:
			return 0;
		}
	}

	ServiceRouteMode UiToServiceMode(int uiMode)
	{
		return uiMode == 1 ? ServiceRouteMode::VpnTunnel : ServiceRouteMode::Antizapret;
	}

	constexpr float kColNum = 30.f;

	enum class RuleRowResult
	{
		Continue,
		Delete,
	};

	std::string IconUtf8(uint32_t codepoint)
	{
		wchar_t wide[] = { static_cast<wchar_t>(codepoint), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof utf8), nullptr, nullptr);
		if (len <= 0)
			return {};
		return std::string(utf8, static_cast<size_t>(len));
	}

	bool DrawMiniToggle(const char* id, float mix, const UiThemeColors& colors)
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
		const ImVec4 bg = {
			offBg.x + (onBg.x - offBg.x) * mix,
			offBg.y + (onBg.y - offBg.y) * mix,
			offBg.z + (onBg.z - offBg.z) * mix,
			offBg.w + (onBg.w - offBg.w) * mix,
		};
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

	bool DrawInlineFieldCombo(
		const char* fieldId,
		const char* label,
		int& current,
		const char* const* items,
		int count,
		float width,
		float labelWidth,
		const UiThemeColors& colors)
	{
		ImGui::PushID(fieldId);
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();
		ImGui::SameLine(labelWidth);
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(width - labelWidth);
		const bool changed = ImGui::Combo("##value", &current, items, count);
		UiCommon::PopInputStyle();
		ImGui::PopID();
		ImGui::Dummy({ 0.f, 8.f });
		return changed;
	}

	bool DrawInlineToggleRow(
		const char* fieldId,
		const char* label,
		bool& value,
		float& mix,
		float width,
		const UiThemeColors& colors)
	{
		const float toggleW = 40.f;
		const float lineStart = ImGui::GetCursorStartPos().x;
		ImGui::PushID(fieldId);
		ImGui::AlignTextToFramePadding();
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::SetCursorPosX(lineStart + width - toggleW);
		mix = UiCommon::AnimateMix(mix, value, ImGui::GetIO().DeltaTime, 10.f);
		bool changed = false;
		if (DrawMiniToggle("##tog", mix, colors))
		{
			value = !value;
			changed = true;
		}
		ImGui::PopID();
		ImGui::Dummy({ width, 8.f });
		return changed;
	}

	void DrawHorizontalSeparator(float width, const UiThemeColors& colors)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		const float y = pos.y + 2.f;
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(pos.x, y),
			ImVec2(pos.x + width, y),
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.45f)),
			1.f);
		ImGui::Dummy({ width, 10.f });
	}

	bool DrawCollapsedSection(const char* id, const char* title, const UiThemeColors& colors, bool defaultOpen = false)
	{
		ImGui::PushID(id);
		ImGui::PushStyleColor(ImGuiCol_Header, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.navHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		const ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
		const bool open = ImGui::CollapsingHeader(title, flags);
		ImGui::PopStyleColor(4);
		ImGui::PopID();
		return open;
	}

	bool DrawFoldRevealBar(
		FontManager& fonts,
		const char* id,
		const char* title,
		const char* captionCollapsed,
		const char* captionExpanded,
		int itemCount,
		bool& expanded,
		float width,
		const UiThemeColors& colors,
		uint32_t groupIcon = 0)
	{
		const float rowH = 52.f;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(id, { width, rowH });
		const bool hovered = ImGui::IsItemHovered();
		const bool pressed = ImGui::IsItemClicked();
		if (pressed)
			expanded = !expanded;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 rowMax = { pos.x + width, pos.y + rowH };
		const ImVec4 fill = hovered
			? UiCommon::WithAlpha(colors.navHover, 1.f)
			: colors.navActive;
		drawList->AddRectFilled(pos, rowMax, ImGui::GetColorU32(fill), UiMetrics::kCardRadius);
		drawList->AddRect(
			pos,
			rowMax,
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, hovered ? 0.75f : 0.45f)),
			UiMetrics::kCardRadius,
			0,
			1.f);

		// Segoe MDL2: ChevronDown / ChevronRight
		constexpr uint32_t kChevronDown = 0xE70D;
		constexpr uint32_t kChevronRight = 0xE76C;
		const std::string chevron = IconUtf8(expanded ? kChevronDown : kChevronRight);
		const std::string groupGlyph = groupIcon ? IconUtf8(groupIcon) : std::string();
		ImFont* iconFont = fonts.GetIconFont();
		const float titleY = pos.y + 10.f;
		ImGui::SetCursorScreenPos({ pos.x + 14.f, titleY });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		if (iconFont && !chevron.empty())
		{
			ImGui::PushFont(iconFont);
			ImGui::TextUnformatted(chevron.c_str());
			ImGui::PopFont();
			ImGui::SameLine(0.f, 8.f);
			ImGui::SetCursorScreenPos({
				ImGui::GetCursorScreenPos().x,
				titleY + (iconFont->LegacySize - ImGui::GetTextLineHeight()) * 0.5f
			});
		}
		if (iconFont && !groupGlyph.empty())
		{
			ImGui::PushFont(iconFont);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted(groupGlyph.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
			ImGui::SameLine(0.f, 8.f);
			ImGui::SetCursorScreenPos({
				ImGui::GetCursorScreenPos().x,
				titleY + (iconFont->LegacySize - ImGui::GetTextLineHeight()) * 0.5f
			});
		}
		ImGui::TextUnformatted(title);
		ImGui::PopStyleColor();

		char countBuf[32];
		snprintf(countBuf, sizeof countBuf, "%d", itemCount);
		const ImVec2 countSize = ImGui::CalcTextSize(countBuf);
		const float badgePadX = 8.f;
		const float badgeH = 20.f;
		const float badgeW = countSize.x + badgePadX * 2.f;
		const ImVec2 badgeMin = { pos.x + width - 14.f - badgeW, pos.y + (rowH - badgeH) * 0.5f };
		const ImVec2 badgeMax = { badgeMin.x + badgeW, badgeMin.y + badgeH };
		drawList->AddRectFilled(
			badgeMin,
			badgeMax,
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.18f)),
			10.f);
		drawList->AddText(
			{ badgeMin.x + badgePadX, badgeMin.y + (badgeH - countSize.y) * 0.5f },
			ImGui::GetColorU32(colors.textMuted),
			countBuf);

		ImGui::SetCursorScreenPos({ pos.x + 34.f, pos.y + 30.f });
		UiCommon::CaptionText(
			expanded ? captionExpanded : captionCollapsed,
			colors,
			width - 48.f);

		ImGui::SetCursorScreenPos({ pos.x, pos.y + rowH });
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		return expanded;
	}

	bool IsGameSection(ServiceCatalogSection section)
	{
		return section == ServiceCatalogSection::ForeignGames
			|| section == ServiceCatalogSection::ForeignSteamNew;
	}

	bool DrawTableDeleteButton(
		FontManager& fonts,
		const UiThemeColors& colors,
		const UiAccentColors& accents)
	{
		const float size = ImGui::GetTextLineHeight();
		wchar_t wide[] = { static_cast<wchar_t>(0xE74D), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(
			CP_UTF8,
			0,
			wide,
			1,
			utf8,
			static_cast<int>(sizeof utf8),
			nullptr,
			nullptr);

		(void)colors;
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UiCommon::WithAlpha(accents.fail, 0.12f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, UiCommon::WithAlpha(accents.fail, 0.22f));
		ImGui::PushStyleColor(ImGuiCol_Text, accents.fail);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));

		ImFont* iconFont = fonts.GetIconFont();
		if (iconFont)
			ImGui::PushFont(iconFont);
		ImGui::PushID("delete");
		const bool pressed = ImGui::Button(len > 0 ? utf8 : "?", ImVec2(size, size));
		ImGui::PopID();
		if (iconFont)
			ImGui::PopFont();

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);
		UiCommon::SetItemTooltip("Удалить");
		return pressed;
	}

	RuleRowResult DrawRuleTableRow(
		int rowNum,
		const char* label,
		int& action,
		int rowIndex,
		int& selectedIndex,
		FontManager& fonts,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		bool& changed)
	{
		const float rowContentH = UiCommon::TableRowInputHeight(colors);
		ImGui::TableNextRow(ImGuiTableRowFlags_None, UiCommon::TableRowMinHeight(rowContentH));

		const bool rowSelected = selectedIndex == rowIndex;

		char numBuf[8];
		snprintf(numBuf, sizeof numBuf, "%d", rowNum);

		ImGui::TableSetColumnIndex(0);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		if (UiCommon::TableRowSelectable(numBuf, rowSelected, rowContentH))
			selectedIndex = rowIndex;

		ImGui::TableSetColumnIndex(1);
		{
			const float columnWidth = ImGui::GetColumnWidth();
			const float iconSize = ImGui::GetTextLineHeight();
			const float pad = ImGui::GetStyle().CellPadding.x;
			const float cellStartX = ImGui::GetCursorPosX();
			const float cellStartY = ImGui::GetCursorPosY();
			const float textWidth = columnWidth - iconSize - 6.f - pad * 2.f;

			UiCommon::TableAlignTextY(rowContentH);
			ImGui::PushTextWrapPos(cellStartX + textWidth);
			ImGui::TextUnformatted(label);
			ImGui::PopTextWrapPos();

			ImGui::SetCursorPos(ImVec2(
				cellStartX + columnWidth - iconSize - pad,
				cellStartY + (rowContentH - iconSize) * 0.5f));
			ImGui::SetNextItemAllowOverlap();
			if (DrawTableDeleteButton(fonts, colors, accents))
			{
				ImGui::PopStyleColor();
				return RuleRowResult::Delete;
			}
		}

		ImGui::TableSetColumnIndex(2);
		ImGui::SetNextItemAllowOverlap();
		UiCommon::PushInputStyle(colors);
		UiCommon::TableAlignFrameY(rowContentH);
		ImGui::SetNextItemWidth(-1.f);
		if (ImGui::Combo("##action", &action, kActions, 3))
			changed = true;
		UiCommon::PopInputStyle();

		ImGui::PopStyleColor();
		return RuleRowResult::Continue;
	}

	bool DrawDnsPairRow(
		const char* rowId,
		int& server,
		int& type,
		const char* const* servers,
		int serverCount,
		const char* const* types,
		int typeCount,
		float width,
		const UiThemeColors& colors)
	{
		ImGui::PushID(rowId);
		const float typeW = 132.f;
		const float gap = 8.f;
		const float serverW = width - typeW - gap;
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(serverW);
		bool changed = ImGui::Combo("##server", &server, servers, serverCount);
		ImGui::SameLine(0.f, gap);
		ImGui::SetNextItemWidth(typeW);
		changed = ImGui::Combo("##type", &type, types, typeCount) || changed;
		UiCommon::PopInputStyle();
		ImGui::PopID();
		ImGui::Dummy({ 0.f, 6.f });
		return changed;
	}

	enum class ServiceRowChange
	{
		None,
		Mode,
		Toggle,
		Delete,
	};

	std::string ResolveServiceOpenUrl(const ServiceRouteEntry& service)
	{
		std::vector<std::string> domains;
		std::vector<std::string> processes;
		VpnServiceRoutes::CollectRouteTargets(service, domains, processes);
		if (domains.empty() || domains.front().empty())
			return {};
		return "https://" + domains.front();
	}

	bool OpenUrlInDefaultBrowser(const char* url)
	{
		if (!url || !url[0])
			return false;
		return reinterpret_cast<intptr_t>(
			ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL)) > 32;
	}

	ServiceRowChange DrawServiceRow(
		FontManager& fonts,
		const char* scope,
		const char* serviceId,
		uint32_t iconCode,
		bool brandIcon,
		const char* title,
		const char* description,
		bool& enabled,
		int& mode,
		float width,
		const UiThemeColors& colors,
		float& toggleMix,
		const char* openUrl,
		bool canDelete,
		const UiAccentColors* accents)
	{
		const float rowH = 58.f;
		const float iconArea = 36.f;
		const float leftPad = 12.f;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy({ width, rowH });

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 rowMax = { pos.x + width, pos.y + rowH };
		drawList->AddRectFilled(pos, rowMax, ImGui::GetColorU32(colors.tileBg), UiMetrics::kCardRadius);
		drawList->AddRect(
			pos,
			rowMax,
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.55f)),
			UiMetrics::kCardRadius,
			0,
			1.f);

		const bool iconClickable = openUrl && openUrl[0];
		bool iconHovered = false;
		ImGui::PushID(scope);
		ImGui::PushID(serviceId);
		if (iconClickable)
		{
			const float hit = 28.f;
			ImGui::SetCursorScreenPos(ImVec2(
				pos.x + leftPad - 4.f,
				pos.y + (rowH - hit) * 0.5f));
			ImGui::InvisibleButton("##open_site", { hit, hit });
			iconHovered = ImGui::IsItemHovered();
			if (iconHovered)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				UiCommon::SetItemTooltip("Открыть сайт\n%s", openUrl);
			}
			if (ImGui::IsItemClicked())
				OpenUrlInDefaultBrowser(openUrl);
		}

		const std::string glyph = IconUtf8(iconCode);
		ImFont* iconFont = brandIcon ? fonts.GetBrandFont() : fonts.GetIconFont();
		if (iconFont && !glyph.empty())
		{
			const float iconH = iconFont->LegacySize;
			ImGui::SetCursorScreenPos(ImVec2(pos.x + leftPad, pos.y + (rowH - iconH) * 0.5f));
			ImGui::PushFont(iconFont);
			ImGui::PushStyleColor(
				ImGuiCol_Text,
				iconHovered ? colors.textPrimary : colors.textMuted);
			ImGui::TextUnformatted(glyph.c_str());
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}
		ImGui::PopID();
		ImGui::PopID();

		const float textX = pos.x + leftPad + iconArea;
		ImGui::SetCursorScreenPos(ImVec2(textX, pos.y + 10.f));
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(title);
		ImGui::PopStyleColor();
		ImGui::SetCursorScreenPos(ImVec2(textX, pos.y + 28.f));
		UiCommon::CaptionText(description, colors, width - iconArea - 250.f);

		const float comboW = 168.f;
		const float toggleW = 40.f;
		const float toggleLabelW = 28.f;
		const float deleteW = canDelete ? 28.f : 0.f;
		const float rightPad = 12.f;
		const float controlsY = pos.y + (rowH - UiMetrics::kSmallBtnHeight) * 0.5f;

		ImGui::PushID(scope);
		ImGui::PushID(serviceId);
		ImGui::PushID("controls");
		ServiceRowChange change = ServiceRowChange::None;

		float rightX = pos.x + width - rightPad;
		if (canDelete && accents)
		{
			rightX -= deleteW;
			ImGui::SetCursorScreenPos(ImVec2(rightX, pos.y + (rowH - 22.f) * 0.5f));
			if (DrawTableDeleteButton(fonts, colors, *accents))
				change = ServiceRowChange::Delete;
			rightX -= 8.f;
		}

		ImGui::SetCursorScreenPos(ImVec2(rightX - toggleW, pos.y + (rowH - 22.f) * 0.5f));
		toggleMix = UiCommon::AnimateMix(toggleMix, enabled, ImGui::GetIO().DeltaTime, 10.f);
		if (change == ServiceRowChange::None && DrawMiniToggle("##sw", toggleMix, colors))
		{
			enabled = !enabled;
			change = ServiceRowChange::Toggle;
		}

		ImGui::SetCursorScreenPos(ImVec2(
			rightX - toggleW - 6.f - toggleLabelW,
			pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, enabled ? colors.textPrimary : colors.textMuted);
		ImGui::TextUnformatted(enabled ? "Вкл" : "Выкл");
		ImGui::PopStyleColor();

		ImGui::SetCursorScreenPos(ImVec2(
			rightX - toggleW - toggleLabelW - 12.f - comboW,
			controlsY));
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(comboW);
		if (!enabled)
			ImGui::BeginDisabled();
		if (change == ServiceRowChange::None && ImGui::Combo("##mode", &mode, kServiceRouteModes, 2))
			change = ServiceRowChange::Mode;
		if (!enabled)
			ImGui::EndDisabled();
		UiCommon::PopInputStyle();
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();

		ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + rowH));
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
		return change;
	}
}

void UiRoutingPage::EnsureLoaded()
{
	VpnStoreSettings settings;
	m_store.LoadSettings(settings);
	// Режим Proxy/Tunnel меняется на вкладке VPN — всегда подтягиваем актуальное значение.
	m_transportMode = settings.transportMode;
	if (m_loaded)
		return;

	m_dnsMode = settings.dnsMode;
	m_bootstrapDns = settings.bootstrapDns;
	m_bootstrapType = settings.bootstrapType;
	m_proxyDns = settings.proxyDns;
	m_proxyType = settings.proxyType;
	m_loaded = true;
}

void UiRoutingPage::ScheduleApply()
{
	constexpr float kApplyDebounceSec = 0.12f;
	m_applyDebounce = kApplyDebounceSec;
}

void UiRoutingPage::FlushApplyIfDue(float deltaTime)
{
	if (m_applyDebounce <= 0.f)
		return;

	m_applyDebounce -= deltaTime;
	if (m_applyDebounce > 0.f)
		return;

	ApplyRouting();
}

void UiRoutingPage::ApplyRouting()
{
	if (!m_serviceRoutesLoaded || m_serviceRoutesLoading)
		return;

	VpnStoreSettings settings;
	m_store.LoadSettings(settings);
	// transportMode задаётся на вкладке VPN — не перезаписываем.
	settings.dnsMode = m_dnsMode;
	settings.bootstrapDns = m_bootstrapDns;
	settings.bootstrapType = m_bootstrapType;
	settings.proxyDns = m_proxyDns;
	settings.proxyType = m_proxyType;
	++settings.routingRevision;
	m_store.SaveSettings(settings);
	VpnServiceRoutes::Save(m_serviceRoutes);
	VpnDomainRoutes::Save(m_domainRules);

	if (m_vpnManager)
		m_vpnManager->RequestReloadFromStore();

	m_applyDebounce = 0.f;
	m_applySuccessTimer = 2.5f;
	AppLog::Instance().Append(LogSource::VpnRouting, "Настройки маршрутизации применены.");
}

void UiRoutingPage::EnsureServiceRoutesLoaded()
{
	if (m_serviceRoutesLoaded)
	{
		if (m_serviceMix.size() != m_serviceRoutes.size())
			m_serviceMix.resize(m_serviceRoutes.size(), 1.f);
		SyncDiscordRouteFromFixDiscord();
		return;
	}

	if (m_serviceRoutesLoading)
		return;

	m_serviceRoutesLoading = true;
	std::vector<ServiceRouteEntry> loaded;
	VpnServiceRoutes::Load(loaded);
	m_serviceRoutes = std::move(loaded);
	m_serviceMix.assign(m_serviceRoutes.size(), 1.f);
	m_serviceRoutesLoaded = true;
	m_serviceRoutesLoading = false;
	SyncDiscordRouteFromFixDiscord();
}

void UiRoutingPage::SyncDiscordRouteFromFixDiscord()
{
	if (!m_serviceRoutesLoaded)
		return;

	VpnStoreSettings settings;
	m_store.LoadSettings(settings);
	if (!VpnServiceRoutes::ApplyFixDiscordToRoutes(m_serviceRoutes, settings.fixDiscord))
		return;
	VpnServiceRoutes::Save(m_serviceRoutes);
}

void UiRoutingPage::WriteFixDiscordFromDiscordRoute()
{
	bool fixDiscord = false;
	for (const ServiceRouteEntry& entry : m_serviceRoutes)
	{
		if (entry.id != "discord")
			continue;
		fixDiscord = VpnServiceRoutes::IsFixDiscordEffective(entry);
		break;
	}

	VpnStoreSettings settings;
	m_store.LoadSettings(settings);
	if (settings.fixDiscord == fixDiscord)
		return;
	settings.fixDiscord = fixDiscord;
	++settings.routingRevision;
	m_store.SaveSettings(settings);
}

void UiRoutingPage::EnsureDomainRulesLoaded()
{
	if (m_domainRulesLoaded)
		return;

	VpnDomainRoutes::Load(m_domainRules);
	m_domainRulesLoaded = true;
}

bool UiRoutingPage::MatchesTextSearch(const char* text) const
{
	if (!m_serviceSearch[0] || !text || !text[0])
		return !m_serviceSearch[0];

	auto decodeUtf8 = [](const char*& p) -> unsigned
	{
		const unsigned char c0 = static_cast<unsigned char>(*p);
		if (c0 < 0x80)
		{
			++p;
			return c0;
		}
		if ((c0 & 0xE0) == 0xC0 && p[1])
		{
			const unsigned cp = ((c0 & 0x1Fu) << 6) | (static_cast<unsigned char>(p[1]) & 0x3Fu);
			p += 2;
			return cp;
		}
		if ((c0 & 0xF0) == 0xE0 && p[1] && p[2])
		{
			const unsigned cp = ((c0 & 0x0Fu) << 12)
				| ((static_cast<unsigned char>(p[1]) & 0x3Fu) << 6)
				| (static_cast<unsigned char>(p[2]) & 0x3Fu);
			p += 3;
			return cp;
		}
		if ((c0 & 0xF8) == 0xF0 && p[1] && p[2] && p[3])
		{
			const unsigned cp = ((c0 & 0x07u) << 18)
				| ((static_cast<unsigned char>(p[1]) & 0x3Fu) << 12)
				| ((static_cast<unsigned char>(p[2]) & 0x3Fu) << 6)
				| (static_cast<unsigned char>(p[3]) & 0x3Fu);
			p += 4;
			return cp;
		}
		++p;
		return c0;
	};

	auto keepCodepoint = [](unsigned cp) -> bool
	{
		if (cp >= '0' && cp <= '9')
			return true;
		if (cp >= 'a' && cp <= 'z')
			return true;
		if (cp >= 'A' && cp <= 'Z')
			return true;
		if (cp == ' ' || cp == '\t')
			return true;
		// Latin-1 / Latin Extended letters (accents)
		if (cp >= 0x00C0 && cp <= 0x024F)
			return true;
		// Cyrillic
		if (cp >= 0x0400 && cp <= 0x04FF)
			return true;
		return false;
	};

	auto normalize = [&](const char* src) -> std::string
	{
		std::string out;
		out.reserve(64);
		const char* p = src;
		while (p && *p)
		{
			unsigned cp = decodeUtf8(p);
			if (!keepCodepoint(cp))
				continue;
			if (cp >= 'A' && cp <= 'Z')
				cp = cp - 'A' + 'a';
			else if (cp >= 0x0410 && cp <= 0x042F) // А-Я → а-я
				cp = cp - 0x0410 + 0x0430;
			else if (cp == 0x0401) // Ё
				cp = 0x0451;
			if (cp < 0x80)
			{
				out.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800)
			{
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else
			{
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		// collapse spaces
		std::string compact;
		compact.reserve(out.size());
		bool prevSpace = false;
		for (char ch : out)
		{
			const bool space = ch == ' ' || ch == '\t';
			if (space)
			{
				if (!prevSpace && !compact.empty())
					compact.push_back(' ');
				prevSpace = true;
			}
			else
			{
				compact.push_back(ch);
				prevSpace = false;
			}
		}
		while (!compact.empty() && compact.back() == ' ')
			compact.pop_back();
		return compact;
	};

	const std::string needle = normalize(m_serviceSearch);
	if (needle.empty())
		return true;
	const std::string hay = normalize(text);
	return hay.find(needle) != std::string::npos;
}

bool UiRoutingPage::MatchesServiceSearch(const ServiceRouteEntry& service) const
{
	if (!m_serviceSearch[0])
		return true;
	return MatchesTextSearch(service.name.c_str())
		|| MatchesTextSearch(service.description.c_str())
		|| MatchesTextSearch(service.id.c_str());
}

void UiRoutingPage::DrawServiceRoutes(
	FontManager& fonts,
	float width,
	const UiThemeColors& colors,
	const UiAccentColors& accents)
{
	const bool searching = m_serviceSearch[0] != 0;
	const bool showAdultCatalog = m_appSettings && m_appSettings->GetConfirmAdult();

	auto sectionFoldOpen = [&](ServiceCatalogSection section) -> bool&
	{
		return m_sectionExpanded[static_cast<int>(section)];
	};

	auto foldCaptionShow = [](ServiceCatalogSection section) -> const char*
	{
		switch (section)
		{
		case ServiceCatalogSection::ForeignTools: return "Показать утилиты";
		case ServiceCatalogSection::ForeignSocial: return "Показать соцсети и мессенджеры";
		case ServiceCatalogSection::ForeignStreaming: return "Показать стриминг и музыку";
		case ServiceCatalogSection::ForeignBrowser: return "Показать браузеры";
		case ServiceCatalogSection::ForeignAI: return "Показать AI-сервисы";
		case ServiceCatalogSection::ForeignDev: return "Показать сервисы для разработчиков";
		case ServiceCatalogSection::ForeignLaunchers: return "Показать игровые лаунчеры";
		case ServiceCatalogSection::ForeignGames: return "Показать онлайн-игры и новинки Steam";
		case ServiceCatalogSection::ForeignSteamNew: return "Показать новинки Steam";
		case ServiceCatalogSection::ForeignAdult: return "Показать 18+ сайты";
		case ServiceCatalogSection::ForeignMisc: return "Показать торрент клиенты";
		case ServiceCatalogSection::ForeignStandalone: return "Показать Windows";
		case ServiceCatalogSection::RussianBrowser: return "Показать браузеры";
		case ServiceCatalogSection::RussianEco: return "Показать экосистемы и мессенджеры";
		case ServiceCatalogSection::RussianBank: return "Показать банки и платежи";
		case ServiceCatalogSection::RussianGov: return "Показать госуслуги";
		case ServiceCatalogSection::RussianShop: return "Показать маркетплейсы и магазины";
		case ServiceCatalogSection::RussianDelivery: return "Показать доставку и такси";
		case ServiceCatalogSection::RussianTelecom: return "Показать телеком";
		case ServiceCatalogSection::RussianStreaming: return "Показать стриминг и видео";
		case ServiceCatalogSection::RussianTravel: return "Показать транспорт и путешествия";
		case ServiceCatalogSection::RussianProperty: return "Показать недвижимость и авто";
		case ServiceCatalogSection::RussianWorkHealth: return "Показать работу, медицину, безопасность";
		case ServiceCatalogSection::RussianMisc: return "Показать проверку IP";
		case ServiceCatalogSection::CustomApps:
		case ServiceCatalogSection::CustomSites: return "Показать мною добавленное";
		default: return "Показать список";
		}
	};

	auto foldCaptionHide = [](ServiceCatalogSection section) -> const char*
	{
		switch (section)
		{
		case ServiceCatalogSection::ForeignGames: return "Скрыть онлайн-игры и новинки Steam";
		case ServiceCatalogSection::ForeignAdult: return "Скрыть список 18+ сайтов";
		case ServiceCatalogSection::CustomApps:
		case ServiceCatalogSection::CustomSites: return "Скрыть мною добавленное";
		default: return "Скрыть список";
		}
	};

	auto sectionFoldOpenDefault = [&](ServiceCatalogSection section, bool defaultOpen) -> bool&
	{
		const int key = static_cast<int>(section);
		const auto it = m_sectionExpanded.find(key);
		if (it == m_sectionExpanded.end())
			return m_sectionExpanded.emplace(key, defaultOpen).first->second;
		return it->second;
	};

	auto drawServiceAt = [&](size_t index) -> bool
	{
		ServiceRouteEntry& service = m_serviceRoutes[index];
		int uiMode = ServiceModeToUi(service.mode);
		const std::string openUrl = (IsGameSection(service.section) || service.kind == ServiceCatalogKind::App)
			? std::string()
			: ResolveServiceOpenUrl(service);
		const ServiceRowChange change = DrawServiceRow(
			fonts,
			"services",
			service.id.c_str(),
			service.icon,
			service.brandIcon,
			service.name.c_str(),
			service.description.c_str(),
			service.enabled,
			uiMode,
			width,
			colors,
			m_serviceMix[index],
			openUrl.empty() ? nullptr : openUrl.c_str(),
			service.custom,
			&accents);
		if (change == ServiceRowChange::Delete && service.custom)
		{
			m_serviceRoutes.erase(m_serviceRoutes.begin() + static_cast<std::ptrdiff_t>(index));
			if (index < m_serviceMix.size())
				m_serviceMix.erase(m_serviceMix.begin() + static_cast<std::ptrdiff_t>(index));
			ApplyRouting();
			return true;
		}
		if (change == ServiceRowChange::Mode)
		{
			service.mode = UiToServiceMode(uiMode);
			if (service.id == "discord")
				WriteFixDiscordFromDiscordRoute();
			ApplyRouting();
		}
		else if (change == ServiceRowChange::Toggle)
		{
			if (service.id == "discord")
				WriteFixDiscordFromDiscordRoute();
			ScheduleApply();
		}
		return false;
	};

	auto drawKindSections = [&](ServiceCatalogKind kind, const char* foldIdPrefix) -> int
	{
		struct SectionBucket
		{
			ServiceCatalogSection section = ServiceCatalogSection::ForeignTools;
			std::vector<size_t> indices;
		};
		std::vector<SectionBucket> buckets;
		std::vector<size_t> gameIndices;
		std::vector<size_t> customIndices;
		std::vector<size_t> standaloneIndices;
		const ServiceCatalogSection customSection = kind == ServiceCatalogKind::App
			? ServiceCatalogSection::CustomApps
			: ServiceCatalogSection::CustomSites;

		for (size_t i = 0; i < m_serviceRoutes.size(); ++i)
		{
			if (i >= m_serviceMix.size())
				m_serviceMix.resize(m_serviceRoutes.size(), 1.f);

			const ServiceRouteEntry& service = m_serviceRoutes[i];
			if (service.kind != kind || !MatchesServiceSearch(service))
				continue;

			if (VpnServiceRoutes::IsAdultSection(service.section) && !showAdultCatalog)
				continue;

			if (service.custom || service.section == customSection)
			{
				customIndices.push_back(i);
				continue;
			}

			if (kind == ServiceCatalogKind::App && IsGameSection(service.section))
			{
				gameIndices.push_back(i);
				continue;
			}

			if (service.section == ServiceCatalogSection::ForeignStandalone)
			{
				standaloneIndices.push_back(i);
				continue;
			}

			if (buckets.empty() || buckets.back().section != service.section)
				buckets.push_back({ service.section, {} });
			buckets.back().indices.push_back(i);
		}

		int drawn = 0;
		bool firstSection = true;

		auto drawFoldedSection = [&](
			ServiceCatalogSection sectionKey,
			const char* title,
			const char* id,
			const std::vector<size_t>& indices,
			bool& expanded,
			bool showSubHeaders,
			bool allowEmpty = false)
		{
			if (indices.empty() && !allowEmpty)
				return;

			ImGui::Dummy({ 0.f, firstSection ? 2.f : 6.f });
			firstSection = false;

			bool showItems = true;
			if (!searching)
			{
				showItems = DrawFoldRevealBar(
					fonts,
					id,
					title,
					foldCaptionShow(sectionKey),
					foldCaptionHide(sectionKey),
					static_cast<int>(indices.size()),
					expanded,
					width,
					colors,
					VpnServiceRoutes::SectionIcon(sectionKey));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
				char sectionTitle[96];
				snprintf(
					sectionTitle,
					sizeof sectionTitle,
					"%s (%zu)",
					title,
					indices.size());
				ImGui::TextUnformatted(sectionTitle);
				ImGui::PopStyleColor();
				ImGui::Dummy({ 0.f, 2.f });
			}

			if (!showItems)
				return;

			ServiceCatalogSection lastSection = static_cast<ServiceCatalogSection>(-1);
			for (size_t i = 0; i < indices.size(); )
			{
				const size_t index = indices[i];
				if (index >= m_serviceRoutes.size())
				{
					++i;
					continue;
				}
				const ServiceRouteEntry& service = m_serviceRoutes[index];
				if (showSubHeaders && service.section != lastSection)
				{
					ImGui::Dummy({ 0.f, 4.f });
					ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
					ImGui::TextUnformatted(VpnServiceRoutes::SectionLabel(service.section));
					ImGui::PopStyleColor();
					ImGui::Dummy({ 0.f, 2.f });
					lastSection = service.section;
				}
				if (drawServiceAt(index))
					return;
				++drawn;
				++i;
			}
		};

		// «Мною добавленное» — первая группа, только если есть записи
		if (!customIndices.empty())
		{
			char foldId[64];
			snprintf(foldId, sizeof foldId, "##%s_fold_custom", foldIdPrefix);
			drawFoldedSection(
				customSection,
				"Мною добавленное",
				foldId,
				customIndices,
				sectionFoldOpenDefault(customSection, true),
				false);
		}

		for (const SectionBucket& bucket : buckets)
		{
			char foldId[64];
			snprintf(
				foldId,
				sizeof foldId,
				"##%s_fold_%d",
				foldIdPrefix,
				static_cast<int>(bucket.section));
			drawFoldedSection(
				bucket.section,
				VpnServiceRoutes::SectionLabel(bucket.section),
				foldId,
				bucket.indices,
				sectionFoldOpen(bucket.section),
				false);
		}

		if (kind == ServiceCatalogKind::App)
		{
			char gamesId[48];
			snprintf(gamesId, sizeof gamesId, "##%s_games_fold", foldIdPrefix);
			drawFoldedSection(
				ServiceCatalogSection::ForeignGames,
				"Игры",
				gamesId,
				gameIndices,
				m_gamesExpanded,
				true);
		}

		// Вне категорий — в самом конце списка (например Windows).
		if (!standaloneIndices.empty())
		{
			ImGui::Dummy({ 0.f, firstSection ? 2.f : 10.f });
			firstSection = false;
			for (size_t index : standaloneIndices)
			{
				if (drawServiceAt(index))
					return drawn;
				++drawn;
			}
		}

		return drawn;
	};

	auto countKind = [&](ServiceCatalogKind kind) -> int
	{
		int n = 0;
		for (const ServiceRouteEntry& service : m_serviceRoutes)
		{
			if (service.kind != kind || !MatchesServiceSearch(service))
				continue;
			if (VpnServiceRoutes::IsAdultSection(service.section) && !showAdultCatalog)
				continue;
			++n;
		}
		return n;
	};

	auto drawCustomAddForm = [&](
		ServiceCatalogKind kind,
		char* nameBuf,
		size_t nameSize,
		char* targetsBuf,
		size_t targetsSize,
		const char* addId)
	{
		UiCommon::CaptionText(
			kind == ServiceCatalogKind::App
				? "Добавить вручную или выбрать из запущенных процессов"
				: "Добавить сайт (домены через запятую)",
			colors,
			width);

		const float gap = 8.f;
		const float addBtnW = 88.f;
		const float pickBtnW = kind == ServiceCatalogKind::App ? 128.f : 0.f;
		const float buttonsW = addBtnW + (pickBtnW > 0.f ? gap + pickBtnW : 0.f);
		const float fieldsAvail = (std::max)(120.f, width - buttonsW - gap);
		const float nameW = fieldsAvail * 0.38f;
		const float targetsW = (std::max)(80.f, fieldsAvail - nameW - gap);

		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(nameW);
		ImGui::InputTextWithHint(
			kind == ServiceCatalogKind::App ? "##custom_app_name" : "##custom_site_name",
			"Название",
			nameBuf,
			static_cast<int>(nameSize));
		ImGui::SameLine(0.f, gap);
		ImGui::SetNextItemWidth(targetsW);
		ImGui::InputTextWithHint(
			kind == ServiceCatalogKind::App ? "##custom_app_targets" : "##custom_site_targets",
			kind == ServiceCatalogKind::App ? "myapp.exe, helper.exe" : "example.com, cdn.example.com",
			targetsBuf,
			static_cast<int>(targetsSize));
		UiCommon::PopInputStyle();

		ImGui::SameLine(0.f, gap);
		const bool canAdd = nameBuf[0] != 0 && targetsBuf[0] != 0;
		if (!canAdd)
			ImGui::BeginDisabled();
		if (UiCommon::SecondaryButton(addId, { addBtnW, UiMetrics::kSmallBtnHeight }, colors) && canAdd)
		{
			if (TryAddCustomEntry(kind, nameBuf, targetsBuf))
			{
				nameBuf[0] = 0;
				targetsBuf[0] = 0;
			}
		}
		if (!canAdd)
			ImGui::EndDisabled();

		if (kind == ServiceCatalogKind::App)
		{
			ImGui::SameLine(0.f, gap);
			if (UiCommon::SecondaryButton(
				"Из процессов##pick_proc",
				{ pickBtnW, UiMetrics::kSmallBtnHeight },
				colors))
			{
				RefreshProcessPickerList();
				m_processPickerFilter[0] = 0;
				m_processPickerScrollY = 0.f;
				m_processPickerScrollDisplay = 0.f;
				m_processPickerScrollVel = 0.f;
				m_openProcessPicker = true;
			}
		}
		ImGui::Dummy({ 0.f, 8.f });
	};

	auto drawBlockHeader = [&](const char* title, uint32_t iconCode, int count)
	{
		const std::string glyph = IconUtf8(iconCode);
		ImFont* iconFont = fonts.GetIconFont();
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		if (iconFont && !glyph.empty())
		{
			ImGui::PushFont(iconFont);
			ImGui::TextUnformatted(glyph.c_str());
			ImGui::PopFont();
			ImGui::SameLine(0.f, 8.f);
		}
		char hdr[96];
		snprintf(hdr, sizeof hdr, "%s (%d)", title, count);
		ImGui::TextUnformatted(hdr);
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 6.f });
	};

	auto drawKindBlock = [&](
		ServiceCatalogKind kind,
		const char* title,
		uint32_t blockIcon,
		const char* foldPrefix,
		char* nameBuf,
		size_t nameSize,
		char* targetsBuf,
		size_t targetsSize,
		const char* addLabel) -> int
	{
		const int total = countKind(kind);
		if (searching && total == 0)
			return 0;

		drawBlockHeader(title, blockIcon, total);
		if (!searching)
			drawCustomAddForm(kind, nameBuf, nameSize, targetsBuf, targetsSize, addLabel);
		drawKindSections(kind, foldPrefix);
		return total;
	};

	const int appsDrawn = drawKindBlock(
		ServiceCatalogKind::App,
		"Приложения",
		0xE71D,
		"app",
		m_customAppName,
		sizeof m_customAppName,
		m_customAppTargets,
		sizeof m_customAppTargets,
		"Добавить##add_app");

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddLine(
			{ p.x, p.y + 1.f },
			{ p.x + width, p.y + 1.f },
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.tileBorder, 0.55f)),
			1.f);
		ImGui::Dummy({ width, 8.f });
	}

	const int sitesDrawn = drawKindBlock(
		ServiceCatalogKind::Site,
		"Сайты",
		0xE774,
		"site",
		m_customSiteName,
		sizeof m_customSiteName,
		m_customSiteTargets,
		sizeof m_customSiteTargets,
		"Добавить##add_site");

	if (searching && appsDrawn == 0 && sitesDrawn == 0)
	{
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText("Ничего не найдено.", colors, width);
	}
}

void UiRoutingPage::DrawAdvancedRules(
	FontManager& fonts,
	float width,
	const UiThemeColors& colors,
	const UiAccentColors& accents)
{
	const size_t totalRules = m_processRules.size() + m_domainRules.size();
	char header[96];
	snprintf(
		header,
		sizeof header,
		"Процессы и доменные правила (%zu)",
		totalRules);
	if (!DrawCollapsedSection("advanced_rules", header, colors))
		return;

	const float innerWidth = width;

	ImGui::Dummy({ 0.f, 6.f });
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Маршрутизация по процессам");
	ImGui::PopStyleColor();
	ImGui::Dummy({ 0.f, 4.f });

	const float processBtnW = 118.f;
	if (UiCommon::SecondaryButton("Добавить exe", { processBtnW, UiMetrics::kSmallBtnHeight }, colors))
	{
		ProcessRule rule {};
		snprintf(rule.label, sizeof rule.label, "new_app.exe");
		rule.action = 1;
		m_processRules.push_back(rule);
		ScheduleApply();
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("Добавить папку", { processBtnW, UiMetrics::kSmallBtnHeight }, colors))
		ScheduleApply();

	ImGui::Dummy({ 0.f, 6.f });
	UiCommon::PushTableStyle(colors);
	if (ImGui::BeginTable(
		"##process_rules",
		3,
		UiCommon::StretchableTableFlags(false),
		ImVec2(innerWidth, 0.f)))
	{
		ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, kColNum);
		ImGui::TableSetupColumn("Приложение / папка", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthStretch, 0.7f);
		UiCommon::TableHeadersRowCentered(colors);

		int rowNum = 0;
		for (size_t i = 0; i < m_processRules.size(); )
		{
			if (m_serviceSearch[0] && !MatchesTextSearch(m_processRules[i].label))
			{
				++i;
				continue;
			}

			++rowNum;
			ImGui::PushID(static_cast<int>(i));
			bool rowChanged = false;
			const RuleRowResult result = DrawRuleTableRow(
				rowNum,
				m_processRules[i].label,
				m_processRules[i].action,
				static_cast<int>(i),
				m_selectedProcess,
				fonts,
				colors,
				accents,
				rowChanged);
			if (rowChanged)
				ApplyRouting();
			if (result == RuleRowResult::Delete)
			{
				m_processRules.erase(m_processRules.begin() + static_cast<std::ptrdiff_t>(i));
				if (m_selectedProcess == static_cast<int>(i))
					m_selectedProcess = -1;
				else if (m_selectedProcess > static_cast<int>(i))
					--m_selectedProcess;
				ScheduleApply();
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++i;
		}
		ImGui::EndTable();
	}
	UiCommon::PopTableStyle();

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Доменные правила");
	ImGui::PopStyleColor();
	ImGui::Dummy({ 0.f, 4.f });

	const float domainBtnW = 100.f;
	if (UiCommon::SecondaryButton("Добавить", { domainBtnW, UiMetrics::kSmallBtnHeight }, colors))
	{
		VpnDomainRule rule {};
		rule.address = "example.com";
		rule.action = VpnDomainRuleAction::Proxy;
		m_domainRules.push_back(rule);
		ScheduleApply();
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("Импорт", { domainBtnW, UiMetrics::kSmallBtnHeight }, colors))
		ScheduleApply();
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("Экспорт", { domainBtnW, UiMetrics::kSmallBtnHeight }, colors))
		ScheduleApply();

	ImGui::Dummy({ 0.f, 6.f });
	UiCommon::PushTableStyle(colors);
	if (ImGui::BeginTable(
		"##domain_rules",
		3,
		UiCommon::StretchableTableFlags(false),
		ImVec2(innerWidth, 0.f)))
	{
		ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, kColNum);
		ImGui::TableSetupColumn("Адрес", ImGuiTableColumnFlags_WidthStretch, 1.6f);
		ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthStretch, 0.7f);
		UiCommon::TableHeadersRowCentered(colors);

		int rowNum = 0;
		for (size_t i = 0; i < m_domainRules.size(); )
		{
			if (m_serviceSearch[0] && !MatchesTextSearch(m_domainRules[i].address.c_str()))
			{
				++i;
				continue;
			}

			++rowNum;
			ImGui::PushID(static_cast<int>(i));
			bool rowChanged = false;
			int action = static_cast<int>(m_domainRules[i].action);
			const RuleRowResult result = DrawRuleTableRow(
				rowNum,
				m_domainRules[i].address.c_str(),
				action,
				static_cast<int>(i),
				m_selectedDomain,
				fonts,
				colors,
				accents,
				rowChanged);
			if (rowChanged)
			{
				m_domainRules[i].action = static_cast<VpnDomainRuleAction>(action);
				ApplyRouting();
			}
			if (result == RuleRowResult::Delete)
			{
				m_domainRules.erase(m_domainRules.begin() + static_cast<std::ptrdiff_t>(i));
				if (m_selectedDomain == static_cast<int>(i))
					m_selectedDomain = -1;
				else if (m_selectedDomain > static_cast<int>(i))
					--m_selectedDomain;
				ScheduleApply();
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++i;
		}
		ImGui::EndTable();
	}
	UiCommon::PopTableStyle();
}

void UiRoutingPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	EnsureLoaded();
	EnsureServiceRoutesLoaded();
	EnsureDomainRulesLoaded();

	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();
	const float deltaTime = ImGui::GetIO().DeltaTime;
	m_bypassLanMix = UiCommon::AnimateMix(m_bypassLanMix, m_bypassLan, deltaTime, 10.f);

	if (m_applySuccessTimer > 0.f)
		m_applySuccessTimer -= deltaTime;

	FlushApplyIfDue(deltaTime);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xf4d7,
		"Маршрутизация",
		nullptr,
		colors,
		UiCommon::TitleIconFont::Solid);

	if (m_applySuccessTimer > 0.f)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, accents.ok);
		ImGui::TextUnformatted("Настройки применены.");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });
	}

	if (UiCommon::BeginCard("##routing_settings", width, colors))
	{
		const float fieldW = ImGui::GetContentRegionAvail().x;
		constexpr float kLabelW = 72.f;

		if (DrawInlineFieldCombo("dns_mode", "DNS", m_dnsMode, kDnsModes, 2, fieldW, kLabelW, colors))
			ApplyRouting();

		DrawHorizontalSeparator(fieldW, colors);

		if (DrawInlineToggleRow("bypass_lan", "Обход локальной сети", m_bypassLan, m_bypassLanMix, fieldW, colors))
			ScheduleApply();
		UiCommon::CaptionText(
			"Локальные адреса (192.168.x.x, 10.x.x.x) и устройства в домашней сети идут напрямую, без VPN.",
			colors,
			fieldW);

		if (m_transportMode == 1)
		{
			ImGui::Dummy({ 0.f, 4.f });
			UiCommon::CaptionText("Bootstrap DNS (direct):", colors);
			if (DrawDnsPairRow(
				"bootstrap",
				m_bootstrapDns,
				m_bootstrapType,
				kBootstrapDns,
				5,
				kBootstrapType,
				4,
				fieldW,
				colors))
			{
				ApplyRouting();
			}

			UiCommon::CaptionText("Proxy DNS (VPN):", colors);
			if (DrawDnsPairRow(
				"proxy",
				m_proxyDns,
				m_proxyType,
				kProxyDns,
				4,
				kProxyType,
				3,
				fieldW,
				colors))
			{
				ApplyRouting();
			}
		}
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	// Процессы и доменные правила временно скрыты в UI.
	// UiCommon::CaptionText(
	// 	"Приоритет применения: приложения и папки выше сервисов, сервисы выше доменных правил.",
	// 	colors,
	// 	width);
	// ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
	// DrawHorizontalSeparator(width, colors);

	if (UiCommon::BeginCard("##routing_services", width, colors))
	{
		const float cardInner = ImGui::GetContentRegionAvail().x;
		UiCommon::SectionHeader("Маршрутизация по каталогу", colors);
		ImGui::Dummy({ 0.f, 2.f });
		UiCommon::CaptionText(
			"Два блока: приложения (процессы) и сайты (домены). Внутри — только группы.",
			colors,
			cardInner);
		ImGui::Dummy({ 0.f, 6.f });
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(cardInner);
		ImGui::InputTextWithHint(
			"##service_search",
			"Поиск в приложениях и сайтах...",
			m_serviceSearch,
			sizeof m_serviceSearch);
		UiCommon::PopInputStyle();
		ImGui::Dummy({ 0.f, 8.f });
		DrawServiceRoutes(fonts, cardInner, colors, accents);
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	// DrawAdvancedRules(fonts, width, colors, accents);
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();

	DrawProcessPickerModal(fonts, colors, accents);
	DrawDuplicateWarningModal(colors, accents);
}

void UiRoutingPage::RefreshProcessPickerList()
{
	m_processPickerList.clear();
	std::unordered_set<std::string> seen;

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe))
	{
		do
		{
			if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4)
				continue;

			char utf8[MAX_PATH] = {};
			WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, utf8, MAX_PATH, nullptr, nullptr);
			if (!utf8[0])
				continue;

			std::string name = utf8;
			std::string key = name;
			for (char& ch : key)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

			if (key == "system" || key == "idle" || key == "registry" || key == "smss.exe"
				|| key == "csrss.exe" || key == "wininit.exe" || key == "services.exe"
				|| key == "lsass.exe" || key == "svchost.exe" || key == "fontdrvhost.exe"
				|| key == "dwm.exe" || key == "conhost.exe" || key == "antizapret.exe"
				|| key == "antizapret_new.exe")
			{
				continue;
			}

			if (!seen.insert(key).second)
				continue;
			m_processPickerList.push_back(std::move(name));
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);

	std::sort(
		m_processPickerList.begin(),
		m_processPickerList.end(),
		[](const std::string& a, const std::string& b) {
			std::string la = a;
			std::string lb = b;
			for (char& ch : la)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			for (char& ch : lb)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			return la < lb;
		});
}

bool UiRoutingPage::IsProcessAlreadyCovered(const std::string& exeName) const
{
	return FindCoveringApp(exeName) != nullptr;
}

const ServiceRouteEntry* UiRoutingPage::FindCoveringApp(const std::string& exeOrName) const
{
	if (exeOrName.empty())
		return nullptr;

	auto toLower = [](std::string value) {
		for (char& ch : value)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return value;
	};
	auto containsWord = [](const std::string& hay, const std::string& needle) -> bool {
		if (needle.empty() || hay.empty())
			return false;
		size_t pos = 0;
		while ((pos = hay.find(needle, pos)) != std::string::npos)
		{
			const bool leftOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(hay[pos - 1]));
			const size_t end = pos + needle.size();
			const bool rightOk = end >= hay.size() || !std::isalnum(static_cast<unsigned char>(hay[end]));
			if (leftOk && rightOk)
				return true;
			pos = end;
		}
		return false;
	};

	const std::string exe = toLower(exeOrName);
	std::string base = exe;
	if (base.size() > 4 && base.compare(base.size() - 4, 4, ".exe") == 0)
		base.resize(base.size() - 4);

	for (const ServiceRouteEntry& entry : m_serviceRoutes)
	{
		if (entry.kind != ServiceCatalogKind::App)
			continue;

		const std::string name = toLower(entry.name);
		const std::string desc = toLower(entry.description);
		const std::string id = toLower(entry.id);

		if (exe == desc || desc.find(exe) != std::string::npos)
			return &entry;
		if (!base.empty()
			&& (base == id || base == name || containsWord(name, base) || containsWord(desc, base)
				|| containsWord(id, base)))
		{
			return &entry;
		}

		std::vector<std::string> domains;
		std::vector<std::string> processes;
		VpnServiceRoutes::CollectRouteTargets(entry, domains, processes);
		for (const std::string& processName : processes)
		{
			if (toLower(processName) == exe)
				return &entry;
		}
	}
	return nullptr;
}

const ServiceRouteEntry* UiRoutingPage::FindCoveringSite(const std::string& domainOrName) const
{
	if (domainOrName.empty())
		return nullptr;

	auto toLower = [](std::string value) {
		for (char& ch : value)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return value;
	};
	auto stripWww = [](std::string value) {
		if (value.rfind("www.", 0) == 0)
			value.erase(0, 4);
		return value;
	};
	auto domainMatch = [&](const std::string& aRaw, const std::string& bRaw) -> bool {
		std::string a = stripWww(toLower(aRaw));
		std::string b = stripWww(toLower(bRaw));
		if (a.empty() || b.empty())
			return false;
		if (a == b)
			return true;
		if (a.size() > b.size() && a.compare(a.size() - b.size() - 1, 1, ".") == 0
			&& a.compare(a.size() - b.size(), b.size(), b) == 0)
		{
			return true;
		}
		if (b.size() > a.size() && b.compare(b.size() - a.size() - 1, 1, ".") == 0
			&& b.compare(b.size() - a.size(), a.size(), a) == 0)
		{
			return true;
		}
		return false;
	};

	const std::string needle = toLower(domainOrName);
	const std::string needleBase = stripWww(needle);

	for (const ServiceRouteEntry& entry : m_serviceRoutes)
	{
		if (entry.kind != ServiceCatalogKind::Site)
			continue;

		const std::string name = toLower(entry.name);
		const std::string id = toLower(entry.id);
		if (needleBase == name || needleBase == id || needle == name || needle == id)
			return &entry;

		std::vector<std::string> domains;
		std::vector<std::string> processes;
		VpnServiceRoutes::CollectRouteTargets(entry, domains, processes);
		for (const std::string& domain : domains)
		{
			if (domainMatch(domain, needle))
				return &entry;
		}

		// Fallback: description may list domains without structured targets for catalogue items.
		std::string desc = toLower(entry.description);
		size_t start = 0;
		while (start <= desc.size())
		{
			const size_t comma = desc.find(',', start);
			std::string token = desc.substr(
				start,
				comma == std::string::npos ? std::string::npos : comma - start);
			while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
				token.erase(token.begin());
			while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
				token.pop_back();
			if (!token.empty() && domainMatch(token, needle))
				return &entry;
			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
	}
	return nullptr;
}

void UiRoutingPage::ShowDuplicateWarning(const std::string& attempted, const std::string& existing)
{
	m_duplicateWarningAttempt = attempted;
	m_duplicateWarningExisting = existing;
	m_showDuplicateWarning = true;
}

bool UiRoutingPage::TryAddCustomEntry(
	ServiceCatalogKind kind,
	const std::string& name,
	const std::string& targets)
{
	EnsureServiceRoutesLoaded();

	auto trimToken = [](std::string token) {
		while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
			token.erase(token.begin());
		while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
			token.pop_back();
		return token;
	};

	size_t start = 0;
	while (start <= targets.size())
	{
		const size_t comma = targets.find(',', start);
		const std::string token = trimToken(targets.substr(
			start,
			comma == std::string::npos ? std::string::npos : comma - start));
		if (!token.empty())
		{
			const ServiceRouteEntry* existing = kind == ServiceCatalogKind::App
				? FindCoveringApp(token)
				: FindCoveringSite(token);
			if (existing)
			{
				ShowDuplicateWarning(
					!name.empty() ? name + " (" + token + ")" : token,
					existing->name.empty() ? existing->id : existing->name);
				return false;
			}
		}
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}

	if (!name.empty())
	{
		const ServiceRouteEntry* existing = kind == ServiceCatalogKind::App
			? FindCoveringApp(name)
			: FindCoveringSite(name);
		if (existing)
		{
			ShowDuplicateWarning(name, existing->name.empty() ? existing->id : existing->name);
			return false;
		}
	}

	ServiceRouteEntry entry = VpnServiceRoutes::MakeCustomEntry(kind, name, targets);
	m_serviceRoutes.push_back(std::move(entry));
	m_serviceMix.push_back(1.f);
	ApplyRouting();
	return true;
}

void UiRoutingPage::AddCustomAppFromProcess(const std::string& exeName)
{
	if (exeName.empty())
		return;

	if (const ServiceRouteEntry* existing = FindCoveringApp(exeName))
	{
		ShowDuplicateWarning(exeName, existing->name.empty() ? existing->id : existing->name);
		return;
	}

	EnsureServiceRoutesLoaded();

	std::string displayName = exeName;
	if (displayName.size() > 4)
	{
		std::string lower = displayName;
		for (char& ch : lower)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		if (lower.compare(lower.size() - 4, 4, ".exe") == 0)
			displayName.resize(displayName.size() - 4);
	}

	ServiceRouteEntry entry = VpnServiceRoutes::MakeCustomEntry(
		ServiceCatalogKind::App,
		displayName,
		exeName);
	m_serviceRoutes.push_back(std::move(entry));
	m_serviceMix.push_back(1.f);
	ApplyRouting();
}

void UiRoutingPage::DrawProcessPickerModal(
	FontManager& fonts,
	const UiThemeColors& colors,
	const UiAccentColors& accents)
{
	(void)fonts;

	if (m_openProcessPicker)
		ImGui::OpenPopup("##routing_process_picker");

	const bool light = UiCommon::IsLightTheme(colors);
	const ImVec4 popupBg = light
		? ImVec4(0.90f, 0.90f, 0.92f, 0.98f)
		: UiCommon::WithAlpha(colors.tileBg, 0.98f);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
	ImGui::PushStyleColor(ImGuiCol_Border, UiCommon::WithAlpha(colors.tileBorder, light ? 0.55f : 0.40f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, popupBg);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, popupBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 18.f, 16.f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 10.f, 8.f });

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	const float modalW = (std::min)(680.f, vp->WorkSize.x * 0.92f);
	const float modalH = (std::min)(520.f, vp->WorkSize.y * 0.88f);
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize({ modalW, modalH }, ImGuiCond_Always);

	if (ImGui::BeginPopupModal(
			"##routing_process_picker",
			nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
	{
		m_openProcessPicker = false;

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Выбор процесса");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 2.f });
		UiCommon::CaptionText(
			"Серым — уже есть в каталоге (по клику покажем предупреждение). Остальные добавляются в «Мною добавленное».",
			colors,
			ImGui::GetContentRegionAvail().x);

		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 108.f);
		ImGui::InputTextWithHint(
			"##proc_filter",
			"Фильтр...",
			m_processPickerFilter,
			sizeof m_processPickerFilter);
		UiCommon::PopInputStyle();
		ImGui::SameLine(0.f, 8.f);
		if (UiCommon::SecondaryButton("Обновить", { 100.f, UiMetrics::kSmallBtnHeight }, colors))
			RefreshProcessPickerList();

		ImGui::Dummy({ 0.f, 4.f });

		const float listH = (std::max)(120.f, ImGui::GetContentRegionAvail().y - (UiMetrics::kSmallBtnHeight + 14.f));
		ImGui::PushStyleColor(ImGuiCol_ChildBg, UiCommon::WithAlpha(colors.navActive, light ? 0.55f : 0.35f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::BeginChild(
			"##proc_grid",
			{ 0.f, listH },
			ImGuiChildFlags_Borders,
			ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

		ImGuiWindow* procWindow = ImGui::GetCurrentWindow();
		ImGuiIO& io = ImGui::GetIO();
		float wheelCaptured = 0.f;
		if (ImGui::IsWindowHovered())
		{
			wheelCaptured = io.MouseWheel;
			io.MouseWheel = 0.f;
			io.MouseWheelH = 0.f;
		}

		const float deltaTime = (std::max)(io.DeltaTime, 0.0001f);
		const float maxScroll = procWindow ? procWindow->ScrollMax.y : 0.f;
		const ImGuiID scrollBarId = procWindow
			? ImGui::GetWindowScrollbarID(procWindow, ImGuiAxis_Y)
			: 0;
		const bool scrollbarActive = scrollBarId != 0
			&& (GImGui->ActiveId == scrollBarId || GImGui->ActiveIdPreviousFrame == scrollBarId);

		if (scrollbarActive && procWindow)
		{
			m_processPickerScrollY = procWindow->Scroll.y;
			m_processPickerScrollDisplay = procWindow->Scroll.y;
			m_processPickerScrollVel = 0.f;
		}
		else
		{
			if (wheelCaptured != 0.f)
				m_processPickerScrollVel -= wheelCaptured * 220.f;

			if (std::fabs(m_processPickerScrollVel) > 0.5f)
			{
				m_processPickerScrollY += m_processPickerScrollVel * deltaTime;
				m_processPickerScrollVel *= expf(-deltaTime * 7.f);
			}
			else
			{
				m_processPickerScrollVel = 0.f;
			}

			m_processPickerScrollY = (std::max)(0.f, (std::min)(m_processPickerScrollY, maxScroll));
			if (m_processPickerScrollY <= 0.f || m_processPickerScrollY >= maxScroll)
				m_processPickerScrollVel = 0.f;

			const float smoothK = 1.f - expf(-deltaTime * 14.f);
			m_processPickerScrollDisplay += (m_processPickerScrollY - m_processPickerScrollDisplay) * smoothK;
			if (std::fabs(m_processPickerScrollY - m_processPickerScrollDisplay) < 0.25f)
				m_processPickerScrollDisplay = m_processPickerScrollY;
			m_processPickerScrollDisplay = (std::max)(0.f, (std::min)(m_processPickerScrollDisplay, maxScroll));
		}

		ImGui::SetScrollY(m_processPickerScrollDisplay);

		constexpr int kCols = 3;
		auto matchesFilter = [&](const std::string& name) -> bool {
			if (!m_processPickerFilter[0])
				return true;
			std::string hay = name;
			std::string needle = m_processPickerFilter;
			for (char& ch : hay)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			for (char& ch : needle)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			return hay.find(needle) != std::string::npos;
		};

		int visible = 0;
		if (ImGui::BeginTable(
				"##proc_table",
				kCols,
				ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX))
		{
			for (int c = 0; c < kCols; ++c)
				ImGui::TableSetupColumn("c", ImGuiTableColumnFlags_WidthStretch);

			int col = 0;
			for (size_t i = 0; i < m_processPickerList.size(); ++i)
			{
				const std::string& name = m_processPickerList[i];
				if (!matchesFilter(name))
					continue;

				if (col == 0)
					ImGui::TableNextRow(ImGuiTableRowFlags_None, 40.f);
				ImGui::TableSetColumnIndex(col);
				++visible;

				const bool covered = IsProcessAlreadyCovered(name);
				ImGui::PushID(static_cast<int>(i));
				ImGui::PushStyleColor(
					ImGuiCol_Button,
					covered ? UiCommon::WithAlpha(colors.tileBg, 0.45f) : colors.tileBg);
				ImGui::PushStyleColor(
					ImGuiCol_ButtonHovered,
					covered ? UiCommon::WithAlpha(colors.navHover, 0.55f) : colors.navHover);
				ImGui::PushStyleColor(
					ImGuiCol_ButtonActive,
					covered ? UiCommon::WithAlpha(colors.navActive, 0.55f) : colors.navActive);
				ImGui::PushStyleColor(
					ImGuiCol_Text,
					covered ? colors.textMuted : colors.textPrimary);
				if (ImGui::Button(name.c_str(), { -1.f, 34.f }))
				{
					AddCustomAppFromProcess(name);
					ImGui::CloseCurrentPopup();
				}
				if (covered)
					UiCommon::SetItemTooltip("Уже есть в каталоге — нажмите, чтобы увидеть предупреждение");
				ImGui::PopStyleColor(4);
				ImGui::PopID();

				col = (col + 1) % kCols;
			}
			ImGui::EndTable();
		}

		if (visible == 0)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted(
				m_processPickerList.empty()
					? "Список процессов пуст."
					: "Ничего не найдено по фильтру.");
			ImGui::PopStyleColor();
		}

		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		ImGui::Dummy({ 0.f, 6.f });
		const float closeW = 100.f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - closeW);
		if (UiCommon::SecondaryButton("Закрыть", { closeW, UiMetrics::kSmallBtnHeight }, colors))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(4);
	(void)accents;
}

void UiRoutingPage::DrawDuplicateWarningModal(
	const UiThemeColors& colors,
	const UiAccentColors& accents)
{
	if (m_showDuplicateWarning)
		ImGui::OpenPopup("##routing_duplicate_warn");

	const bool light = UiCommon::IsLightTheme(colors);
	const ImVec4 popupBg = light
		? ImVec4(0.90f, 0.90f, 0.92f, 0.98f)
		: UiCommon::WithAlpha(colors.tileBg, 0.98f);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
	ImGui::PushStyleColor(ImGuiCol_Border, UiCommon::WithAlpha(colors.tileBorder, light ? 0.55f : 0.40f));
	ImGui::PushStyleColor(ImGuiCol_TitleBg, popupBg);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, popupBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 18.f, 16.f });
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 10.f, 8.f });

	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize({ 420.f, 0.f }, ImGuiCond_Appearing);

	if (ImGui::BeginPopupModal(
			"##routing_duplicate_warn",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
	{
		m_showDuplicateWarning = false;

		ImGui::PushStyleColor(ImGuiCol_Text, accents.warn);
		ImGui::TextUnformatted("Уже есть в списках");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 6.f });

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextWrapped(
			"Вы пытаетесь добавить «%s», но это уже есть в каталоге как «%s».",
			m_duplicateWarningAttempt.c_str(),
			m_duplicateWarningExisting.c_str());
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::CaptionText(
			"Повторно добавлять не нужно — используйте существующую запись.",
			colors,
			ImGui::GetContentRegionAvail().x);

		ImGui::Dummy({ 0.f, 10.f });
		const float btnW = 110.f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - btnW);
		if (UiCommon::AccentButton("Понятно", { btnW, UiMetrics::kSmallBtnHeight }, accents.warn, colors)
			|| ImGui::IsKeyPressed(ImGuiKey_Escape)
			|| ImGui::IsKeyPressed(ImGuiKey_Enter))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(4);
}
