#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/ui_home_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "net/traffic_monitor.h"
#include "net/process_net_monitor.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "ui/ui_common.h"
#include "ui/ui_vpn_page.h"
#include "vpn/vpn_manager.h"
#include "zapret/strategies.hpp"
#include "zapret/zapret_manager.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
	const char* RunStatusLabel(ZapretRunStatus status)
	{
		switch (status)
		{
		case ZapretRunStatus::Running: return "Работает";
		case ZapretRunStatus::Starting: return "Запускается";
		case ZapretRunStatus::Stopped: return "Не работает";
		}
		return "Не работает";
	}

	ImVec4 RunStatusColor(ZapretRunStatus status)
	{
		// Fixed — independent of theme accents.
		constexpr ImVec4 kOk = { 33 / 255.f, 176 / 255.f, 77 / 255.f, 1.f };
		constexpr ImVec4 kWarn = { 230 / 255.f, 170 / 255.f, 50 / 255.f, 1.f };
		constexpr ImVec4 kFail = { 196 / 255.f, 72 / 255.f, 72 / 255.f, 1.f };
		switch (status)
		{
		case ZapretRunStatus::Running: return kOk;
		case ZapretRunStatus::Starting: return kWarn;
		case ZapretRunStatus::Stopped: return kFail;
		}
		return kFail;
	}

	ImVec4 FixedOk()
	{
		return { 33 / 255.f, 176 / 255.f, 77 / 255.f, 1.f };
	}

	ImVec4 FixedWarn()
	{
		return { 230 / 255.f, 170 / 255.f, 50 / 255.f, 1.f };
	}

	ImVec4 FixedFail()
	{
		return { 196 / 255.f, 72 / 255.f, 72 / 255.f, 1.f };
	}

	std::string FormatSpeed(float bytesPerSec, bool bitsMode)
	{
		char buffer[32] = {};
		if (bitsMode)
		{
			const float bits = bytesPerSec * 8.f;
			if (bits < 1000.f)
				snprintf(buffer, sizeof buffer, "%.0f бит/с", bits);
			else if (bits < 1000000.f)
			{
				const float kbit = bits / 1000.f;
				if (kbit < 10.f)
					snprintf(buffer, sizeof buffer, "%.2f Кбит/с", kbit);
				else if (kbit < 100.f)
					snprintf(buffer, sizeof buffer, "%.1f Кбит/с", kbit);
				else
					snprintf(buffer, sizeof buffer, "%.0f Кбит/с", kbit);
			}
			else
			{
				const float mbit = bits / 1000000.f;
				if (mbit < 10.f)
					snprintf(buffer, sizeof buffer, "%.2f Мбит/с", mbit);
				else if (mbit < 100.f)
					snprintf(buffer, sizeof buffer, "%.1f Мбит/с", mbit);
				else
					snprintf(buffer, sizeof buffer, "%.0f Мбит/с", mbit);
			}
			return buffer;
		}

		if (bytesPerSec < 1024.f)
			snprintf(buffer, sizeof buffer, "%.0f Б/с", bytesPerSec);
		else if (bytesPerSec < 1024.f * 1024.f)
			snprintf(buffer, sizeof buffer, "%.1f КБ/с", bytesPerSec / 1024.f);
		else
			snprintf(buffer, sizeof buffer, "%.2f МБ/с", bytesPerSec / (1024.f * 1024.f));
		return buffer;
	}

	std::string FormatBytes(std::uint64_t bytes)
	{
		if (bytes < 1024ull)
		{
			char buffer[32] = {};
			snprintf(buffer, sizeof buffer, "%llu B", static_cast<unsigned long long>(bytes));
			return buffer;
		}
		if (bytes < 1024ull * 1024ull)
		{
			char buffer[32] = {};
			snprintf(buffer, sizeof buffer, "%.1f KB", static_cast<double>(bytes) / 1024.0);
			return buffer;
		}
		if (bytes < 1024ull * 1024ull * 1024ull)
		{
			char buffer[32] = {};
			snprintf(buffer, sizeof buffer, "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
			return buffer;
		}
		char buffer[32] = {};
		snprintf(buffer, sizeof buffer, "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
		return buffer;
	}

	std::string FormatAxisSpeed(float bps, bool bitsMode)
	{
		char buffer[16] = {};
		if (bitsMode)
		{
			const float bits = bps * 8.f;
			if (bits < 1000.f)
			{
				snprintf(buffer, sizeof buffer, "%.0f b", bits);
				return buffer;
			}
			if (bits < 1000000.f)
			{
				const float kb = bits / 1000.f;
				if (kb >= 10.f)
					snprintf(buffer, sizeof buffer, "%.0f Kb", kb);
				else
					snprintf(buffer, sizeof buffer, "%.1f Kb", kb);
				return buffer;
			}
			const float mb = bits / 1000000.f;
			if (mb >= 10.f)
				snprintf(buffer, sizeof buffer, "%.0f Mb", mb);
			else
				snprintf(buffer, sizeof buffer, "%.1f Mb", mb);
			return buffer;
		}

		if (bps < 1024.f)
		{
			snprintf(buffer, sizeof buffer, "%.0f B", bps);
			return buffer;
		}
		if (bps < 1024.f * 1024.f)
		{
			const float kb = bps / 1024.f;
			if (kb >= 10.f)
				snprintf(buffer, sizeof buffer, "%.0f K", kb);
			else
				snprintf(buffer, sizeof buffer, "%.1f K", kb);
			return buffer;
		}
		const float mb = bps / (1024.f * 1024.f);
		if (mb >= 10.f)
			snprintf(buffer, sizeof buffer, "%.0f M", mb);
		else
			snprintf(buffer, sizeof buffer, "%.1f M", mb);
		return buffer;
	}

	// Steam-style segmented control: МБ/с | Мбит/с
	bool DrawSpeedUnitToggle(bool bitsMode, const UiThemeColors& colors)
	{
		const char* leftLabel = "МБ/с";
		const char* rightLabel = "Мбит/с";
		const float padX = 10.f;
		const float padY = 4.f;
		const float gap = 1.f;
		const ImVec2 leftSize = ImGui::CalcTextSize(leftLabel);
		const ImVec2 rightSize = ImGui::CalcTextSize(rightLabel);
		const float segH = (std::max)(leftSize.y, rightSize.y) + padY * 2.f;
		const float leftW = leftSize.x + padX * 2.f;
		const float rightW = rightSize.x + padX * 2.f;
		const float totalW = leftW + gap + rightW;

		const float avail = ImGui::GetContentRegionAvail().x;
		if (avail > totalW)
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - totalW);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const float rounding = 6.f;

		drawList->AddRectFilled(
			origin,
			{ origin.x + totalW, origin.y + segH },
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.inputBg, 0.95f)),
			rounding);

		bool changed = false;
		ImGui::PushID("##net_speed_unit");

		ImGui::InvisibleButton("##bytes", { leftW, segH });
		const bool leftHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked() && bitsMode)
			changed = true;
		{
			const ImVec2 a = origin;
			const ImVec2 b = { origin.x + leftW, origin.y + segH };
			if (!bitsMode)
				drawList->AddRectFilled(a, b, ImGui::GetColorU32(colors.navActive), rounding, ImDrawFlags_RoundCornersLeft);
			else if (leftHovered)
				drawList->AddRectFilled(a, b, ImGui::GetColorU32(UiCommon::WithAlpha(colors.navHover, 0.85f)), rounding, ImDrawFlags_RoundCornersLeft);
			drawList->AddText(
				{ a.x + padX, a.y + (segH - leftSize.y) * 0.5f },
				ImGui::GetColorU32(!bitsMode ? colors.textPrimary : colors.textMuted),
				leftLabel);
		}

		ImGui::SameLine(0.f, gap);
		ImGui::InvisibleButton("##bits", { rightW, segH });
		const bool rightHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked() && !bitsMode)
			changed = true;
		{
			const ImVec2 a = { origin.x + leftW + gap, origin.y };
			const ImVec2 b = { a.x + rightW, origin.y + segH };
			if (bitsMode)
				drawList->AddRectFilled(a, b, ImGui::GetColorU32(colors.navActive), rounding, ImDrawFlags_RoundCornersRight);
			else if (rightHovered)
				drawList->AddRectFilled(a, b, ImGui::GetColorU32(UiCommon::WithAlpha(colors.navHover, 0.85f)), rounding, ImDrawFlags_RoundCornersRight);
			drawList->AddText(
				{ a.x + padX, a.y + (segH - rightSize.y) * 0.5f },
				ImGui::GetColorU32(bitsMode ? colors.textPrimary : colors.textMuted),
				rightLabel);
		}

		drawList->AddRect(
			origin,
			{ origin.x + totalW, origin.y + segH },
			ImGui::GetColorU32(colors.tileBorder),
			rounding);

		ImGui::PopID();
		return changed;
	}

	std::string FormatRelativeTime(float secondsAgo)
	{
		char buffer[32] = {};
		if (secondsAgo <= 0.05f)
			return "сейчас";
		if (secondsAgo < 59.5f)
		{
			snprintf(buffer, sizeof buffer, "−%.0f с", secondsAgo);
			return buffer;
		}
		const int totalSec = static_cast<int>(secondsAgo + 0.5f);
		snprintf(buffer, sizeof buffer, "−%d:%02d", totalSec / 60, totalSec % 60);
		return buffer;
	}

	float SampleValue(
		const TrafficMonitor& monitor,
		bool download,
		size_t chronologicalIndex)
	{
		return download
			? monitor.GetDownloadSampleAt(chronologicalIndex)
			: monitor.GetUploadSampleAt(chronologicalIndex);
	}

	// ageSlots: 0 = live tip (right), larger = older (toward left).
	float ValueAtAgeSlots(
		const TrafficMonitor& monitor,
		bool download,
		float liveValue,
		float scrollPhase,
		float ageSlots)
	{
		if (ageSlots <= 0.001f)
			return liveValue;

		const size_t count = monitor.GetHistorySampleCount();
		if (count == 0)
			return 0.f;

		const float fromNewest = ageSlots - scrollPhase + 1.f;
		const float chronoFloat = static_cast<float>(count - 1) - fromNewest;
		if (chronoFloat < 0.f)
		{
			// Keep oldest sample while it scrolls off — never snap to 0 on a full buffer.
			if (count >= TrafficMonitor::kHistorySize)
				return SampleValue(monitor, download, 0);
			return 0.f;
		}

		const size_t i0 = static_cast<size_t>((std::clamp)(chronoFloat, 0.f, static_cast<float>(count - 1)));
		const size_t i1 = (std::min)(i0 + 1, count - 1);
		const float frac = chronoFloat - static_cast<float>(i0);
		const float v0 = SampleValue(monitor, download, i0);
		const float v1 = SampleValue(monitor, download, i1);
		return v0 + (v1 - v0) * (std::clamp)(frac, 0.f, 1.f);
	}

	float AnimatedTip(const TrafficMonitor& monitor, bool download, float phase)
	{
		const size_t count = monitor.GetHistorySampleCount();
		if (count == 0)
			return 0.f;
		const float previous = count > 1 ? SampleValue(monitor, download, count - 2) : 0.f;
		const float latest = SampleValue(monitor, download, count - 1);
		return previous + (latest - previous) * (phase * phase * (3.f - 2.f * phase));
	}

	void BuildSeriesPoints(
		const TrafficMonitor& monitor,
		bool download,
		float graphLeft,
		float graphRight,
		float graphTop,
		float graphBottom,
		float scaleMax,
		float scrollPhase,
		float liveValue,
		std::vector<ImVec2>& outPoints)
	{
		outPoints.clear();
		const float graphW = (std::max)(1.f, graphRight - graphLeft);
		const float graphH = (std::max)(1.f, graphBottom - graphTop);
		const float slotW = graphW / static_cast<float>(TrafficMonitor::kHistorySize - 1);

		auto pushPoint = [&](float ageSlots, float value)
		{
			const float x = graphRight - ageSlots * slotW;
			// Кривая строится за краем и обрезается только при отрисовке.
			if (ageSlots > static_cast<float>(TrafficMonitor::kHistorySize))
				return;
			const float norm = (std::clamp)(value / scaleMax, 0.f, 1.f);
			outPoints.push_back({ x, graphBottom - norm * graphH });
		};

		// Новая точка плавно входит справа; предыдущий замер уходит в историю.
		pushPoint(0.f, liveValue);
		for (size_t rev = 0; rev <= TrafficMonitor::kHistorySize; ++rev)
		{
			const float ageSlots = static_cast<float>(rev) + scrollPhase;
			if (ageSlots < 0.001f)
				continue;
			pushPoint(ageSlots, ValueAtAgeSlots(monitor, download, liveValue, scrollPhase, ageSlots));
		}

		std::reverse(outPoints.begin(), outPoints.end());

		// While buffer is filling: stretch left with zeros so the line starts at Y=0.
		if (!outPoints.empty() && outPoints.front().x > graphLeft + 0.5f)
		{
			outPoints.insert(outPoints.begin(), ImVec2{ graphLeft, graphBottom });
		}
	}

	void TessellateSmooth(
		const std::vector<ImVec2>& control,
		std::vector<ImVec2>& outSmooth,
		int segmentsPerSpan = 16)
	{
		outSmooth.clear();
		if (control.size() < 2)
		{
			outSmooth = control;
			return;
		}
		if (control.size() == 2)
		{
			outSmooth = control;
			return;
		}

		outSmooth.reserve(control.size() * static_cast<size_t>(segmentsPerSpan));
		for (size_t i = 0; i + 1 < control.size(); ++i)
		{
			const ImVec2& p1 = control[i];
			const ImVec2& p2 = control[i + 1];

			for (int s = 0; s < segmentsPerSpan; ++s)
			{
				const float t = static_cast<float>(s) / static_cast<float>(segmentsPerSpan);
				const float eased = t * t * t * (10.f + t * (-15.f + 6.f * t));
				outSmooth.push_back({ p1.x + (p2.x - p1.x) * t, p1.y + (p2.y - p1.y) * eased });
			}
		}
		outSmooth.push_back(control.back());
	}

	void DrawSeriesArea(
		ImDrawList* drawList,
		const std::vector<ImVec2>& smoothPoints,
		float graphBottom,
		ImU32 lineColor,
		ImU32 fillColor)
	{
		if (smoothPoints.size() < 2)
			return;

		const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
		const ImU32 transparent = fillColor & ~IM_COL32_A_MASK;
		for (size_t i = 0; i + 1 < smoothPoints.size(); ++i)
		{
			const ImVec2& a = smoothPoints[i];
			const ImVec2& b = smoothPoints[i + 1];
			drawList->PrimReserve(6, 4);
			const auto first = drawList->_VtxCurrentIdx;
			for (unsigned offset : { 0u, 1u, 2u, 0u, 2u, 3u })
				drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + offset));
			drawList->PrimWriteVtx(a, uv, fillColor);
			drawList->PrimWriteVtx(b, uv, fillColor);
			drawList->PrimWriteVtx({ b.x, graphBottom }, uv, transparent);
			drawList->PrimWriteVtx({ a.x, graphBottom }, uv, transparent);
		}

		drawList->AddPolyline(smoothPoints.data(), static_cast<int>(smoothPoints.size()),
			lineColor, ImDrawFlags_None, 2.25f);
	}

	void DrawSpeedGraph(
		const TrafficMonitor& monitor,
		float width,
		float height,
		bool bitsMode,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		float fillAlphaDown = 0.28f,
		float fillAlphaUp = 0.22f)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 rectMax = { origin.x + width, origin.y + height };
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(origin, rectMax, ImGui::GetColorU32(colors.inputBg), UiMetrics::kCardRadius);
		drawList->AddRect(origin, rectMax, ImGui::GetColorU32(colors.tileBorder), UiMetrics::kCardRadius);

		const float scaleMax = (std::max)(monitor.GetDisplayScaleMax(), 1024.f);
		const float scrollPhase = (std::clamp)(monitor.GetScrollPhase(), 0.f, 1.f);
		const float labelWidth = bitsMode ? 48.f : 42.f;
		const float graphPadRight = 10.f;
		const float graphPadTop = 8.f;
		const float timeAxisH = 18.f;
		const float graphPadBottom = timeAxisH + 4.f;
		const float graphLeft = origin.x + labelWidth + 6.f;
		const float graphRight = rectMax.x - graphPadRight;
		const float graphTop = origin.y + graphPadTop;
		const float graphBottom = rectMax.y - graphPadBottom;
		const float graphW = (std::max)(1.f, graphRight - graphLeft);
		const float graphH = (std::max)(1.f, graphBottom - graphTop);
		const float windowSec =
			static_cast<float>(TrafficMonitor::kHistorySize - 1) * TrafficMonitor::kSampleIntervalSec;

		const ImU32 gridColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.18f));
		const ImU32 labelColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.85f));
		constexpr int kGridLines = 4;
		for (int i = 0; i <= kGridLines; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(kGridLines);
			const float y = graphBottom - t * graphH;
			drawList->AddLine({ graphLeft, y }, { graphRight, y }, gridColor, 1.f);

			const float speed = scaleMax * t;
			const std::string label = FormatAxisSpeed(speed, bitsMode);
			const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			drawList->AddText(
				{ graphLeft - textSize.x - 6.f, y - textSize.y * 0.5f },
				labelColor,
				label.c_str());
		}

		std::vector<ImVec2> downControl;
		std::vector<ImVec2> upControl;
		BuildSeriesPoints(
			monitor,
			true,
			graphLeft,
			graphRight,
			graphTop,
			graphBottom,
			scaleMax,
			scrollPhase,
			AnimatedTip(monitor, true, scrollPhase),
			downControl);
		BuildSeriesPoints(
			monitor,
			false,
			graphLeft,
			graphRight,
			graphTop,
			graphBottom,
			scaleMax,
			scrollPhase,
			AnimatedTip(monitor, false, scrollPhase),
			upControl);

		std::vector<ImVec2> downSmooth;
		std::vector<ImVec2> upSmooth;
		TessellateSmooth(downControl, downSmooth);
		TessellateSmooth(upControl, upSmooth);

		const ImVec4 downAccent = accents.download;
		const ImVec4 upAccent = accents.upload;

		drawList->PushClipRect({ graphLeft, graphTop }, { graphRight, graphBottom }, true);
		DrawSeriesArea(
			drawList,
			downSmooth,
			graphBottom,
			ImGui::GetColorU32(downAccent),
			ImGui::GetColorU32(UiCommon::WithAlpha(downAccent, fillAlphaDown)));
		DrawSeriesArea(
			drawList,
			upSmooth,
			graphBottom,
			ImGui::GetColorU32(upAccent),
			ImGui::GetColorU32(UiCommon::WithAlpha(upAccent, fillAlphaUp)));

		const auto drawLatest = [&](const std::vector<ImVec2>& points, const ImVec4& accent) {
			if (points.empty())
				return;
			drawList->AddCircleFilled(points.back(), 6.f, ImGui::GetColorU32(UiCommon::WithAlpha(accent, 0.22f)), 16);
			drawList->AddCircleFilled(points.back(), 3.2f, ImGui::GetColorU32(accent), 12);
		};
		drawLatest(downSmooth, downAccent);
		drawLatest(upSmooth, upAccent);

		// Hover scrubber.
		ImGui::SetCursorScreenPos({ graphLeft, graphTop });
		ImGui::InvisibleButton("##speed_graph_hover", { graphW, graphH });
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			const float mouseX = (std::clamp)(ImGui::GetIO().MousePos.x, graphLeft, graphRight);
			const float ageSlots = (graphRight - mouseX) / (graphW / static_cast<float>(TrafficMonitor::kHistorySize - 1));
			const float ageSec = ageSlots * TrafficMonitor::kSampleIntervalSec;
			const auto traceY = [mouseX, graphBottom](const std::vector<ImVec2>& points) {
				if (points.empty())
					return graphBottom;
				const auto right = std::lower_bound(points.begin(), points.end(), mouseX,
					[](const ImVec2& point, float x) { return point.x < x; });
				if (right == points.begin())
					return right->y;
				if (right == points.end())
					return points.back().y;
				const auto& left = *(right - 1);
				return left.y + (right->y - left.y) * ((mouseX - left.x) / (right->x - left.x));
			};
			const float downY = traceY(downSmooth);
			const float upY = traceY(upSmooth);
			const float downHover = (graphBottom - downY) * scaleMax / graphH;
			const float upHover = (graphBottom - upY) * scaleMax / graphH;

			const ImU32 scrubColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.textPrimary, 0.55f));
			drawList->AddLine({ mouseX, graphTop }, { mouseX, graphBottom }, scrubColor, 1.25f);

			drawList->AddCircleFilled({ mouseX, downY }, 3.5f, ImGui::GetColorU32(downAccent), 12);
			drawList->AddCircleFilled({ mouseX, upY }, 3.5f, ImGui::GetColorU32(upAccent), 12);

			char tip[160] = {};
			snprintf(
				tip,
				sizeof tip,
				"%s\n↓ %s\n↑ %s",
				FormatRelativeTime(ageSec).c_str(),
				FormatSpeed(downHover, bitsMode).c_str(),
				FormatSpeed(upHover, bitsMode).c_str());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
				UiCommon::ShowTooltip("%s", tip);
		}
		drawList->PopClipRect();

		// Time axis under the plot.
		const ImU32 timeColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.9f));
		constexpr int kTimeMarks = 4;
		for (int i = 0; i <= kTimeMarks; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(kTimeMarks);
			const float x = graphLeft + t * graphW;
			const float ageSec = (1.f - t) * windowSec;
			const std::string label = (i == kTimeMarks) ? std::string("сейчас") : FormatRelativeTime(ageSec);
			const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			float textX = x - textSize.x * 0.5f;
			if (i == 0)
				textX = x;
			else if (i == kTimeMarks)
				textX = x - textSize.x;
			drawList->AddLine(
				{ x, graphBottom },
				{ x, graphBottom + 3.f },
				ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.35f)),
				1.f);
			drawList->AddText({ textX, graphBottom + 4.f }, timeColor, label.c_str());
		}

		char windowLabel[32] = {};
		snprintf(windowLabel, sizeof windowLabel, "1 мин");
		const ImVec2 windowSize = ImGui::CalcTextSize(windowLabel);
		drawList->AddText(
			{ graphRight - windowSize.x, origin.y + 4.f },
			ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.7f)),
			windowLabel);

		drawList->AddRect(origin, rectMax, ImGui::GetColorU32(colors.tileBorder), UiMetrics::kCardRadius);

		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy({ width, height });
	}

	void DrawMetricLine(ImDrawList* drawList, ImVec2 pos, float width, float height, ImU32 color, const std::string& text)
	{
		const float pad = 8.f;
		const float available = (std::max)(1.f, width - 2.f * pad);
		const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
		const float scale = textWidth > available ? (std::max)(0.8f, available / textWidth) : 1.f;
		const float size = ImGui::GetFontSize() * scale;
		const ImVec4 clip(pos.x + pad, pos.y, pos.x + width - pad, pos.y + height);
		const float x = pos.x + (std::max)(pad, (width - textWidth * scale) * 0.5f);
		drawList->AddText(ImGui::GetFont(), size, { x, pos.y + (height - size) * 0.5f },
			color, text.c_str(), nullptr, 0.f, &clip);
		ImGui::Dummy({ width, height });
		if (textWidth * scale > available && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("%s", text.c_str());
	}

	void DrawRateCard(
		const char* title,
		float bytesPerSec,
		bool bitsMode,
		ImVec4 accent,
		float width,
		float height)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(
			pos,
			{ pos.x + width, pos.y + height },
			ImGui::GetColorU32(UiCommon::WithAlpha(accent, 0.10f)),
			8.f);
		drawList->AddRect(
			pos,
			{ pos.x + width, pos.y + height },
			ImGui::GetColorU32(UiCommon::WithAlpha(accent, 0.35f)),
			8.f);

		DrawMetricLine(drawList, pos, width, height, ImGui::GetColorU32(accent),
			std::string(title) + ": " + FormatSpeed(bytesPerSec, bitsMode));
	}

	void DrawSessionCard(
		std::uint64_t bytesIn,
		std::uint64_t bytesOut,
		const UiThemeColors& colors,
		float width,
		float height)
	{
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(
			pos,
			{ pos.x + width, pos.y + height },
			ImGui::GetColorU32(colors.inputBg),
			8.f);
		drawList->AddRect(
			pos,
			{ pos.x + width, pos.y + height },
			ImGui::GetColorU32(colors.tileBorder),
			8.f);

		DrawMetricLine(drawList, pos, width, height, ImGui::GetColorU32(colors.textPrimary),
			"Сессия: ↓ " + FormatBytes(bytesIn) + "  ↑ " + FormatBytes(bytesOut));
	}

	void DrawSpeedProgressBars(
		float downBps,
		float upBps,
		float scaleMaxBytesPerSec,
		const UiThemeColors& colors)
	{
		const float barH = 7.f;
		const float barW = ImGui::GetContentRegionAvail().x;
		const float denom = (std::max)(scaleMaxBytesPerSec, 1.f);
		ImDrawList* dl = ImGui::GetWindowDrawList();

		auto drawBar = [&](float value, ImVec4 color)
		{
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			dl->AddRectFilled(
				origin,
				{ origin.x + barW, origin.y + barH },
				ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.12f)),
				3.f);
			dl->AddRectFilled(
				origin,
				{ origin.x + barW * (std::clamp)(value / denom, 0.f, 1.f), origin.y + barH },
				ImGui::GetColorU32(UiCommon::WithAlpha(color, 0.85f)),
				3.f);
			ImGui::Dummy({ barW, barH + 5.f });
		};

		drawBar(downBps, UiCommon::FixedDownloadAccent());
		drawBar(upBps, UiCommon::FixedUploadAccent());
	}

	void DrawMiniBadge(const char* label, ImVec4 color, const UiThemeColors& colors, ImVec2* outSize = nullptr)
	{
		const ImVec2 pad = { 8.f, 3.f };
		const ImVec2 textSize = ImGui::CalcTextSize(label);
		const ImVec2 size = { textSize.x + pad.x * 2.f, textSize.y + pad.y * 2.f };
		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(pos, { pos.x + size.x, pos.y + size.y }, ImGui::GetColorU32(UiCommon::WithAlpha(color, 0.18f)), 4.f);
		drawList->AddText({ pos.x + pad.x, pos.y + pad.y }, ImGui::GetColorU32(color), label);
		ImGui::Dummy(size);
		if (outSize)
			*outSize = size;
	}

	void DrawStatusRowWithNote(
		const char* statusText,
		ImVec4 statusColor,
		const char* noteText,
		float innerWidth,
		const UiThemeColors& colors)
	{
		const float rowY = ImGui::GetCursorPosY();
		const float rowStartX = ImGui::GetCursorPosX();

		ImVec2 badgeSize {};
		DrawMiniBadge(statusText, statusColor, colors, &badgeSize);

		if (noteText && noteText[0] != '\0')
		{
			const float gap = 8.f;
			const float noteMaxWidth = (std::max)(0.f, innerWidth - badgeSize.x - gap);
			std::string note = noteText;
			if (noteMaxWidth > 0.f)
			{
				if (ImGui::CalcTextSize(note.c_str()).x > noteMaxWidth)
				{
					while (note.size() > 1 && ImGui::CalcTextSize((note + "...").c_str()).x > noteMaxWidth)
						note.pop_back();
					note += "...";
				}
			}

			if (!note.empty())
			{
				const float noteWidth = ImGui::CalcTextSize(note.c_str()).x;
				const float textOffsetY = (badgeSize.y - ImGui::GetTextLineHeight()) * 0.5f;
				ImGui::SetCursorPos({ rowStartX + innerWidth - noteWidth, rowY + textOffsetY });
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
				ImGui::TextUnformatted(note.c_str());
				ImGui::PopStyleColor();
			}
		}

		ImGui::SetCursorPos({ rowStartX, rowY + badgeSize.y + 8.f });
	}

	bool BeginHomeModuleCard(const char* id, float width, float height, const UiThemeColors& colors)
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { UiMetrics::kCardPad, UiMetrics::kCardPad });
		return ImGui::BeginChild(id, { width, height }, ImGuiChildFlags_Borders);
	}

	void EndHomeModuleCard()
	{
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}

	float HomeModuleCardHeight()
	{
		const float line = ImGui::GetTextLineHeight();
		const float badgeH = line + 6.f;
		return UiMetrics::kCardPad * 2.f
			+ line
			+ UiMetrics::kRowGap
			+ line * 2.f
			+ 4.f
			+ badgeH
			+ 8.f
			+ 16.f
			+ UiMetrics::kBtnHeight
			+ UiMetrics::kRowGap;
	}

	void DrawHomeModuleCard(
		const char* id,
		float width,
		float height,
		const char* title,
		const char* subtitle,
		const char* statusText,
		ImVec4 statusColor,
		const char* statusNote,
		const UiThemeColors& colors,
		const std::function<void(float)>& drawActions)
	{
		const bool open = BeginHomeModuleCard(id, width, height, colors);

		if (open)
		{
			const float innerWidth = ImGui::GetContentRegionAvail().x;
			const float innerHeight = ImGui::GetContentRegionAvail().y;
			const float line = ImGui::GetTextLineHeight();
			const float subtitleBlockH = line * 2.f;

			UiCommon::SectionHeader(title, colors);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::BeginChild("##subtitle", { innerWidth, subtitleBlockH }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
			ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
			ImGui::TextUnformatted(subtitle ? subtitle : "");
			ImGui::PopTextWrapPos();
			ImGui::EndChild();
			ImGui::PopStyleColor();

			ImGui::Dummy({ 0.f, 4.f });
			DrawStatusRowWithNote(statusText, statusColor, statusNote, innerWidth, colors);

			const float btnY = innerHeight - UiMetrics::kBtnHeight;
			const float remaining = btnY - ImGui::GetCursorPosY();
			if (remaining > 0.f)
				ImGui::Dummy({ 0.f, remaining });

			drawActions(innerWidth);
		}

		EndHomeModuleCard();
	}
}

