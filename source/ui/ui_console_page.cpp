#include "ui/ui_console_page.h"

#include "app/app_log.h"
#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
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
		case LogSource::Zapret: return 0xf3ed;     // FA solid: shield-halved
		case LogSource::Telegram: return 0xf2c6;   // FA brands: telegram
		case LogSource::VpnRouting: return 0xf0ac; // FA solid: globe
		}
		return 0xf120; // FA solid: terminal
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
		std::string action = entry.action;
		std::string result = entry.result;
		if (action.empty())
			AppLog::ParseActionResult(entry.text, action, result);
		if (action.empty())
			action = entry.text;

		std::string line = IconUtf8(SourceTabIcon(entry.source));
		if (!line.empty())
			line += " ";
		line += SourceTag(entry.source);
		line += " [";
		line += FormatTime(entry.time);
		line += "] [";
		line += action;
		line += "]";
		if (!result.empty())
		{
			line += " : ";
			line += result;
		}
		return line;
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

void UiConsolePage::RebuildDisplayBuffer(const std::vector<LogEntry>& entries)
{
	std::string text;
	text.reserve(entries.size() * 96);
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (i > 0)
			text += '\n';
		text += FormatLogLine(entries[i]);
	}

	m_displayBuf.assign(text.begin(), text.end());
	m_displayBuf.push_back('\0');
	m_cachedCount = entries.size();
	m_cachedFilter = m_filter;
	m_cachedTail = entries.empty() ? std::string() : entries.back().text;
}

void UiConsolePage::ApplyLogInertiaScroll(ImGuiWindow* logWindow, float wheelCaptured, float wheelMultiplier)
{
	if (!logWindow)
		return;

	const float deltaTime = ImGui::GetIO().DeltaTime;
	const float maxScroll = logWindow->ScrollMax.y;

	if (!m_logScrollReady)
	{
		m_logScrollY = logWindow->Scroll.y;
		m_logScrollDisplay = logWindow->Scroll.y;
		m_logScrollVelocity = 0.f;
		m_logScrollReady = true;
	}

	if (wheelCaptured != 0.f)
	{
		// Ignore ImGui's NewFrame instant jump — keep our own scroll and add inertia.
		m_autoScroll = false;
		m_logScrollVelocity -= wheelCaptured * 220.f * wheelMultiplier;
	}

	if (m_autoScroll && wheelCaptured == 0.f && std::fabs(m_logScrollVelocity) < 0.5f)
	{
		m_logScrollY = maxScroll;
		m_logScrollDisplay = maxScroll;
		m_logScrollVelocity = 0.f;
		ImGui::SetScrollY(logWindow, maxScroll);
		return;
	}

	if (std::fabs(m_logScrollVelocity) > 0.5f)
	{
		m_logScrollY += m_logScrollVelocity * deltaTime;
		m_logScrollVelocity *= expf(-deltaTime * 7.f);
	}

	m_logScrollY = (std::max)(0.f, (std::min)(m_logScrollY, maxScroll));
	if (m_logScrollY <= 0.f || m_logScrollY >= maxScroll)
		m_logScrollVelocity = 0.f;

	const float smoothK = 1.f - expf(-deltaTime * 14.f);
	m_logScrollDisplay += (m_logScrollY - m_logScrollDisplay) * smoothK;
	if (std::fabs(m_logScrollY - m_logScrollDisplay) < 0.25f)
		m_logScrollDisplay = m_logScrollY;
	m_logScrollDisplay = (std::max)(0.f, (std::min)(m_logScrollDisplay, maxScroll));

	// Must run before InputTextMultiline draws (it snapshots Scroll.y at begin).
	ImGui::SetScrollY(logWindow, m_logScrollDisplay);

	if (maxScroll > 0.f
		&& m_logScrollY >= maxScroll - 1.f
		&& std::fabs(m_logScrollVelocity) < 0.5f)
		m_autoScroll = true;
}

