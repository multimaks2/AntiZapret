#include "ui/ui_routing_page.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_domain_routes.h"
#include "vpn/vpn_manager.h"
#include "imgui.h"

#include <Windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

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
		const UiThemeColors& colors)
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

		// Segoe MDL2: ChevronDown / ChevronRight (Unicode ▾/▸ отсутствуют в шрифте UI)
		constexpr uint32_t kChevronDown = 0xE70D;
		constexpr uint32_t kChevronRight = 0xE76C;
		const std::string chevron = IconUtf8(expanded ? kChevronDown : kChevronRight);
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
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Удалить");
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
	};

	std::string ResolveServiceOpenUrl(const std::string& serviceId)
	{
		std::vector<std::string> domains;
		VpnServiceRoutes::CollectFallbackDomains(serviceId, domains);
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
		const char* title,
		const char* description,
		bool& enabled,
		int& mode,
		float width,
		const UiThemeColors& colors,
		float& toggleMix,
		const char* openUrl)
	{
		const float rowH = 58.f;
		const float iconArea = 36.f;
		const float leftPad = 12.f;
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::Dummy({ width, rowH });

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 rowMax = { pos.x + width, pos.y + rowH };
		drawList->AddRectFilled(pos, rowMax, ImGui::GetColorU32(colors.navActive), UiMetrics::kCardRadius);
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
				ImGui::SetTooltip("Открыть сайт\n%s", openUrl);
			}
			if (ImGui::IsItemClicked())
				OpenUrlInDefaultBrowser(openUrl);
		}

		const std::string glyph = IconUtf8(iconCode);
		ImFont* iconFont = fonts.GetIconFont();
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
		const float rightPad = 12.f;
		const float controlsY = pos.y + (rowH - UiMetrics::kSmallBtnHeight) * 0.5f;

		ImGui::PushID(scope);
		ImGui::PushID(serviceId);
		ImGui::PushID("controls");
		ImGui::SetCursorScreenPos(ImVec2(pos.x + width - rightPad - toggleW, pos.y + (rowH - 22.f) * 0.5f));
		toggleMix = UiCommon::AnimateMix(toggleMix, enabled, ImGui::GetIO().DeltaTime, 10.f);
		ServiceRowChange change = ServiceRowChange::None;
		if (DrawMiniToggle("##sw", toggleMix, colors))
		{
			enabled = !enabled;
			change = ServiceRowChange::Toggle;
		}

		ImGui::SetCursorScreenPos(ImVec2(
			pos.x + width - rightPad - toggleW - 6.f - toggleLabelW,
			pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Text, enabled ? colors.textPrimary : colors.textMuted);
		ImGui::TextUnformatted(enabled ? "Вкл" : "Выкл");
		ImGui::PopStyleColor();

		ImGui::SetCursorScreenPos(ImVec2(pos.x + width - rightPad - toggleW - toggleLabelW - 12.f - comboW, controlsY));
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(comboW);
		if (!enabled)
			ImGui::BeginDisabled();
		if (ImGui::Combo("##mode", &mode, kServiceRouteModes, 2))
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

	auto toLower = [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	};

	const char* hay = text;
	for (; *hay; ++hay)
	{
		const char* h = hay;
		const char* n = m_serviceSearch;
		while (*h && *n && toLower(static_cast<unsigned char>(*h)) == toLower(static_cast<unsigned char>(*n)))
		{
			++h;
			++n;
		}
		if (!*n)
			return true;
	}
	return false;
}

bool UiRoutingPage::MatchesServiceSearch(const ServiceRouteEntry& service) const
{
	if (!m_serviceSearch[0])
		return true;
	return MatchesTextSearch(service.name.c_str())
		|| MatchesTextSearch(service.description.c_str())
		|| MatchesTextSearch(service.id.c_str());
}

