#include "ui/ui_console_page.h"

#include "app/app_log.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "imgui.h"

#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace
{
	LogFilter FilterForSidebarTab(UiTab tab)
	{
		switch (tab)
		{
		case UiTab::AntiZapret: return LogFilter::Zapret;
		case UiTab::Vpn:
		case UiTab::Routing: return LogFilter::VpnRouting;
		case UiTab::TgWsProxy: return LogFilter::Telegram;
		default: return LogFilter::All;
		}
	}

	uint32_t SourceTabIcon(LogSource source)
	{
		switch (source)
		{
		case LogSource::Zapret: return 0xE774;
		case LogSource::Telegram: return 0xE8BD;
		case LogSource::VpnRouting: return 0xE705;
		}
		return 0xE756;
	}

	const char* SourceTag(LogSource source)
	{
		switch (source)
		{
		case LogSource::Zapret: return "[AZ]";
		case LogSource::Telegram: return "[TG]";
		case LogSource::VpnRouting: return "[VPN]";
		}
		return "[?]";
	}

	std::string IconUtf8(uint32_t codepoint)
	{
		wchar_t wide[] = { static_cast<wchar_t>(codepoint), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof utf8), nullptr, nullptr);
		if (len <= 0)
			return {};
		return std::string(utf8, static_cast<size_t>(len));
	}

	std::string FormatTime(const std::chrono::system_clock::time_point& time)
	{
		const std::time_t tt = std::chrono::system_clock::to_time_t(time);
		std::tm localTime = {};
		localtime_s(&localTime, &tt);
		char buffer[16] = {};
		snprintf(buffer, sizeof buffer, "%02d:%02d:%02d", localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
		return buffer;
	}

	std::string FormatLogLine(const LogEntry& entry)
	{
		return IconUtf8(SourceTabIcon(entry.source)) + "  "
			+ SourceTag(entry.source) + "  "
			+ FormatTime(entry.time) + "  " + entry.text;
	}

	bool DrawFilterButton(const char* label, bool selected, const UiThemeColors& colors)
	{
		const ImVec2 size = { ImGui::CalcTextSize(label).x + 24.f, UiMetrics::kSmallBtnHeight };
		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, colors.navActive);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.navHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.navActive);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
			const bool pressed = ImGui::Button(label, size);
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(4);
			return pressed;
		}
		return UiCommon::SecondaryButton(label, size, colors);
	}
}

void UiConsolePage::SetFilterFromTab(UiTab tab)
{
	m_filter = FilterForSidebarTab(tab);
}

void UiConsolePage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xE756,
		"Консоль",
		"Логи Антизапрета, TG WS Proxy, VPN и маршрутизации",
		colors);

	struct FilterOption
	{
		LogFilter filter;
		const char* label;
	};

	static const FilterOption kFilters[] = {
		{ LogFilter::All, "Все" },
		{ LogFilter::Zapret, "Антизапрет" },
		{ LogFilter::Telegram, "TG WS Proxy" },
		{ LogFilter::VpnRouting, "VPN и маршрутизация" },
	};

	for (size_t i = 0; i < std::size(kFilters); ++i)
	{
		if (i > 0)
			ImGui::SameLine(0.f, UiMetrics::kGridGap);
		ImGui::PushID(static_cast<int>(kFilters[i].filter));
		if (DrawFilterButton(kFilters[i].label, m_filter == kFilters[i].filter, colors))
			m_filter = kFilters[i].filter;
		ImGui::PopID();
	}

	ImGui::SameLine(0.f, UiMetrics::kGridGap * 2.f);
	if (UiCommon::SecondaryButton("Очистить", { 92.f, UiMetrics::kSmallBtnHeight }, colors))
		AppLog::Instance().Clear(m_filter);

	ImGui::Dummy({ 0.f, UiMetrics::kSectionGap });

	const float logHeight = ImGui::GetContentRegionAvail().y - UiMetrics::kCardGap;
	if (UiCommon::BeginCard("##console_log", width, colors))
	{
		const std::vector<LogEntry> entries = AppLog::Instance().Snapshot(m_filter);

		if (entries.empty())
		{
			UiCommon::CaptionText(
				"Логов пока нет. Сообщения появятся при запуске сервисов и изменении настроек.",
				colors,
				ImGui::GetContentRegionAvail().x);
		}
		else
		{
			std::string body;
			body.reserve(entries.size() * 64);
			for (const LogEntry& entry : entries)
			{
				body += FormatLogLine(entry);
				body += '\n';
			}

			std::vector<char> buffer(body.begin(), body.end());
			buffer.push_back('\0');

			const bool light = UiCommon::IsLightTheme(colors);
			const ImVec4 consoleBg = light
				? ImVec4(0.92f, 0.92f, 0.94f, 1.f)
				: ImVec4(colors.inputBg.x, colors.inputBg.y, colors.inputBg.z, 1.f);
			const ImVec4 consoleText = colors.textPrimary;
			ImGui::PushStyleColor(ImGuiCol_FrameBg, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_Text, consoleText);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.f, 8.f });
			const float innerWidth = ImGui::GetContentRegionAvail().x;
			const float innerHeight = logHeight > 120.f ? logHeight - 32.f : 120.f;
			ImGui::InputTextMultiline(
				"##console_output",
				buffer.data(),
				buffer.size(),
				{ innerWidth, innerHeight },
				ImGuiInputTextFlags_ReadOnly);
			if (m_autoScroll && ImGui::IsItemVisible())
			{
				const float scrollMax = ImGui::GetScrollMaxY();
				if (scrollMax > 0.f)
					ImGui::SetScrollY(scrollMax);
			}
			if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.f)
				m_autoScroll = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.f;
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}
