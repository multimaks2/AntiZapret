#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/ui_home_page.h"

#include "app/app_settings.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "net/traffic_monitor.h"
#include "tgproxy/tg_ws_proxy_manager.h"
#include "ui/ui_common.h"
#include "ui/ui_vpn_page.h"
#include "vpn/vpn_manager.h"
#include "zapret/smart_strategy_engine.h"
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

	ImVec4 RunStatusColor(ZapretRunStatus status, const UiAccentColors& accents)
	{
		switch (status)
		{
		case ZapretRunStatus::Running: return accents.ok;
		case ZapretRunStatus::Starting: return accents.warn;
		case ZapretRunStatus::Stopped: return accents.fail;
		}
		return accents.fail;
	}

	std::string FormatSpeed(float bytesPerSec)
	{
		const float mbit = bytesPerSec * 8.f / 1000000.f;
		char buffer[64] = {};
		if (bytesPerSec < 1024.f)
		{
			snprintf(buffer, sizeof buffer, "%.0f B/s (%.2f Мбит/с)", bytesPerSec, mbit);
			return buffer;
		}
		if (bytesPerSec < 1024.f * 1024.f)
		{
			snprintf(buffer, sizeof buffer, "%.1f KB/s (%.1f Мбит/с)", bytesPerSec / 1024.f, mbit);
			return buffer;
		}
		snprintf(
			buffer,
			sizeof buffer,
			"%.2f MB/s (%.1f Мбит/с)",
			bytesPerSec / (1024.f * 1024.f),
			mbit);
		return buffer;
	}

	std::string FormatSpeedPrimary(float bytesPerSec)
	{
		char buffer[32] = {};
		if (bytesPerSec < 1024.f)
			snprintf(buffer, sizeof buffer, "%.0f B/s", bytesPerSec);
		else if (bytesPerSec < 1024.f * 1024.f)
			snprintf(buffer, sizeof buffer, "%.1f KB/s", bytesPerSec / 1024.f);
		else
			snprintf(buffer, sizeof buffer, "%.2f MB/s", bytesPerSec / (1024.f * 1024.f));
		return buffer;
	}

	std::string FormatSpeedMbit(float bytesPerSec)
	{
		char buffer[32] = {};
		const float mbit = bytesPerSec * 8.f / 1000000.f;
		if (mbit < 10.f)
			snprintf(buffer, sizeof buffer, "%.2f Мбит/с", mbit);
		else if (mbit < 100.f)
			snprintf(buffer, sizeof buffer, "%.1f Мбит/с", mbit);
		else
			snprintf(buffer, sizeof buffer, "%.0f Мбит/с", mbit);
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

	std::string FormatAxisSpeed(float bps)
	{
		if (bps < 1024.f)
		{
			char buffer[16] = {};
			snprintf(buffer, sizeof buffer, "%.0f B", bps);
			return buffer;
		}
		if (bps < 1024.f * 1024.f)
		{
			char buffer[16] = {};
			const float kb = bps / 1024.f;
			if (kb >= 10.f)
				snprintf(buffer, sizeof buffer, "%.0f K", kb);
			else
				snprintf(buffer, sizeof buffer, "%.1f K", kb);
			return buffer;
		}
		char buffer[16] = {};
		const float mb = bps / (1024.f * 1024.f);
		if (mb >= 10.f)
			snprintf(buffer, sizeof buffer, "%.0f M", mb);
		else
			snprintf(buffer, sizeof buffer, "%.1f M", mb);
		return buffer;
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

		const float fromNewest = ageSlots - scrollPhase;
		if (fromNewest <= 0.f)
		{
			const float newest = SampleValue(monitor, download, count - 1);
			const float t = scrollPhase > 0.001f ? (ageSlots / scrollPhase) : 0.f;
			return liveValue + (newest - liveValue) * (std::clamp)(t, 0.f, 1.f);
		}

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

	ImVec2 CatmullRom(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t)
	{
		const float t2 = t * t;
		const float t3 = t2 * t;
		return {
			0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t + (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2
				+ (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3),
			0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t + (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2
				+ (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3),
		};
	}

	void ClipSeriesToLeft(std::vector<ImVec2>& points, float graphLeft)
	{
		if (points.size() < 2)
			return;

		size_t firstVisible = 0;
		while (firstVisible < points.size() && points[firstVisible].x < graphLeft)
			++firstVisible;

		if (firstVisible == 0)
			return;

		if (firstVisible >= points.size())
		{
			points.clear();
			return;
		}

		const ImVec2& a = points[firstVisible - 1];
		const ImVec2& b = points[firstVisible];
		const float span = b.x - a.x;
		float y = b.y;
		if (std::fabs(span) > 0.001f)
			y = a.y + (b.y - a.y) * ((graphLeft - a.x) / span);

		points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(firstVisible));
		points.insert(points.begin(), ImVec2{ graphLeft, y });
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
			// Keep one slot past the left so ClipSeriesToLeft can interpolate the edge.
			if (x < graphLeft - slotW || x > graphRight + slotW)
				return;
			const float norm = (std::clamp)(value / scaleMax, 0.f, 1.f);
			outPoints.push_back({ x, graphBottom - norm * graphH });
		};

		// Sliding timeline (stable). 250ms delay is only for the text cards, not here.
		pushPoint(0.f, liveValue);
		for (size_t rev = 0; rev + 1 < TrafficMonitor::kHistorySize; ++rev)
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

		ClipSeriesToLeft(outPoints, graphLeft);
	}

	void TessellateSmooth(
		const std::vector<ImVec2>& control,
		std::vector<ImVec2>& outSmooth,
		int segmentsPerSpan = 6)
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
			const ImVec2& p0 = control[i == 0 ? 0 : i - 1];
			const ImVec2& p1 = control[i];
			const ImVec2& p2 = control[i + 1];
			const ImVec2& p3 = control[i + 2 < control.size() ? i + 2 : control.size() - 1];
			const float yMin = (std::min)(p1.y, p2.y);
			const float yMax = (std::max)(p1.y, p2.y);

			for (int s = 0; s < segmentsPerSpan; ++s)
			{
				const float t = static_cast<float>(s) / static_cast<float>(segmentsPerSpan);
				ImVec2 pt = CatmullRom(p0, p1, p2, p3, t);
				pt.y = (std::clamp)(pt.y, yMin, yMax);
				outSmooth.push_back(pt);
			}
		}
		outSmooth.push_back(control.back());
	}

	void DrawSeriesArea(
		ImDrawList* drawList,
		const std::vector<ImVec2>& controlPoints,
		const std::vector<ImVec2>& smoothPoints,
		float graphBottom,
		ImU32 lineColor,
		ImU32 fillColor)
	{
		if (smoothPoints.size() < 2)
			return;

		if (controlPoints.size() >= 2)
		{
			const ImDrawListFlags backupFlags = drawList->Flags;
			drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
			for (size_t i = 0; i + 1 < controlPoints.size(); ++i)
			{
				const ImVec2& a = controlPoints[i];
				const ImVec2& b = controlPoints[i + 1];
				drawList->AddQuadFilled(
					a,
					b,
					{ b.x, graphBottom },
					{ a.x, graphBottom },
					fillColor);
			}
			drawList->Flags = backupFlags;
		}

		drawList->AddPolyline(
			smoothPoints.data(),
			static_cast<int>(smoothPoints.size()),
			lineColor,
			ImDrawFlags_None,
			2.25f);
	}

	void DrawSpeedGraph(
		const TrafficMonitor& monitor,
		float width,
		float height,
		const UiThemeColors& colors,
		const UiAccentColors& accents)
	{
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 rectMax = { origin.x + width, origin.y + height };
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		drawList->AddRectFilled(origin, rectMax, ImGui::GetColorU32(colors.inputBg), UiMetrics::kCardRadius);

		const float scaleMax = (std::max)(monitor.GetDisplayScaleMax(), 1024.f);
		const float scrollPhase = (std::clamp)(monitor.GetScrollPhase(), 0.f, 1.f);
		const float labelWidth = 42.f;
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
			const std::string label = FormatAxisSpeed(speed);
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
			monitor.GetDisplayDownloadBps(),
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
			monitor.GetDisplayUploadBps(),
			upControl);

		std::vector<ImVec2> downSmooth;
		std::vector<ImVec2> upSmooth;
		TessellateSmooth(downControl, downSmooth);
		TessellateSmooth(upControl, upSmooth);

		drawList->PushClipRect({ graphLeft, graphTop }, { graphRight, graphBottom }, true);
		DrawSeriesArea(
			drawList,
			downControl,
			downSmooth,
			graphBottom,
			ImGui::GetColorU32(accents.download),
			ImGui::GetColorU32(UiCommon::WithAlpha(accents.download, 0.18f)));
		DrawSeriesArea(
			drawList,
			upControl,
			upSmooth,
			graphBottom,
			ImGui::GetColorU32(accents.upload),
			ImGui::GetColorU32(UiCommon::WithAlpha(accents.upload, 0.12f)));

		if (!downSmooth.empty())
			drawList->AddCircleFilled(downSmooth.back(), 3.2f, ImGui::GetColorU32(accents.download), 12);
		if (!upSmooth.empty())
			drawList->AddCircleFilled(upSmooth.back(), 3.2f, ImGui::GetColorU32(accents.upload), 12);

		// Hover scrubber.
		ImGui::SetCursorScreenPos({ graphLeft, graphTop });
		ImGui::InvisibleButton("##speed_graph_hover", { graphW, graphH });
		const bool hovered = ImGui::IsItemHovered();
		if (hovered)
		{
			const float mouseX = (std::clamp)(ImGui::GetIO().MousePos.x, graphLeft, graphRight);
			const float ageSlots = (graphRight - mouseX) / (graphW / static_cast<float>(TrafficMonitor::kHistorySize - 1));
			const float ageSec = ageSlots * TrafficMonitor::kSampleIntervalSec;
			const float downHover = ValueAtAgeSlots(
				monitor,
				true,
				monitor.GetDisplayDownloadBps(),
				scrollPhase,
				ageSlots);
			const float upHover = ValueAtAgeSlots(
				monitor,
				false,
				monitor.GetDisplayUploadBps(),
				scrollPhase,
				ageSlots);

			const ImU32 scrubColor = ImGui::GetColorU32(UiCommon::WithAlpha(colors.textPrimary, 0.55f));
			drawList->AddLine({ mouseX, graphTop }, { mouseX, graphBottom }, scrubColor, 1.25f);

			const float downY = graphBottom - (std::clamp)(downHover / scaleMax, 0.f, 1.f) * graphH;
			const float upY = graphBottom - (std::clamp)(upHover / scaleMax, 0.f, 1.f) * graphH;
			drawList->AddCircleFilled({ mouseX, downY }, 3.5f, ImGui::GetColorU32(accents.download), 12);
			drawList->AddCircleFilled({ mouseX, upY }, 3.5f, ImGui::GetColorU32(accents.upload), 12);

			char tip[160] = {};
			snprintf(
				tip,
				sizeof tip,
				"%s\n↓ %s\n↑ %s",
				FormatRelativeTime(ageSec).c_str(),
				FormatSpeed(downHover).c_str(),
				FormatSpeed(upHover).c_str());
			ImGui::SetTooltip("%s", tip);
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

	void DrawRateCard(
		const char* title,
		float bytesPerSec,
		ImVec4 accent,
		const UiThemeColors& colors,
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

		drawList->AddText(
			{ pos.x + 12.f, pos.y + 8.f },
			ImGui::GetColorU32(UiCommon::WithAlpha(accent, 0.95f)),
			title);

		const std::string primary = FormatSpeedPrimary(bytesPerSec);
		const std::string mbit = FormatSpeedMbit(bytesPerSec);
		drawList->AddText(
			{ pos.x + 12.f, pos.y + 8.f + ImGui::GetTextLineHeight() + 4.f },
			ImGui::GetColorU32(accent),
			primary.c_str());
		drawList->AddText(
			{ pos.x + 12.f, pos.y + 8.f + ImGui::GetTextLineHeight() * 2.f + 6.f },
			ImGui::GetColorU32(colors.textMuted),
			mbit.c_str());

		ImGui::Dummy({ width, height });
	}

	void DrawSessionCard(
		std::uint64_t bytesIn,
		std::uint64_t bytesOut,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
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

		drawList->AddText(
			{ pos.x + 12.f, pos.y + 8.f },
			ImGui::GetColorU32(colors.textMuted),
			"Сессия");

		const float midX = pos.x + width * 0.5f;
		const float labelY = pos.y + 8.f + ImGui::GetTextLineHeight() + 4.f;
		const float valueY = labelY + ImGui::GetTextLineHeight() + 2.f;

		drawList->AddText(
			{ pos.x + 12.f, labelY },
			ImGui::GetColorU32(UiCommon::WithAlpha(accents.download, 0.9f)),
			"Загружено");
		drawList->AddText(
			{ pos.x + 12.f, valueY },
			ImGui::GetColorU32(accents.download),
			FormatBytes(bytesIn).c_str());

		drawList->AddText(
			{ midX + 6.f, labelY },
			ImGui::GetColorU32(UiCommon::WithAlpha(accents.upload, 0.9f)),
			"Отдано");
		drawList->AddText(
			{ midX + 6.f, valueY },
			ImGui::GetColorU32(accents.upload),
			FormatBytes(bytesOut).c_str());

		ImGui::Dummy({ width, height });
	}

	void DrawSpeedProgressBars(
		float downBps,
		float upBps,
		float scaleMaxBytesPerSec,
		const UiThemeColors& colors,
		const UiAccentColors& accents)
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
				ImGui::GetColorU32(UiCommon::WithAlpha(color, 0.8f)),
				3.f);
			ImGui::Dummy({ barW, barH + 5.f });
		};

		drawBar(downBps, accents.download);
		drawBar(upBps, accents.upload);
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
	TrafficMonitor* traffic)
{
	m_zapret = zapret;
	m_tgProxy = tgProxy;
	m_vpn = vpn;
	m_vpnPage = vpnPage;
	m_settings = settings;
	m_traffic = traffic;
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
		{
			m_selectedStrategy = m_zapret->GetPreferredStrategyIndex(m_autoSelect);
			m_hasSmartStrategy = m_zapret->IsSmartStrategyEnabled();
		}
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
		0xE80F,
		"Главная",
		"Быстрое управление и обзор состояния",
		colors);

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
					ZapretStrategies::GetStrategyLabel(testingIndex).data(),
					current,
					total);
			}
			else
				snprintf(buf, sizeof buf, "Тестирование... (%d/%d)", current, total);
			strategyNote = buf;
		}
	}
	else if (azRunning && m_zapret)
	{
		if (m_zapret->IsActiveSmartStrategy())
			strategyNote = std::string("Стратегия: ") + SmartStrategyEngine::kLabel;
		else if (activeStrategy >= 0)
			strategyNote = std::string("Стратегия: ") + ZapretStrategies::GetStrategyLabel(activeStrategy).data();
	}

	const char* azStatusText = RunStatusLabel(azStatus);
	ImVec4 azStatusColor = RunStatusColor(azStatus, accents);
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
		"Обход Discord, YouTube",
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
				const ImVec4& accent = strategyTestRunning ? accents.fail : accents.download;
				if (UiCommon::AccentButton(
					label,
					{ btnW, UiMetrics::kBtnHeight },
					accent,
					colors,
					m_zapret && !m_zapret->IsOperationInFlight()) && m_zapret)
				{
					m_zapret->HandleStrategyTestButton(ZapretStrategies::GameFilterMode::Disabled);
				}
			}
			else if (azRunning)
			{
				if (UiCommon::AccentButton("Остановить", { btnW, UiMetrics::kBtnHeight }, accents.fail, colors) && m_zapret)
					m_zapret->RequestStop();
			}
			else if (UiCommon::AccentButton(
				"Запустить",
				{ btnW, UiMetrics::kBtnHeight },
				accents.download,
				colors,
				m_zapret && !m_zapret->IsOperationInFlight()) && m_zapret)
			{
				const bool smartActive = m_hasSmartStrategy
					&& m_zapret->GetStore().GetLastStrategy() == SmartStrategyEngine::kLabel;
				const bool firstRun = m_zapret->GetStore().GetResults().empty();
				if (smartActive)
					m_zapret->RequestStartSmartStrategy(ZapretStrategies::GameFilterMode::Disabled);
				else if (firstRun)
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
		tgRunning ? accents.ok : accents.fail,
		nullptr,
		colors,
		[&](float btnW)
		{
			const char* label = m_tgProxy ? m_tgProxy->GetPrimaryActionLabel() : "Запустить";
			const bool canAction = m_tgProxy && m_tgProxy->CanPrimaryAction();
			const ImVec4 btnColor = tgRunning ? accents.fail : accents.download;
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
		vpnRunning ? accents.ok : (vpnEnabled ? accents.warn : accents.fail),
		nullptr,
		colors,
		[&](float btnW)
		{
			if (vpnRunning || vpnEnabled)
			{
				if (UiCommon::AccentButton("Отключить VPN", { btnW, UiMetrics::kBtnHeight }, accents.fail, colors) && m_vpnPage)
					m_vpnPage->SetVpnEnabled(false);
			}
			else if (UiCommon::AccentButton(
				"Подключить VPN",
				{ btnW, UiMetrics::kBtnHeight },
				accents.download,
				colors,
				m_vpnPage && m_vpnPage->HasActiveServer()) && m_vpnPage)
			{
				m_vpnPage->SetVpnEnabled(true);
			}
		});

	UiCommon::CardGap();

	if (UiCommon::BeginCard("##home_traffic", width, colors))
	{
		UiCommon::SectionHeader("Мониторинг сети", colors);
		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });

		const float downBps = m_traffic ? m_traffic->GetDisplayDownloadBps() : 0.f;
		const float upBps = m_traffic ? m_traffic->GetDisplayUploadBps() : 0.f;
		const std::uint64_t bytesIn = m_traffic ? m_traffic->GetSessionBytesIn() : 0;
		const std::uint64_t bytesOut = m_traffic ? m_traffic->GetSessionBytesOut() : 0;
		const float scaleMax = m_traffic ? (std::max)(m_traffic->GetDisplayScaleMax(), 1.f) : 1.f;

		m_cardSampleTimer += ImGui::GetIO().DeltaTime;
		constexpr float kCardSampleIntervalSec = 0.25f;
		if (!m_cardSampleReady || m_cardSampleTimer >= kCardSampleIntervalSec)
		{
			m_cardSampleTimer = 0.f;
			m_cardDownloadBps = downBps;
			m_cardUploadBps = upBps;
			m_cardSampleReady = true;
		}

		const float gap = 8.f;
		const float cardH = ImGui::GetTextLineHeight() * 3.f + 20.f;
		const float cardW = (ImGui::GetContentRegionAvail().x - gap * 2.f) / 3.f;
		DrawRateCard("Загрузка", m_cardDownloadBps, accents.download, colors, cardW, cardH);
		ImGui::SameLine(0.f, gap);
		DrawRateCard("Отдача", m_cardUploadBps, accents.upload, colors, cardW, cardH);
		ImGui::SameLine(0.f, gap);
		DrawSessionCard(bytesIn, bytesOut, colors, accents, cardW, cardH);

		ImGui::Dummy({ 0.f, 8.f });
		DrawSpeedProgressBars(downBps, upBps, scaleMax, colors, accents);

		if (m_traffic)
			DrawSpeedGraph(*m_traffic, ImGui::GetContentRegionAvail().x, 196.f, colors, accents);
	}
	UiCommon::EndCard();

	ImGui::PopStyleVar();
}