void UiHomePage::SetManagers(
	ZapretManager* zapret,
	TgWsProxyManager* tgProxy,
	VpnManager* vpn,
	UiVpnPage* vpnPage,
	AppSettings* settings,
	TrafficMonitor* traffic,
	ProcessNetMonitor* processNet)
{
	m_zapret = zapret;
	m_tgProxy = tgProxy;
	m_vpn = vpn;
	m_vpnPage = vpnPage;
	m_settings = settings;
	m_traffic = traffic;
	m_processNet = processNet;
}

void UiHomePage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();

	if (!m_preferencesLoaded)
	{
		if (m_settings)
			m_autoSelect = m_settings->GetAutoSelectBestStrategy();
		if (m_zapret)
			m_selectedStrategy = m_zapret->GetPreferredStrategyIndex(m_autoSelect);
		m_preferencesLoaded = true;
	}

	const ZapretRunStatus azStatus = m_zapret ? m_zapret->GetCachedRunStatus() : ZapretRunStatus::Stopped;
	const bool azRunning = azStatus == ZapretRunStatus::Running || azStatus == ZapretRunStatus::Starting;
	const bool tgRunning = m_tgProxy && m_tgProxy->IsRunning();
	const bool vpnRunning = m_vpn && m_vpn->IsRunning();
	const bool vpnEnabled = m_vpnPage && m_vpnPage->IsVpnEnabled();
	const bool discordOnline = m_zapret && m_zapret->IsDiscordOnline();
	const bool youtubeOnline = m_zapret && m_zapret->IsYouTubeOnline();
	const bool telegramProbeOk = m_zapret && m_zapret->IsTelegramOnline();
	const int activeStrategy = m_zapret ? m_zapret->GetActiveStrategyIndex() : -1;
	const StrategyTestState strategyTestState = m_zapret
		? m_zapret->GetStrategyTestState()
		: StrategyTestState::Idle;
	const bool strategyTestRunning = strategyTestState == StrategyTestState::Running;
	const bool strategyTestPaused = strategyTestState == StrategyTestState::Paused;
	const bool strategySelectionActive = strategyTestRunning || strategyTestPaused;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.f, UiMetrics::kRowGap });
	UiCommon::PageTitle(
		fonts,
		0xf015,
		"Главная",
		"Быстрое управление и обзор состояния",
		colors,
		UiCommon::TitleIconFont::Solid);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.f, 6.f });
	if (ImGui::BeginChild("##home_services", { width, 0.f }, ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Доступность сервисов");
		ImGui::PopStyleColor();
		ImGui::SameLine(0.f, 16.f);

		const float serviceGap = 20.f;
		UiCommon::DrawServiceInline("Discord", discordOnline, accents.discord, colors, accents);
		ImGui::SameLine(0.f, serviceGap);
		UiCommon::DrawServiceInline("YouTube", youtubeOnline, accents.youtube, colors, accents);
		ImGui::SameLine(0.f, serviceGap);
		if (tgRunning)
			UiCommon::DrawServiceInline("Telegram", true, accents.telegram, colors, accents, "MTPROTO(ok)");
		else
			UiCommon::DrawServiceInline("Telegram", telegramProbeOk, accents.telegram, colors, accents);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	UiCommon::CardGap();

	const float moduleGap = UiMetrics::kGridGap;
	const float moduleW = (width - moduleGap * 2.f) / 3.f;
	const float moduleH = HomeModuleCardHeight();

	std::string strategyNote;
	if (strategyTestRunning && m_zapret)
	{
		const int total = m_zapret->GetStrategyTestTotal();
		const int current = m_zapret->GetStrategyTestCurrent();
		const int testingIndex = m_zapret->GetStrategyTestActiveIndex();
		if (total > 0)
		{
			char buf[160] = {};
			if (testingIndex >= 0)
			{
				snprintf(
					buf,
					sizeof buf,
					"Тест: %s (%d/%d)",
					m_zapret->GetStrategyLabel(testingIndex).c_str(),
					current,
					total);
			}
			else
				snprintf(buf, sizeof buf, "Тестирование... (%d/%d)", current, total);
			strategyNote = buf;
		}
	}
	const std::string strategyLabel = azStatus == ZapretRunStatus::Running && m_zapret && !strategySelectionActive
		? m_zapret->GetStrategyLabel(activeStrategy)
		: std::string {};
	const std::string azSubtitle = strategyLabel.empty() ? "Обход Discord, YouTube" : "Стратегия - " + strategyLabel;

	const char* azStatusText = RunStatusLabel(azStatus);
	ImVec4 azStatusColor = RunStatusColor(azStatus);
	if (strategySelectionActive)
	{
		azStatusText = strategyTestPaused ? "Подбор приостановлен" : "Подбор стратегий";
		azStatusColor = ImVec4(0.92f, 0.62f, 0.18f, 1.f);
	}

	DrawHomeModuleCard(
		"##home_az",
		moduleW,
		moduleH,
		"Антизапрет",
		azSubtitle.c_str(),
		azStatusText,
		azStatusColor,
		strategyNote.empty() ? nullptr : strategyNote.c_str(),
		colors,
		[&](float btnW)
		{
			if (strategySelectionActive)
			{
				const char* label = m_zapret
					? m_zapret->GetStrategyTestButtonLabel()
					: "Остановить подбор";
				const ImVec4 accent = strategyTestRunning
					? UiCommon::FixedStopAccent()
					: UiCommon::FixedStartAccent();
				if (UiCommon::AccentButton(
					label,
					{ btnW, UiMetrics::kBtnHeight },
					accent,
					colors,
					m_zapret && !m_zapret->IsOperationInFlight()) && m_zapret)
				{
					m_zapret->HandleStrategyTestButton(ZapretStrategies::GameFilterMode::Disabled);
				}
				if (strategyTestRunning)
				{
					UiCommon::SetItemTooltip(
						"Можно полностью остановить, нажав правой кнопкой мыши.");
					if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && m_zapret)
						m_zapret->RequestStopStrategyTest();
				}
			}
			else if (azRunning)
			{
				if (UiCommon::AccentButton("Остановить", { btnW, UiMetrics::kBtnHeight }, UiCommon::FixedStopAccent(), colors) && m_zapret)
					m_zapret->RequestStop();
			}
			else if (UiCommon::AccentButton(
				"Запустить",
				{ btnW, UiMetrics::kBtnHeight },
				UiCommon::FixedStartAccent(),
				colors,
				m_zapret && !m_zapret->IsOperationInFlight()) && m_zapret)
			{
				const bool firstRun = m_zapret->GetStore().GetResults().empty();
				if (firstRun)
					m_zapret->HandleStrategyTestButton(ZapretStrategies::GameFilterMode::Disabled);
				else
					m_zapret->RequestStart(m_selectedStrategy, ZapretStrategies::GameFilterMode::Disabled);
			}
		});
	ImGui::SameLine(0.f, moduleGap);

	DrawHomeModuleCard(
		"##home_tg",
		moduleW,
		moduleH,
		"TG WS Proxy",
		"MTProto для Telegram",
		tgRunning ? "Работает" : "Не работает",
		tgRunning ? FixedOk() : FixedFail(),
		nullptr,
		colors,
		[&](float btnW)
		{
			const char* label = m_tgProxy ? m_tgProxy->GetPrimaryActionLabel() : "Запустить";
			const bool canAction = m_tgProxy && m_tgProxy->CanPrimaryAction();
			const ImVec4 btnColor = tgRunning ? UiCommon::FixedStopAccent() : UiCommon::FixedStartAccent();
			const bool openTelegram = m_settings && m_settings->GetOpenTelegramOnProxyStart();
			if (UiCommon::AccentButton(label, { btnW, UiMetrics::kBtnHeight }, btnColor, colors, canAction) && m_tgProxy)
				m_tgProxy->HandlePrimaryAction(openTelegram);
		});
	ImGui::SameLine(0.f, moduleGap);

	const std::string vpnSubtitle = m_vpnPage ? m_vpnPage->GetActiveServerLabel() : "Сервер не выбран";
	DrawHomeModuleCard(
		"##home_vpn",
		moduleW,
		moduleH,
		"VPN",
		vpnSubtitle.c_str(),
		vpnRunning ? "Подключён" : (vpnEnabled ? "Подключение..." : "Отключён"),
		vpnRunning ? FixedOk() : (vpnEnabled ? FixedWarn() : FixedFail()),
		nullptr,
		colors,
		[&](float btnW)
		{
			if (vpnRunning || vpnEnabled)
			{
				if (UiCommon::AccentButton("Отключить VPN", { btnW, UiMetrics::kBtnHeight }, UiCommon::FixedStopAccent(), colors) && m_vpnPage)
					m_vpnPage->SetVpnEnabled(false);
			}
			else if (UiCommon::AccentButton(
				"Подключить VPN",
				{ btnW, UiMetrics::kBtnHeight },
				UiCommon::FixedStartAccent(),
				colors,
				m_vpnPage && m_vpnPage->HasActiveServer()) && m_vpnPage)
			{
				m_vpnPage->SetVpnEnabled(true);
			}
		});

	UiCommon::CardGap();

	if (UiCommon::BeginCard("##home_traffic", width, colors))
	{
		const bool bitsMode = m_settings && m_settings->GetNetworkSpeedBits();
		bool displayBits = bitsMode;
		const float downBps = m_traffic ? m_traffic->GetDisplayDownloadBps() : 0.f;
		const float upBps = m_traffic ? m_traffic->GetDisplayUploadBps() : 0.f;
		const std::uint64_t bytesIn = m_traffic ? m_traffic->GetSessionBytesIn() : 0;
		const std::uint64_t bytesOut = m_traffic ? m_traffic->GetSessionBytesOut() : 0;
		const float scaleMax = m_traffic ? (std::max)(m_traffic->GetDisplayScaleMax(), 1.f) : 1.f;
		(void)scaleMax;
		m_cardSampleTimer += ImGui::GetIO().DeltaTime;
		if (!m_cardSampleReady || m_cardSampleTimer >= 0.75f)
		{
			m_cardSampleTimer = 0.f;
			m_cardDownloadBps = downBps;
			m_cardUploadBps = upBps;
			m_cardSampleReady = true;
		}
		const auto drawMetrics = [&](float width) {
			const float gap = 6.f;
			const float cardH = ImGui::GetTextLineHeight() + 8.f;
			const float rowW = width - gap * 2.f;
			const float cardW = rowW * 0.29f;
			DrawRateCard("Загрузка", m_cardDownloadBps, displayBits, UiCommon::FixedDownloadAccent(), cardW, cardH);
			ImGui::SameLine(0.f, gap);
			DrawRateCard("Отдача", m_cardUploadBps, displayBits, UiCommon::FixedUploadAccent(), cardW, cardH);
			ImGui::SameLine(0.f, gap);
			DrawSessionCard(bytesIn, bytesOut, colors, rowW - cardW * 2.f, cardH);
		};
		const float headerY = ImGui::GetCursorPosY();
		const char* title = "Мониторинг сети";
		const ImVec2 titleSize = ImGui::CalcTextSize(title);
		const ImVec2 titlePos = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddText({ titlePos.x, titlePos.y + 4.f }, ImGui::GetColorU32(colors.textPrimary), title);
		ImGui::Dummy({ titleSize.x, titleSize.y + 8.f });
		ImGui::SameLine(0.f, 8.f);
		ImGui::SetCursorPosY(headerY);
		const float toggleWidth = ImGui::CalcTextSize("МБ/с").x + ImGui::CalcTextSize("Мбит/с").x + 41.f;
		const float metricsWidth = ImGui::GetContentRegionAvail().x - toggleWidth - 8.f;
		const bool inlineMetrics = metricsWidth >= ImGui::GetFontSize() * 28.f;
		if (inlineMetrics)
		{
			drawMetrics(metricsWidth);
			ImGui::SameLine(0.f, 8.f);
			ImGui::SetCursorPosY(headerY);
		}
		if (DrawSpeedUnitToggle(bitsMode, colors) && m_settings)
		{
			displayBits = !bitsMode;
			m_settings->SetNetworkSpeedBits(displayBits);
		}
		if (!inlineMetrics)
		{
			ImGui::Dummy({ 0.f, 4.f });
			drawMetrics(ImGui::GetContentRegionAvail().x);
		}
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

#ifdef _DEBUG
		const float peakDown = m_traffic ? m_traffic->GetPeakDownloadBps() : 0.f;
		const float peakUp = m_traffic ? m_traffic->GetPeakUploadBps() : 0.f;
		const ImVec4 downAccent = UiCommon::FixedDownloadAccent();
		const ImVec4 upAccent = UiCommon::FixedUploadAccent();

		{
			ImFont* font = ImGui::GetFont();
			const float bigSize = ImGui::GetFontSize() * 1.55f;
			const std::string downText = FormatSpeed(downBps, displayBits);
			const std::string upText = FormatSpeed(upBps, displayBits);
			const std::string peakDownLabel = std::string("↓ пик ") + FormatSpeed(peakDown, displayBits);
			const std::string peakUpLabel = std::string("↑ пик ") + FormatSpeed(peakUp, displayBits);
			const ImVec2 downSz = font->CalcTextSizeA(bigSize, 1e9f, 0.f, downText.c_str());
			const ImVec2 upSz = font->CalcTextSizeA(bigSize, 1e9f, 0.f, upText.c_str());
			(void)upSz;
			const float colW = ImGui::GetContentRegionAvail().x * 0.5f;

			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			dl->AddText(font, bigSize, origin, ImGui::GetColorU32(downAccent), downText.c_str());
			dl->AddText(
				font,
				ImGui::GetFontSize() * 0.85f,
				{ origin.x, origin.y + downSz.y + 2.f },
				ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.95f)),
				peakDownLabel.c_str());

			dl->AddText(
				font,
				bigSize,
				{ origin.x + colW, origin.y },
				ImGui::GetColorU32(upAccent),
				upText.c_str());
			dl->AddText(
				font,
				ImGui::GetFontSize() * 0.85f,
				{ origin.x + colW, origin.y + downSz.y + 2.f },
				ImGui::GetColorU32(UiCommon::WithAlpha(colors.textMuted, 0.95f)),
				peakUpLabel.c_str());

			ImGui::Dummy({ ImGui::GetContentRegionAvail().x, downSz.y + ImGui::GetFontSize() + 10.f });
		}

		{
			char sessionLine[128] = {};
			snprintf(
				sessionLine,
				sizeof sessionLine,
				"Сессия  ↓ %s   ↑ %s",
				FormatBytes(bytesIn).c_str(),
				FormatBytes(bytesOut).c_str());
			UiCommon::CaptionText(sessionLine, colors, ImGui::GetContentRegionAvail().x);
		}
		ImGui::Dummy({ 0.f, 8.f });

		if (m_traffic)
			DrawSpeedGraph(*m_traffic, ImGui::GetContentRegionAvail().x, 260.f, displayBits, colors, theme.GetAccents(), 0.42f, 0.34f);