void UiConsolePage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	(void)fonts;
	const UiThemeColors colors = theme.GetColors();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	const float contentStartY = ImGui::GetCursorPosY();

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

	// Fill the page viewport — do not use GetContentRegionAvail().y (it grows with custom page scroll).
	const float headerUsed = ImGui::GetCursorPosY() - contentStartY;
	const float logAreaH = (std::max)(
		120.f,
		ImGui::GetWindowHeight() - headerUsed - ImGui::GetStyle().WindowPadding.y - UiMetrics::kCardGap);

	if (UiCommon::BeginCard("##console_log", width, colors))
	{
		const std::vector<LogEntry> entries = AppLog::Instance().Snapshot(m_filter);

		if (entries.empty())
		{
			m_displayBuf.clear();
			m_displayBuf.push_back('\0');
			m_cachedCount = 0;
			m_cachedTail.clear();
			m_logScrollReady = false;
			m_logScrollVelocity = 0.f;
			m_autoScroll = true;
			UiCommon::CaptionText(
				"Логов пока нет. Сообщения появятся при запуске сервисов и изменении настроек.",
				colors,
				ImGui::GetContentRegionAvail().x);
		}
		else
		{
			const std::string tail = entries.back().text;
			if (m_displayBuf.empty()
				|| m_cachedCount != entries.size()
				|| m_cachedFilter != m_filter
				|| m_cachedTail != tail)
			{
				RebuildDisplayBuffer(entries);
			}

			const bool light = UiCommon::IsLightTheme(colors);
			const ImVec4 consoleBg = light
				? ImVec4(0.92f, 0.92f, 0.94f, 1.f)
				: ImVec4(colors.inputBg.x, colors.inputBg.y, colors.inputBg.z, 1.f);

			const float innerWidth = ImGui::GetContentRegionAvail().x;
			const float innerHeight = (std::max)(96.f, logAreaH - UiMetrics::kCardPad * 2.f - 4.f);

			ImGui::PushStyleColor(ImGuiCol_FrameBg, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive, consoleBg);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
			ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 8.f, 8.f });

			ImGuiWindow* parentBefore = ImGui::GetCurrentWindow();
			char childName[256] = {};
			ImFormatString(childName, IM_ARRAYSIZE(childName), "%s/%s", parentBefore->Name, "##console_log_text");

			// Steal wheel for our inertia. ImGui NewFrame may already have jumped Scroll —
			// we overwrite it below before InputText snapshots Scroll.y for drawing.
			const ImVec2 logMin = ImGui::GetCursorScreenPos();
			const ImVec2 logMax = { logMin.x + innerWidth, logMin.y + innerHeight };
			ImGuiIO& io = ImGui::GetIO();
			const bool pointerOverLog = io.MousePos.x >= logMin.x && io.MousePos.x < logMax.x
				&& io.MousePos.y >= logMin.y && io.MousePos.y < logMax.y;
			const float wheelCaptured = pointerOverLog ? io.MouseWheel : 0.f;
			if (pointerOverLog)
			{
				io.MouseWheel = 0.f;
				io.MouseWheelH = 0.f;
			}

			ImGuiWindow* logWindow = ImGui::FindWindowByName(childName);
			ApplyLogInertiaScroll(logWindow, wheelCaptured, AppSettings::kDefaultScrollMultiplier);

			ImGui::InputTextMultiline(
				"##console_log_text",
				m_displayBuf.data(),
				m_displayBuf.size(),
				{ innerWidth, innerHeight },
				ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);

			// Sync when user drags the scrollbar (InputText owns that interaction).
			logWindow = ImGui::FindWindowByName(childName);
			if (logWindow)
			{
				const ImGuiID scrollId = ImGui::GetWindowScrollbarID(logWindow, ImGuiAxis_Y);
				if (GImGui->ActiveId == scrollId || GImGui->ActiveIdPreviousFrame == scrollId)
				{
					m_logScrollY = logWindow->Scroll.y;
					m_logScrollDisplay = logWindow->Scroll.y;
					m_logScrollVelocity = 0.f;
					m_autoScroll = logWindow->Scroll.y >= logWindow->ScrollMax.y - 2.f;
				}
			}

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(4);
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy({ 0.f, UiMetrics::kCardGap });
	ImGui::PopStyleVar();
}