void UiRoutingPage::DrawServiceRoutes(FontManager& fonts, float width, const UiThemeColors& colors)
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
		case ServiceCatalogSection::ForeignMisc: return "Показать прочее";
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
		case ServiceCatalogSection::RussianMisc: return "Показать прочее";
		default: return "Показать список";
		}
	};

	auto foldCaptionHide = [](ServiceCatalogSection section) -> const char*
	{
		switch (section)
		{
		case ServiceCatalogSection::ForeignGames: return "Скрыть онлайн-игры и новинки Steam";
		case ServiceCatalogSection::ForeignAdult: return "Скрыть список 18+ сайтов";
		default: return "Скрыть список";
		}
	};

	auto drawServiceAt = [&](size_t index)
	{
		ServiceRouteEntry& service = m_serviceRoutes[index];
		int uiMode = ServiceModeToUi(service.mode);
		const std::string openUrl = IsGameSection(service.section)
			? std::string()
			: ResolveServiceOpenUrl(service.id);
		const ServiceRowChange change = DrawServiceRow(
			fonts,
			"services",
			service.id.c_str(),
			service.icon,
			service.name.c_str(),
			service.description.c_str(),
			service.enabled,
			uiMode,
			width,
			colors,
			m_serviceMix[index],
			openUrl.empty() ? nullptr : openUrl.c_str());
		if (change == ServiceRowChange::Mode)
		{
			service.mode = UiToServiceMode(uiMode);
			ApplyRouting();
		}
		else if (change == ServiceRowChange::Toggle)
		{
			ScheduleApply();
		}
	};

	auto drawRegion = [&](ServiceCatalogRegion region) -> int
	{
		struct SectionBucket
		{
			ServiceCatalogSection section = ServiceCatalogSection::ForeignTools;
			std::vector<size_t> indices;
		};
		std::vector<SectionBucket> buckets;
		std::vector<size_t> gameIndices;

		for (size_t i = 0; i < m_serviceRoutes.size(); ++i)
		{
			if (i >= m_serviceMix.size())
				m_serviceMix.resize(m_serviceRoutes.size(), 1.f);

			const ServiceRouteEntry& service = m_serviceRoutes[i];
			if (service.region != region || !MatchesServiceSearch(service))
				continue;

			if (VpnServiceRoutes::IsAdultSection(service.section) && !showAdultCatalog)
				continue;

			if (region == ServiceCatalogRegion::Foreign && IsGameSection(service.section))
			{
				gameIndices.push_back(i);
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
			bool showSubHeaders)
		{
			if (indices.empty())
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
					colors);
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
			for (size_t index : indices)
			{
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
				drawServiceAt(index);
				++drawn;
			}
		};

		for (const SectionBucket& bucket : buckets)
		{
			char foldId[48];
			snprintf(foldId, sizeof foldId, "##fold_%d", static_cast<int>(bucket.section));
			drawFoldedSection(
				bucket.section,
				VpnServiceRoutes::SectionLabel(bucket.section),
				foldId,
				bucket.indices,
				sectionFoldOpen(bucket.section),
				false);
		}

		drawFoldedSection(
			ServiceCatalogSection::ForeignGames,
			"Игры",
			"##games_fold",
			gameIndices,
			m_gamesExpanded,
			true);

		return drawn;
	};

	int russianVisible = 0;
	for (const ServiceRouteEntry& service : m_serviceRoutes)
	{
		if (service.region != ServiceCatalogRegion::Russian)
			continue;
		if (MatchesServiceSearch(service))
			++russianVisible;
	}

	const int foreignDrawn = drawRegion(ServiceCatalogRegion::Foreign);

	ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
	if (!searching || russianVisible > 0)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Российские сервисы");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 4.f });
		drawRegion(ServiceCatalogRegion::Russian);
	}

	if (searching && foreignDrawn == 0 && russianVisible == 0)
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
		0xE945,
		"Маршрутизация",
		nullptr,
		colors);

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
		UiCommon::SectionHeader("Сервисы и приложения", colors);
		ImGui::Dummy({ 0.f, 4.f });
		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(cardInner);
		ImGui::InputTextWithHint(
			"##service_search",
			"Поиск сервисов и приложений...",
			m_serviceSearch,
			sizeof m_serviceSearch);
		UiCommon::PopInputStyle();
		ImGui::Dummy({ 0.f, 6.f });
		DrawServiceRoutes(fonts, cardInner, colors);
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	// DrawAdvancedRules(fonts, width, colors, accents);
	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}