#else
		const ImVec4 downAccent = UiCommon::FixedDownloadAccent();
		const ImVec4 upAccent = UiCommon::FixedUploadAccent();

		DrawSpeedProgressBars(downBps, upBps, scaleMax, colors);

		if (m_traffic)
			DrawSpeedGraph(*m_traffic, ImGui::GetContentRegionAvail().x, 196.f, displayBits, colors, theme.GetAccents());
#endif

		ImGui::Dummy({ 0.f, 10.f });
		UiCommon::SectionHeader("Сетевые соединения по приложениям", colors);
		ImGui::Dummy({ 0.f, 6.f });

		static const std::vector<ProcessNetEntry> emptyProcesses;
		const auto& procs = m_processNet ? m_processNet->GetTopProcesses() : emptyProcesses;
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, UiMetrics::kCardRadius);
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, { 8.f, 5.f });
		const bool panelVisible = ImGui::BeginChild("##process_net_panel", { 0.f, 0.f },
			ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
		if (procs.empty())
		{
			if (panelVisible)
				ImGui::TextDisabled("Нет активных соединений");
		}
		else if (panelVisible && ImGui::BeginTable(
					 "##proc_net_autowidth",
					 6,
					 ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
					 | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_PadOuterX
					 | ImGuiTableFlags_Sortable))
		{
			ImGui::TableSetupColumn("Процесс", ImGuiTableColumnFlags_WidthStretch, 2.2f);
			const float sortSpace = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.x;
			float tcpWidth = ImGui::CalcTextSize("TCP").x + sortSpace;
			float udpWidth = ImGui::CalcTextSize("UDP").x + sortSpace;
			for (const auto& entry : procs)
			{
				tcpWidth = (std::max)(tcpWidth, ImGui::CalcTextSize(std::to_string(entry.tcpCount).c_str()).x);
				udpWidth = (std::max)(udpWidth, ImGui::CalcTextSize(std::to_string(entry.udpCount).c_str()).x);
			}
			ImGui::TableSetupColumn("TCP", ImGuiTableColumnFlags_WidthFixed, tcpWidth);
			ImGui::TableSetupColumn("UDP", ImGuiTableColumnFlags_WidthFixed, udpWidth);
			ImGui::TableSetupColumn("Приём", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("Отдача", ImGuiTableColumnFlags_WidthStretch, 1.f);
			ImGui::TableSetupColumn("Всего", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort
				| ImGuiTableColumnFlags_PreferSortDescending, 1.f);
			ImGui::TableHeadersRow();

			float maxTotal = 1.f;
			std::vector<const ProcessNetEntry*> sorted;
			sorted.reserve(procs.size());
			for (const ProcessNetEntry& e : procs)
			{
				sorted.push_back(&e);
				maxTotal = (std::max)(maxTotal, e.downloadBps + e.uploadBps);
			}
			if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0)
			{
				const auto spec = specs->Specs[0];
				// Rates change even when the selected sort column is unchanged.
				std::sort(sorted.begin(), sorted.end(), [spec](const auto* lhs, const auto* rhs) {
					const auto& a = *lhs;
					const auto& b = *rhs;
					const auto compare = [](const auto& x, const auto& y) { return (y < x) - (x < y); };
					int order = 0;
					switch (spec.ColumnIndex)
					{
					case 0: order = compare(a.name, b.name); break;
					case 1: order = compare(a.tcpCount, b.tcpCount); break;
					case 2: order = compare(a.udpCount, b.udpCount); break;
					case 3: order = compare(a.downloadBps, b.downloadBps); break;
					case 4: order = compare(a.uploadBps, b.uploadBps); break;
					case 5: order = compare(a.downloadBps + a.uploadBps, b.downloadBps + b.uploadBps); break;
					}
					if (order == 0)
						return a.pid < b.pid;
					return spec.SortDirection == ImGuiSortDirection_Descending ? order > 0 : order < 0;
				});
				specs->SpecsDirty = false;
			}

			for (const ProcessNetEntry* entry : sorted)
			{
				const ProcessNetEntry& e = *entry;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				char nameLine[160] = {};
				snprintf(nameLine, sizeof nameLine, "%s  ·  %u", e.name.c_str(), e.pid);
				ImGui::TextUnformatted(nameLine);
				const float total = e.downloadBps + e.uploadBps;
				const float barFrac = (std::clamp)(total / maxTotal, 0.f, 1.f);
				const ImVec2 p0 = ImGui::GetItemRectMin();
				const ImVec2 p1 = ImGui::GetItemRectMax();
				const float barH = 3.f;
				ImGui::GetWindowDrawList()->AddRectFilled(
					{ p0.x, p1.y + 1.f },
					{ p0.x + (p1.x - p0.x) * barFrac, p1.y + 1.f + barH },
					ImGui::GetColorU32(UiCommon::WithAlpha(downAccent, 0.55f)),
					1.5f);

				ImGui::TableNextColumn();
				ImGui::Text("%d", e.tcpCount);
				ImGui::TableNextColumn();
				ImGui::Text("%d", e.udpCount);
				ImGui::TableNextColumn();
				ImGui::TextColored(downAccent, "%s", FormatSpeed(e.downloadBps, displayBits).c_str());
				ImGui::TableNextColumn();
				ImGui::TextColored(upAccent, "%s", FormatSpeed(e.uploadBps, displayBits).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FormatSpeed(total, displayBits).c_str());
			}
			ImGui::EndTable();
		}
		ImGui::EndChild();
		ImGui::PopStyleVar(2);
	}
	UiCommon::EndCard();

	ImGui::PopStyleVar();
}
