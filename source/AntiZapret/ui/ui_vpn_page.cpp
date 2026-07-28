#include "ui/ui_vpn_page.h"

#include "app/app_log.h"
#include "gfx/font_manager.h"
#include "gfx/theme_manager.h"
#include "ui/ui_common.h"
#include "vpn/vpn_config_builder.h"
#include "vpn/vpn_flag_icons.h"
#include "vpn/vpn_geo.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_manager.h"
#include "vpn/vpn_mihomo_api.h"
#include "vpn/vpn_module_update_apply.h"
#include "vpn/vpn_module_update_check.h"
#include "vpn/vpn_node_probe.h"
#include "vpn/vpn_routing.h"
#include "zapret/zapret_paths.h"
#include "zapret/zapret_update_check.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <Windows.h>
#include <Shellapi.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
	const char* kWorkModes[] = {
		"RUv1- Заблокированное",
		"RUv1- Все, кроме рф",
		"RUv1- Все",
		"Своя Маршрутизация",
	};

	const char* kTransportModes[] = {
		"Режим - Proxy",
		"Режим - Tunnel",
	};

	// Store: 1=Blocked … 4=Custom (0 — старый мёртвый «региональные прессеты»).
	int WorkModeToUiIndex(int workMode)
	{
		if (workMode < 1 || workMode > 4)
			return 0;
		return workMode - 1;
	}

	int UiIndexToWorkMode(int uiIndex)
	{
		if (uiIndex < 0 || uiIndex > 3)
			return 1;
		return uiIndex + 1;
	}

	constexpr float kColNum = 30.f;

	const char* StatusText(int alive)
	{
		if (alive < 0)
			return "--";
		if (alive > 0)
			return "OK";
		return "X";
	}

	ImVec4 AliveColor(int alive, const UiAccentColors& accents)
	{
		if (alive < 0)
			return accents.warn;
		if (alive > 0)
			return accents.ok;
		return accents.fail;
	}

	const char* FormatPing(int pingMs, char* buffer, int bufferSize)
	{
		if (pingMs < 0)
		{
			snprintf(buffer, static_cast<size_t>(bufferSize), "--");
			return buffer;
		}
		snprintf(buffer, static_cast<size_t>(bufferSize), "%d ms", pingMs);
		return buffer;
	}

	const char* FormatSpeed(float speedMbps, char* buffer, int bufferSize)
	{
		if (speedMbps < 0.f)
		{
			snprintf(buffer, static_cast<size_t>(bufferSize), "--");
			return buffer;
		}
		snprintf(buffer, static_cast<size_t>(bufferSize), "%.1f MB/s", speedMbps);
		return buffer;
	}

	ImVec4 LerpVec4(const ImVec4& a, const ImVec4& b, float t)
	{
		t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
		return ImVec4(
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t,
			a.w + (b.w - a.w) * t);
	}

	void AppendUtf8Lower(std::string& out, unsigned char lead, const char*& p)
	{
		// ASCII
		if (lead < 0x80)
		{
			out.push_back(static_cast<char>(std::tolower(lead)));
			++p;
			return;
		}

		// 2-byte UTF-8 (covers Cyrillic)
		if ((lead & 0xE0) == 0xC0 && p[1])
		{
			const unsigned char b2 = static_cast<unsigned char>(p[1]);
			unsigned int cp = (static_cast<unsigned int>(lead & 0x1F) << 6)
				| static_cast<unsigned int>(b2 & 0x3F);
			if (cp >= 0x410 && cp <= 0x42F) // А-Я
				cp += 0x20;
			else if (cp == 0x401) // Ё
				cp = 0x451;
			out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			p += 2;
			return;
		}

		// Pass through other UTF-8 sequences unchanged.
		const int len = (lead & 0xF0) == 0xF0 ? 4 : (lead & 0xE0) == 0xE0 ? 3 : 2;
		for (int i = 0; i < len && p[i]; ++i)
			out.push_back(p[i]);
		p += len;
	}

	std::string ToSearchLower(const char* text)
	{
		std::string out;
		if (!text || !text[0])
			return out;
		out.reserve(std::strlen(text));
		const char* p = text;
		while (*p)
			AppendUtf8Lower(out, static_cast<unsigned char>(*p), p);
		return out;
	}

	bool ContainsIgnoreCase(const char* haystack, const std::string& needleLower)
	{
		if (needleLower.empty())
			return true;
		if (!haystack || !haystack[0])
			return false;
		const std::string hayLower = ToSearchLower(haystack);
		return hayLower.find(needleLower) != std::string::npos;
	}

	const char* DisplayGroupName(const std::string& groupName)
	{
		if (groupName.empty() || groupName == "Imported")
			return "Моё импортированное";
		return groupName.c_str();
	}

	bool MatchesSearch(const VpnNode& node, const char* query)
	{
		if (!query || !query[0])
			return true;

		const std::string needle = ToSearchLower(query);
		if (needle.empty())
			return true;

		char portBuf[16];
		snprintf(portBuf, sizeof portBuf, "%d", node.port);

		const std::string countryName = VpnGeo::CountryCodeToName(node.country);
		const std::string groupKey = node.group.empty() ? "Imported" : node.group;
		const char* groupDisplay = DisplayGroupName(groupKey);

		return ContainsIgnoreCase(node.name.c_str(), needle)
			|| ContainsIgnoreCase(node.scheme.c_str(), needle)
			|| ContainsIgnoreCase(node.server.c_str(), needle)
			|| ContainsIgnoreCase(node.group.c_str(), needle)
			|| ContainsIgnoreCase(groupKey.c_str(), needle)
			|| ContainsIgnoreCase(groupDisplay, needle)
			|| ContainsIgnoreCase(node.tags.c_str(), needle)
			|| ContainsIgnoreCase(portBuf, needle)
			|| ContainsIgnoreCase(node.country.c_str(), needle)
			|| ContainsIgnoreCase(countryName.c_str(), needle);
	}

	bool GroupMatchesSearch(const std::string& groupName, const char* query)
	{
		if (!query || !query[0])
			return true;
		const std::string needle = ToSearchLower(query);
		if (needle.empty())
			return true;
		return ContainsIgnoreCase(groupName.c_str(), needle)
			|| ContainsIgnoreCase(DisplayGroupName(groupName), needle);
	}

	bool ToolbarIconButton(
		FontManager& fonts,
		uint32_t iconCode,
		const char* tooltip,
		const UiThemeColors& colors,
		bool enabled = true)
	{
		return UiCommon::IconToolButton(fonts, iconCode, tooltip, tooltip, colors, ImVec2(30.f, 30.f), enabled);
	}

	bool HeaderIconButton(
		FontManager& fonts,
		uint32_t iconCode,
		const char* id,
		const char* tooltip,
		const UiThemeColors& colors,
		ImVec2 size,
		bool enabled = true)
	{
		if (!enabled)
			ImGui::BeginDisabled();

		ImGui::PushID(id);
		// Same fill as CollapsingHeader strip (navActive).
		ImGui::PushStyleColor(ImGuiCol_Button, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.navHover);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.navActive);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);

		const bool pressed = ImGui::Button("##hdr_icon", size);

		wchar_t wide[] = { static_cast<wchar_t>(iconCode), 0 };
		char utf8[8] = {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, wide, 1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
		if (len > 0)
		{
			ImFont* iconFont = fonts.GetIconFont();
			if (!iconFont)
				iconFont = ImGui::GetFont();

			const float iconPx = (std::max)(10.f, size.y * 0.52f);
			const ImVec2 glyphSize = iconFont->CalcTextSizeA(iconPx, FLT_MAX, 0.f, utf8);
			const ImVec2 rectMin = ImGui::GetItemRectMin();
			const ImVec2 rectMax = ImGui::GetItemRectMax();
			// Segoe MDL2 glyphs sit optically high-left in their metrics box — nudge to true center.
			const ImVec2 glyphPos(
				rectMin.x + ((rectMax.x - rectMin.x) - glyphSize.x) * 0.5f + iconPx * 0.08f,
				rectMin.y + ((rectMax.y - rectMin.y) - glyphSize.y) * 0.5f + iconPx * 0.10f);
			const ImU32 glyphColor = ImGui::GetColorU32(
				enabled ? colors.textPrimary : colors.textMuted);
			ImGui::GetWindowDrawList()->AddText(iconFont, iconPx, glyphPos, glyphColor, utf8);
		}

		if (tooltip)
			UiCommon::SetItemTooltip("%s", tooltip);

		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor(3);
		ImGui::PopID();

		if (!enabled)
			ImGui::EndDisabled();
		return pressed;
	}

	void DrawCountryFlagCell(const std::string& countryCode, float rowContentH)
	{
		const std::string normalized = countryCode.size() == 2 ? countryCode : std::string {};
		const std::string countryName = normalized.empty() ? std::string {} : VpnGeo::CountryCodeToName(normalized);
		const ImTextureID flagTexture = VpnFlagIcons::Instance().GetFlagTexture(countryCode);
		if (flagTexture != 0)
		{
			constexpr float kFlagHeight = 12.f;
			const ImVec2 flagSize = VpnFlagIcons::Instance().GetFlagDrawSize(countryCode, kFlagHeight);
			const ImVec2 drawSize = flagSize.x > 0.f ? flagSize : ImVec2(16.f, kFlagHeight);
			const float offsetY = (rowContentH - drawSize.y) * 0.5f;
			if (offsetY > 0.f)
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
			const float offsetX = (ImGui::GetColumnWidth() - drawSize.x) * 0.5f;
			if (offsetX > 0.f)
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
			ImGui::Image(ImTextureRef(flagTexture), drawSize);
			if (!countryName.empty())
				UiCommon::SetItemTooltip("%s", countryName.c_str());
			return;
		}

		if (!normalized.empty())
			VpnFlagIcons::Instance().RequestFlag(countryCode);

		UiCommon::TableTextAligned(
			normalized.empty() ? "—" : normalized.c_str(),
			UiCommon::UiTableAlign::Center);
		if (!normalized.empty() && !countryName.empty())
			UiCommon::SetItemTooltip("%s", countryName.c_str());
	}

	std::mutex g_dnsMutex;
	std::unordered_map<std::string, std::string> g_dnsCache;
	std::unordered_set<std::string> g_dnsInFlight;

	bool IsHostnameForTooltip(const std::string& server)
	{
		if (server.empty())
			return false;
		if (VpnGeo::IsPublicIp(server))
			return false;
		// Skip raw IPv6 literals.
		if (server.find(':') != std::string::npos)
			return false;
		return true;
	}

	void RequestHostIpResolve(const std::string& host)
	{
		{
			std::lock_guard<std::mutex> lock(g_dnsMutex);
			if (g_dnsCache.count(host) > 0 || g_dnsInFlight.count(host) > 0)
				return;
			g_dnsInFlight.insert(host);
		}

		std::thread([host]()
		{
			const std::string resolvedIp = VpnNodeProbe::ResolveHostIpv4(host);
			std::lock_guard<std::mutex> lock(g_dnsMutex);
			g_dnsCache[host] = resolvedIp; // empty string = failed
			g_dnsInFlight.erase(host);
		}).detach();
	}

	void DrawServerHostCell(const std::string& server)
	{
		ImGui::TextUnformatted(server.c_str());
		if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || !IsHostnameForTooltip(server))
			return;

		RequestHostIpResolve(server);

		std::string tip;
		bool inFlight = false;
		{
			std::lock_guard<std::mutex> lock(g_dnsMutex);
			const auto it = g_dnsCache.find(server);
			if (it != g_dnsCache.end())
			{
				if (it->second.empty())
					tip = "IP: не удалось определить";
				else
					tip = "IP: " + it->second;
			}
			else
			{
				inFlight = g_dnsInFlight.count(server) > 0;
			}
		}

	if (tip.empty())
		tip = inFlight ? "IP: резолв…" : "IP: …";
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			UiCommon::ShowTooltip("%s", tip.c_str());
	}

	std::string FormatSubscriptionRemaining(long long expireUnix)
	{
		if (expireUnix <= 0)
			return {};

		const long long now = static_cast<long long>(std::time(nullptr));
		const long long left = expireUnix - now;
		char buf[64] = {};
		if (left <= 0)
		{
			snprintf(buf, sizeof buf, "истекла");
			return buf;
		}

		const long long days = left / 86400;
		if (days >= 1)
		{
			snprintf(buf, sizeof buf, "осталось %lld дн.", days);
			return buf;
		}
		const long long hours = left / 3600;
		if (hours >= 1)
		{
			snprintf(buf, sizeof buf, "осталось %lld ч.", hours);
			return buf;
		}
		const long long mins = (std::max)(1LL, left / 60);
		snprintf(buf, sizeof buf, "осталось %lld мин.", mins);
		return buf;
	}

	std::string FormatBytesShort(long long bytes)
	{
		if (bytes < 0)
			bytes = 0;
		char buf[48] = {};
		const double b = static_cast<double>(bytes);
		if (b >= 1024.0 * 1024.0 * 1024.0)
			snprintf(buf, sizeof buf, "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
		else if (b >= 1024.0 * 1024.0)
			snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
		else if (b >= 1024.0)
			snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
		else
			snprintf(buf, sizeof buf, "%lld B", bytes);
		return buf;
	}

	std::string FormatBytesRu(long long bytes)
	{
		std::string s = FormatBytesShort(bytes);
		for (char& ch : s)
		{
			if (ch == '.')
				ch = ',';
		}
		return s;
	}

	bool Utf8Next(const char*& p, const char* end, uint32_t& outCp)
	{
		if (!p || p >= end)
			return false;
		const unsigned char c0 = static_cast<unsigned char>(*p);
		if (c0 < 0x80)
		{
			outCp = c0;
			++p;
			return true;
		}
		if ((c0 & 0xE0) == 0xC0 && p + 1 < end)
		{
			outCp = (static_cast<uint32_t>(c0 & 0x1F) << 6)
				| (static_cast<unsigned char>(p[1]) & 0x3F);
			p += 2;
			return true;
		}
		if ((c0 & 0xF0) == 0xE0 && p + 2 < end)
		{
			outCp = (static_cast<uint32_t>(c0 & 0x0F) << 12)
				| ((static_cast<unsigned char>(p[1]) & 0x3F) << 6)
				| (static_cast<unsigned char>(p[2]) & 0x3F);
			p += 3;
			return true;
		}
		if ((c0 & 0xF8) == 0xF0 && p + 3 < end)
		{
			outCp = (static_cast<uint32_t>(c0 & 0x07) << 18)
				| ((static_cast<unsigned char>(p[1]) & 0x3F) << 12)
				| ((static_cast<unsigned char>(p[2]) & 0x3F) << 6)
				| (static_cast<unsigned char>(p[3]) & 0x3F);
			p += 4;
			return true;
		}
		outCp = 0xFFFD;
		++p;
		return true;
	}

	bool IsAnnounceEmoji(uint32_t cp)
	{
		return cp == 0x231B || cp == 0x1F464 || cp == 0x1F4C4 || cp == 0x2757 || cp == 0x26A0
			|| cp == 0x1F503 || cp == 0x2B50 || cp == 0x2605;
	}

	void DrawAnnounceEmojiIcon(ImDrawList* dl, ImVec2 center, float size, uint32_t cp)
	{
		const float r = size * 0.48f;
		switch (cp)
		{
		case 0x2B50: // star
		case 0x2605:
		{
			const ImU32 col = IM_COL32(255, 193, 7, 255);
			const float s = r * 0.95f;
			for (int i = 0; i < 5; ++i)
			{
				const float a0 = -1.5707963f + i * 6.2831853f / 5.f;
				const float a1 = a0 + 3.1415926f / 5.f;
				const float a2 = a0 + 6.2831853f / 5.f;
				const ImVec2 outer0 = { center.x + cosf(a0) * s, center.y + sinf(a0) * s };
				const ImVec2 inner = { center.x + cosf(a1) * s * 0.42f, center.y + sinf(a1) * s * 0.42f };
				const ImVec2 outer1 = { center.x + cosf(a2) * s, center.y + sinf(a2) * s };
				dl->AddTriangleFilled(outer0, inner, center, col);
				dl->AddTriangleFilled(inner, outer1, center, col);
			}
			break;
		}
		case 0x231B: // hourglass
		{
			const ImU32 col = IM_COL32(255, 193, 7, 255);
			const ImU32 sand = IM_COL32(255, 235, 59, 255);
			dl->AddTriangleFilled(
				{ center.x - r * 0.55f, center.y - r * 0.75f },
				{ center.x + r * 0.55f, center.y - r * 0.75f },
				{ center.x, center.y - r * 0.05f },
				col);
			dl->AddTriangleFilled(
				{ center.x - r * 0.55f, center.y + r * 0.75f },
				{ center.x + r * 0.55f, center.y + r * 0.75f },
				{ center.x, center.y + r * 0.05f },
				col);
			dl->AddCircleFilled(center, r * 0.18f, sand, 12);
			break;
		}
		case 0x1F464: // person
		{
			const ImU32 col = IM_COL32(100, 181, 246, 255);
			dl->AddCircleFilled({ center.x, center.y - r * 0.28f }, r * 0.34f, col, 16);
			dl->PathClear();
			dl->PathArcTo({ center.x, center.y + r * 0.95f }, r * 0.7f, 3.55f, 5.88f, 14);
			dl->PathFillConvex(col);
			break;
		}
		case 0x1F4C4: // document
		{
			const ImU32 paper = IM_COL32(224, 224, 224, 255);
			const ImU32 line = IM_COL32(120, 120, 120, 255);
			const ImVec2 a = { center.x - r * 0.55f, center.y - r * 0.7f };
			const ImVec2 b = { center.x + r * 0.35f, center.y + r * 0.7f };
			dl->AddRectFilled(a, b, paper, 2.f);
			dl->AddTriangleFilled(
				{ center.x + r * 0.05f, center.y - r * 0.7f },
				{ center.x + r * 0.55f, center.y - r * 0.7f },
				{ center.x + r * 0.05f, center.y - r * 0.2f },
				IM_COL32(180, 180, 180, 255));
			for (int i = 0; i < 3; ++i)
			{
				const float yy = center.y - r * 0.15f + i * r * 0.28f;
				dl->AddLine(
					{ center.x - r * 0.35f, yy },
					{ center.x + r * 0.15f, yy },
					line,
					1.2f);
			}
			break;
		}
		case 0x2757: // exclamation
		{
			const ImU32 col = IM_COL32(244, 67, 54, 255);
			dl->AddRectFilled(
				{ center.x - r * 0.18f, center.y - r * 0.75f },
				{ center.x + r * 0.18f, center.y + r * 0.25f },
				col,
				2.f);
			dl->AddCircleFilled({ center.x, center.y + r * 0.55f }, r * 0.2f, col, 12);
			break;
		}
		case 0x26A0: // warning
		{
			const ImU32 col = IM_COL32(255, 193, 7, 255);
			const ImU32 ink = IM_COL32(40, 40, 40, 255);
			dl->AddTriangleFilled(
				{ center.x, center.y - r * 0.85f },
				{ center.x - r * 0.85f, center.y + r * 0.7f },
				{ center.x + r * 0.85f, center.y + r * 0.7f },
				col);
			dl->AddRectFilled(
				{ center.x - r * 0.1f, center.y - r * 0.25f },
				{ center.x + r * 0.1f, center.y + r * 0.25f },
				ink,
				1.f);
			dl->AddCircleFilled({ center.x, center.y + r * 0.45f }, r * 0.12f, ink, 10);
			break;
		}
		case 0x1F503: // clockwise arrows
		default:
		{
			const ImU32 col = IM_COL32(66, 165, 245, 255);
			dl->AddCircle(center, r * 0.65f, col, 20, 2.2f);
			dl->AddTriangleFilled(
				{ center.x + r * 0.55f, center.y - r * 0.15f },
				{ center.x + r * 0.95f, center.y + r * 0.15f },
				{ center.x + r * 0.35f, center.y + r * 0.25f },
				col);
			break;
		}
		}
	}

	ImVec2 CalcAnnounceSizeWithEmoji(const char* text, float wrapW)
	{
		if (!text || !text[0])
			return {};
		const float fontSize = ImGui::GetFontSize();
		const float emojiSize = fontSize * 1.05f;
		const float lineH = ImGui::GetTextLineHeightWithSpacing();
		const char* p = text;
		const char* end = text + std::strlen(text);
		float x = 0.f;
		float maxX = 0.f;
		int lines = 1;
		while (p < end)
		{
			if (*p == '\n')
			{
				maxX = (std::max)(maxX, x);
				x = 0.f;
				++lines;
				++p;
				continue;
			}
			const char* cpStart = p;
			uint32_t cp = 0;
			if (!Utf8Next(p, end, cp))
				break;
			if (cp == 0xFE0F)
				continue;
			float adv = 0.f;
			if (IsAnnounceEmoji(cp))
				adv = emojiSize + 1.f;
			else
			{
				const char* glyphEnd = p;
				adv = ImGui::CalcTextSize(cpStart, glyphEnd).x;
			}
			if (wrapW > 0.f && x > 0.f && x + adv > wrapW)
			{
				maxX = (std::max)(maxX, x);
				x = 0.f;
				++lines;
			}
			x += adv;
		}
		maxX = (std::max)(maxX, x);
		return { maxX, lines * lineH };
	}

	void DrawAnnounceWithEmoji(
		ImDrawList* dl,
		ImVec2 pos,
		float wrapW,
		const char* text,
		ImU32 textCol)
	{
		if (!dl || !text || !text[0])
			return;
		const float fontSize = ImGui::GetFontSize();
		const float emojiSize = fontSize * 1.05f;
		const float lineH = ImGui::GetTextLineHeightWithSpacing();
		ImFont* font = ImGui::GetFont();
		const char* p = text;
		const char* end = text + std::strlen(text);
		float x = pos.x;
		float y = pos.y;
		while (p < end)
		{
			if (*p == '\n')
			{
				x = pos.x;
				y += lineH;
				++p;
				continue;
			}
			const char* cpStart = p;
			uint32_t cp = 0;
			if (!Utf8Next(p, end, cp))
				break;
			if (cp == 0xFE0F)
				continue;
			float adv = 0.f;
			if (IsAnnounceEmoji(cp))
				adv = emojiSize + 1.f;
			else
				adv = ImGui::CalcTextSize(cpStart, p).x;
			if (wrapW > 0.f && x > pos.x && (x - pos.x) + adv > wrapW)
			{
				x = pos.x;
				y += lineH;
			}
			if (IsAnnounceEmoji(cp))
			{
				DrawAnnounceEmojiIcon(dl, { x + emojiSize * 0.5f, y + fontSize * 0.55f }, emojiSize, cp);
				x += adv;
			}
			else
			{
				dl->AddText(font, fontSize, { x, y }, textCol, cpStart, p);
				x += adv;
			}
		}
	}

	bool LooksLikeTelegramCdnUrl(const std::string& url)
	{
		std::string lower = url;
		for (char& ch : lower)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return lower.find("telesco.pe") != std::string::npos
			|| lower.find("telegram.org") != std::string::npos
			|| lower.find("telegram-cdn.org") != std::string::npos
			|| lower.find("cdn.telegram") != std::string::npos;
	}

	void DrawSubscriptionProviderCard(
		FontManager& fonts,
		float width,
		const char* title,
		const VpnStoreSettings& settings,
		const UiThemeColors& colors,
		const UiAccentColors& accents,
		float appearAlpha)
	{
		(void)fonts;
		if (appearAlpha < 0.02f)
			return;

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, appearAlpha);

		const float cardW = width;
		const float pad = 12.f;
		const float avatar = 44.f;
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();

		const char* displayTitle =
			!settings.subscriptionProfileTitle.empty()
				? settings.subscriptionProfileTitle.c_str()
				: (title && title[0] ? title : "Подписка");

		const std::string remaining = FormatSubscriptionRemaining(settings.subscriptionExpireUnix);
		const long long used =
			(std::max)(0LL, settings.subscriptionUploadBytes)
			+ (std::max)(0LL, settings.subscriptionDownloadBytes);
		const long long total = settings.subscriptionTotalBytes;
		const bool hasTraffic = used > 0 || total > 0;

		const std::string& announce = settings.subscriptionAnnounce;
		const bool announceIsStructured =
			announce.find('\n') != std::string::npos
			|| announce.find(u8"👤") != std::string::npos
			|| announce.find(u8"⌛️") != std::string::npos;

		// Measure announce wrap height (emoji replaced by colored icons).
		const float announceWrapW = cardW - pad * 2.f;
		const ImVec2 announceSize = announce.empty()
			? ImVec2(0.f, 0.f)
			: CalcAnnounceSizeWithEmoji(announce.c_str(), announceWrapW);

		const float headerH = avatar + 4.f;
		const float barBlockH = 28.f;
		const float announceH = announce.empty() ? 0.f : announceSize.y + 10.f;
		const float supportH = 34.f;
		const float cardH = pad + headerH + barBlockH + announceH + supportH + pad;

		// No separate filled card — parent monolith provides the background.
		ImGui::Dummy(ImVec2(cardW, cardH));

		float x = origin.x + pad;
		float y = origin.y + pad;

		// Avatar: explicit header URL, else Remnawave page logo (png.brand.press / app-config).
		ImTextureID avatarTex = 0;
		if (!settings.subscriptionIconUrl.empty() && !LooksLikeTelegramCdnUrl(settings.subscriptionIconUrl))
			avatarTex = VpnFlagIcons::Instance().GetUrlTexture(settings.subscriptionIconUrl);
		if (avatarTex == 0 && !settings.lastSubscriptionUrl.empty())
			avatarTex = VpnFlagIcons::Instance().GetSubscriptionPageIcon(settings.lastSubscriptionUrl);

		dl->AddCircleFilled(
			{ x + avatar * 0.5f, y + avatar * 0.5f },
			avatar * 0.5f,
			ImGui::GetColorU32(colors.inputBg),
			32);
		if (avatarTex != 0)
		{
			dl->AddImageRounded(
				ImTextureRef(avatarTex),
				{ x, y },
				{ x + avatar, y + avatar },
				{ 0.f, 0.f },
				{ 1.f, 1.f },
				IM_COL32(255, 255, 255, 255),
				avatar * 0.5f);
		}
		else
		{
			char letter[8] = "?";
			if (displayTitle && (unsigned char)displayTitle[0] >= 0x80)
			{
				const unsigned char b0 = static_cast<unsigned char>(displayTitle[0]);
				int len = 1;
				if ((b0 & 0xE0) == 0xC0)
					len = 2;
				else if ((b0 & 0xF0) == 0xE0)
					len = 3;
				else if ((b0 & 0xF8) == 0xF0)
					len = 4;
				std::memcpy(letter, displayTitle, static_cast<size_t>(len));
				letter[len] = '\0';
			}
			else if (displayTitle && displayTitle[0])
			{
				letter[0] = displayTitle[0];
				letter[1] = '\0';
			}
			const ImVec2 ls = ImGui::CalcTextSize(letter);
			dl->AddText(
				{ x + (avatar - ls.x) * 0.5f, y + (avatar - ls.y) * 0.5f },
				ImGui::GetColorU32(accents.warn),
				letter);
		}

		ImGui::SetCursorScreenPos({ x + avatar + 12.f, y + 10.f });
		{
			const float starSize = ImGui::GetFontSize() * 1.05f;
			const ImVec2 starPos = ImGui::GetCursorScreenPos();
			DrawAnnounceEmojiIcon(
				dl,
				{ starPos.x + starSize * 0.5f, starPos.y + ImGui::GetFontSize() * 0.55f },
				starSize,
				0x2B50);
			ImGui::Dummy(ImVec2(starSize + 2.f, ImGui::GetFontSize()));
		}
		ImGui::SameLine(0.f, 6.f);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted(displayTitle);
		ImGui::PopStyleColor();

		y += headerH;
		x = origin.x + pad;
		const float barW = cardW - pad * 2.f;
		dl->AddRectFilled(
			{ x, y + 2.f },
			{ x + barW, y + 8.f },
			ImGui::GetColorU32(colors.inputBg),
			3.f);
		float trafficT = 0.f;
		if (total > 0)
			trafficT = (std::min)(1.f, static_cast<float>(used) / static_cast<float>(total));
		// total<=0 => unlimited: keep bar empty (0 fill)
		if (trafficT > 0.001f)
		{
			dl->AddRectFilled(
				{ x, y + 2.f },
				{ x + barW * trafficT, y + 8.f },
				ImGui::GetColorU32(accents.ok),
				3.f);
		}

		ImGui::SetCursorScreenPos({ x, y + 10.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		if (!remaining.empty())
			ImGui::TextUnformatted(remaining.c_str());
		if (hasTraffic)
		{
			char trafficBuf[96] = {};
			if (total > 0)
			{
				snprintf(
					trafficBuf,
					sizeof trafficBuf,
					"%s / %s",
					FormatBytesRu(used).c_str(),
					FormatBytesRu(total).c_str());
			}
			else
			{
				snprintf(trafficBuf, sizeof trafficBuf, "%s / ∞", FormatBytesRu(used).c_str());
			}
			const float tw = ImGui::CalcTextSize(trafficBuf).x;
			ImGui::SameLine(0.f, 0.f);
			ImGui::SetCursorScreenPos({ origin.x + cardW - pad - tw, y + 10.f });
			ImGui::TextUnformatted(trafficBuf);
		}
		ImGui::PopStyleColor();

		y += barBlockH;
		if (!announce.empty())
		{
			DrawAnnounceWithEmoji(
				dl,
				{ x, y },
				announceWrapW,
				announce.c_str(),
				ImGui::GetColorU32(colors.textPrimary));
			(void)announceIsStructured;
		}

		ImGui::SetCursorScreenPos({ x, origin.y + cardH - pad - 28.f });
		if (UiCommon::SecondaryButton("Поддержка", ImVec2(118.f, 26.f), colors))
		{
			std::string url = settings.subscriptionSupportUrl;
			if (url.empty())
				url = settings.lastSubscriptionUrl;
			if (!url.empty())
				ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}

		ImGui::SetCursorScreenPos({ origin.x, origin.y + cardH });
		ImGui::Dummy(ImVec2(cardW, 6.f));
		ImGui::PopStyleVar();
	}

	bool DrawServersPageHeader(
		FontManager& fonts,
		float width,
		bool& vpnEnabled,
		float& vpnMix,
		bool& fixDiscord,
		const UiThemeColors& colors)
	{
		const float deltaTime = ImGui::GetIO().DeltaTime;
		vpnMix = UiCommon::AnimateMix(vpnMix, vpnEnabled, deltaTime, 10.f);

		const ImVec2 start = ImGui::GetCursorScreenPos();
		const float lineH = ImGui::GetTextLineHeight();
		const float rowY = start.y + 1.f;

		constexpr const char* kVpnLabel = "VPN";
		constexpr const char* kFixDiscordLabel = "Fix Discord";
		const float toggleW = 40.f;
		const float toggleH = 22.f;
		const float checkBox = 18.f;
		const float labelW = ImGui::CalcTextSize(kVpnLabel).x;
		const float fixLabelW = ImGui::CalcTextSize(kFixDiscordLabel).x;
		const float gap = 8.f;
		const float blockGap = 14.f;

		const float vpnBlockW = labelW + gap + toggleW;
		const float fixBlockW = fixLabelW + gap + checkBox;
		const float rightBlockW = fixBlockW + blockGap + vpnBlockW;

		float x = start.x + width - rightBlockW;
		bool fixChanged = false;

		ImGui::SetCursorScreenPos({ x, rowY });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(kFixDiscordLabel);
		ImGui::PopStyleColor();
		x += fixLabelW + gap;

		ImGui::SetCursorScreenPos({
			x,
			rowY + (ImGui::GetFrameHeight() - checkBox) * 0.5f
		});
		fixChanged = UiCommon::StyledCheckbox("##fix_discord", &fixDiscord, colors);
		UiCommon::SetItemTooltip(
			"Прогоняет Discord (домены, голос UDP и Discord.exe)\n"
			"через VPN на любой стратегии RUv1.");
		x += checkBox + blockGap;

		ImGui::SetCursorScreenPos({ x, rowY });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(kVpnLabel);
		ImGui::PopStyleColor();
		x += labelW + gap;

		ImGui::SetCursorScreenPos({
			x,
			rowY + (ImGui::GetFrameHeight() - toggleH) * 0.5f
		});
		if (UiCommon::ToggleSwitch("##vpn_enable", vpnMix, colors))
			vpnEnabled = !vpnEnabled;

		ImGui::SetCursorScreenPos(start);
		wchar_t iconWide[] = { static_cast<wchar_t>(0xf0ac), 0 };
		char iconUtf8[8] = {};
		WideCharToMultiByte(CP_UTF8, 0, iconWide, 1, iconUtf8, static_cast<int>(sizeof iconUtf8), nullptr, nullptr);
		ImFont* iconFont = fonts.GetSolidFont();
		if (iconFont)
			ImGui::PushFont(iconFont);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		if (iconUtf8[0] != '\0')
			ImGui::TextUnformatted(iconUtf8);
		if (iconFont)
			ImGui::PopFont();
		ImGui::SameLine(0.f, 8.f);
		ImGui::TextUnformatted("Серверы");
		ImGui::PopStyleColor();

		ImGui::SetCursorScreenPos({ start.x, start.y + lineH + 2.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Импорт, тестирование и выбор VPN-профилей.");
		ImGui::PopStyleColor();

		const float headerH = lineH * 2.f + 2.f + UiMetrics::kSectionGap;
		ImGui::SetCursorScreenPos({ start.x, start.y + headerH });
		ImGui::Dummy({ width, 0.f });
		return fixChanged;
	}

	ImVec4 ModuleVersionAccent(ComponentUpdateStatus status, const UiAccentColors& accents)
	{
		switch (status)
		{
		case ComponentUpdateStatus::UpToDate:
			return accents.ok;
		case ComponentUpdateStatus::Checking:
		case ComponentUpdateStatus::UpdateAvailable:
			return accents.warn;
		case ComponentUpdateStatus::Unknown:
		case ComponentUpdateStatus::Error:
		default:
			return accents.fail;
		}
	}

	bool IsDisplayVersion(const std::string& raw)
	{
		if (raw.empty() || raw == "—" || raw == "Unknown" || raw == "Установлен")
			return false;
		for (unsigned char ch : raw)
		{
			if (ch >= '0' && ch <= '9')
				return true;
		}
		return false;
	}

	bool ModuleNeedsUpdate(
		ComponentUpdateStatus status,
		const std::string& local,
		const std::string& remote,
		bool applying)
	{
		if (applying)
			return true;
		if (status == ComponentUpdateStatus::UpdateAvailable)
			return true;
		return IsDisplayVersion(local) && IsDisplayVersion(remote) && local != remote;
	}

	// One compact line, only when mihomo and/or wintun have updates:
	// Модули: mihomo 1.x, wintun 0.x   [Скачать обновление]
	void DrawVpnModulesUpdateRow(VpnManager* manager, const UiThemeColors& colors, const UiAccentColors& accents)
	{
		auto& check = VpnModuleUpdateCheck::Instance();
		auto& apply = VpnModuleUpdateApply::Instance();

		std::string mihomoVer = check.GetMihomoLocalVersion();
		if (!IsDisplayVersion(mihomoVer))
			mihomoVer.clear();
		std::string wintunVer = check.GetWintunLocalVersion();
		if (!IsDisplayVersion(wintunVer))
			wintunVer.clear();

		const bool mihomoNeeds = ModuleNeedsUpdate(
			check.GetMihomoStatus(),
			mihomoVer,
			check.GetMihomoRemoteVersion(),
			apply.IsApplyingMihomo());
		const bool wintunNeeds = ModuleNeedsUpdate(
			check.GetWintunStatus(),
			wintunVer,
			check.GetWintunRemoteVersion(),
			apply.IsApplyingWintun());

		if (!mihomoNeeds && !wintunNeeds)
			return;

		const bool applying = apply.IsApplyingAny();

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Модули:");
		ImGui::PopStyleColor();

		bool first = true;
		auto appendModule = [&](const char* name, const std::string& ver, ComponentUpdateStatus status) {
			if (!first)
			{
				ImGui::SameLine(0.f, 0.f);
				ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
				ImGui::TextUnformatted(",");
				ImGui::PopStyleColor();
				ImGui::SameLine(0.f, 6.f);
			}
			else
			{
				ImGui::SameLine(0.f, 6.f);
			}
			first = false;

			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextUnformatted(name);
			ImGui::PopStyleColor();
			if (!ver.empty())
			{
				ImGui::SameLine(0.f, 4.f);
				// Match "mihomo" text top — default VersionBadge centers in lineH and sits too high here.
				const float nameTop = ImGui::GetItemRectMin().y;
				const ImVec2 cur = ImGui::GetCursorScreenPos();
				ImGui::SetCursorScreenPos({ cur.x, nameTop });
				UiCommon::VersionBadge(ver.c_str(), ModuleVersionAccent(status, accents), colors, false);
			}
		};

		if (mihomoNeeds)
			appendModule("mihomo", mihomoVer, check.GetMihomoStatus());
		if (wintunNeeds)
			appendModule("wintun", wintunVer, check.GetWintunStatus());

		ImGui::SameLine(0.f, 12.f);
		const char* btnLabel = applying ? "Скачивание..." : "Скачать обновление";
		const float btnW = ImGui::CalcTextSize(btnLabel).x + 24.f;
		const float btnH = UiMetrics::kSmallBtnHeight;
		const float lineH = ImGui::GetTextLineHeight();
		if (btnH != lineH)
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (lineH - btnH) * 0.5f);
		if (UiCommon::SecondaryButton(btnLabel, { btnW, btnH }, colors, !applying))
		{
			if (mihomoNeeds && !apply.IsApplyingMihomo())
				apply.RequestApplyMihomo(manager);
			if (wintunNeeds && !apply.IsApplyingWintun())
				apply.RequestApplyWintun(manager);
		}

		ImGui::Dummy({ 0.f, UiMetrics::kRowGap });
	}
}

void UiVpnPage::EnsureStoreLoaded()
{
	if (m_storeLoaded)
		return;

	VpnStoreSettings settings;
	m_store.Load(m_nodes, &settings);
	m_workMode = settings.workMode;
	if (m_workMode < 1 || m_workMode > 4)
		m_workMode = 1; // 0 был мёртвый «региональные прессеты» → RUv1 Blocked
	m_transportMode = settings.transportMode;
	m_fixDiscord = settings.fixDiscord;

	bool normalized = m_workMode != settings.workMode;
	for (VpnNode& node : m_nodes)
	{
		const std::string beforeName = node.name;
		const std::string beforeGroup = node.group;
		const std::string beforeCountry = node.country;
		VpnImport::NormalizeNodeDisplay(node);
		if (node.name != beforeName || node.group != beforeGroup || node.country != beforeCountry)
			normalized = true;
	}

	m_activeIndex = FindNodeIndexByUri(settings.activeUri);
	if (m_activeIndex < 0 && !m_nodes.empty())
		m_activeIndex = 0;

	m_lastAppliedWorkMode = m_workMode;
	m_lastAppliedActiveIndex = m_activeIndex;
	m_store.LoadSettings(m_lastAppliedSettings);
	m_lastAppliedSettings.workMode = m_workMode;
	m_storeLoaded = true;

	if (normalized)
		SaveStore();
}

VpnStoreSettings UiVpnPage::BuildStoreSettings() const
{
	VpnStoreSettings settings;
	m_store.LoadSettings(settings);
	settings.workMode = m_workMode;
	settings.transportMode = m_transportMode;
	settings.fixDiscord = m_fixDiscord;
	if (m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_nodes.size()))
		settings.activeUri = m_nodes[static_cast<size_t>(m_activeIndex)].originalUri;
	if (settings.lastSubscriptionUrl.empty())
	{
		for (const VpnNode& node : m_nodes)
		{
			if (!node.sourceUrl.empty())
			{
				settings.lastSubscriptionUrl = node.sourceUrl;
				break;
			}
		}
	}
	return settings;
}

namespace
{
	bool SameTransportSettings(const VpnStoreSettings& a, const VpnStoreSettings& b)
	{
		return a.workMode == b.workMode
			&& a.activeUri == b.activeUri
			&& a.transportMode == b.transportMode
			&& a.dnsMode == b.dnsMode
			&& a.bootstrapDns == b.bootstrapDns
			&& a.bootstrapType == b.bootstrapType
			&& a.proxyDns == b.proxyDns
			&& a.proxyType == b.proxyType
			&& a.fixDiscord == b.fixDiscord;
	}

	bool SameRuntimeSettings(const VpnStoreSettings& a, const VpnStoreSettings& b)
	{
		return SameTransportSettings(a, b)
			&& a.routingRevision == b.routingRevision;
	}
}

int UiVpnPage::FindNodeIndexByUri(const std::string& uri) const
{
	if (uri.empty())
		return -1;

	for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
	{
		if (m_nodes[static_cast<size_t>(i)].originalUri == uri)
			return i;
	}
	return -1;
}

void UiVpnPage::SetActiveServer(int nodeIndex)
{
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
		return;

	m_activeIndex = nodeIndex;
	SaveStore();
}

void UiVpnPage::SaveStore()
{
	if (!m_storeLoaded)
		return;
	const VpnStoreSettings settings = BuildStoreSettings();
	m_store.Save(m_nodes, &settings);
}

void UiVpnPage::ApplyPendingGeoLookups()
{
	std::vector<PendingGeoResult> pending;
	{
		std::lock_guard<std::mutex> lock(m_geoMutex);
		if (m_pendingGeo.empty())
			return;
		pending.swap(m_pendingGeo);
	}

	bool changed = false;
	for (const PendingGeoResult& result : pending)
	{
		if (result.nodeIndex < 0 || result.nodeIndex >= static_cast<int>(m_nodes.size()))
			continue;
		if (result.countryCode.empty())
			continue;

		VpnNode& node = m_nodes[static_cast<size_t>(result.nodeIndex)];
		if (node.country == result.countryCode)
			continue;

		node.country = result.countryCode;
		changed = true;
	}

	if (changed && m_nodes.size() < 400)
		SaveStore();
}

void UiVpnPage::QueueCountryLookups()
{
	constexpr size_t kMaxGeoInFlight = 8;
	constexpr int kMaxCacheHitsPerFrame = 256;

	int cacheHits = 0;
	size_t inFlight = 0;
	{
		std::lock_guard<std::mutex> lock(m_geoMutex);
		inFlight = m_geoInFlight.size();
	}

	for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
	{
		VpnNode& node = m_nodes[static_cast<size_t>(i)];
		if (!node.country.empty() || node.server.empty() || !VpnGeo::IsPublicIp(node.server))
			continue;

		const std::string cached = VpnGeo::GetCachedCountryCode(node.server);
		if (!cached.empty())
		{
			node.country = cached;
			if (++cacheHits >= kMaxCacheHitsPerFrame)
				break;
			continue;
		}

		if (inFlight >= kMaxGeoInFlight)
			break;

		{
			std::lock_guard<std::mutex> lock(m_geoMutex);
			if (m_geoInFlight.count(node.server) > 0)
				continue;
			m_geoInFlight.insert(node.server);
			inFlight = m_geoInFlight.size();
		}

		const int nodeIndex = i;
		const std::string ip = node.server;
		std::thread([this, nodeIndex, ip]()
		{
			std::string countryCode;
			VpnGeo::LookupCountryCode(ip, countryCode);

			std::lock_guard<std::mutex> lock(m_geoMutex);
			m_geoInFlight.erase(ip);
			if (!countryCode.empty())
				m_pendingGeo.push_back({ nodeIndex, countryCode });
		}).detach();
	}
}

void UiVpnPage::ApplySubscriptionMetaToSettings(VpnStoreSettings& settings, const VpnImportResult& meta)
{
	if (!meta.hasSubscriptionCard)
		return;

	if (meta.subscriptionExpireUnix > 0)
		settings.subscriptionExpireUnix = meta.subscriptionExpireUnix;
	if (meta.subscriptionUploadBytes > 0 || meta.subscriptionDownloadBytes > 0 || meta.subscriptionTotalBytes > 0)
	{
		settings.subscriptionUploadBytes = meta.subscriptionUploadBytes;
		settings.subscriptionDownloadBytes = meta.subscriptionDownloadBytes;
		settings.subscriptionTotalBytes = meta.subscriptionTotalBytes;
	}
	if (!meta.subscriptionSupportUrl.empty())
		settings.subscriptionSupportUrl = meta.subscriptionSupportUrl;
	if (!meta.subscriptionProfileTitle.empty())
		settings.subscriptionProfileTitle = meta.subscriptionProfileTitle;
	if (!meta.subscriptionAnnounce.empty())
		settings.subscriptionAnnounce = meta.subscriptionAnnounce;
	if (!meta.subscriptionProviderId.empty())
		settings.subscriptionProviderId = meta.subscriptionProviderId;
	if (!meta.subscriptionUserId.empty())
		settings.subscriptionUserId = meta.subscriptionUserId;
	if (!meta.subscriptionIconUrl.empty() && !LooksLikeTelegramCdnUrl(meta.subscriptionIconUrl))
		settings.subscriptionIconUrl = meta.subscriptionIconUrl;
	else if (LooksLikeTelegramCdnUrl(settings.subscriptionIconUrl))
		settings.subscriptionIconUrl.clear();
}

void UiVpnPage::ApplyPendingImportIfAny()
{
	PendingImportResult pending;
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		if (!m_pendingImport.ready)
			return;
		pending = std::move(m_pendingImport);
		m_pendingImport = {};
	}

	VpnImportResult meta {};
	meta.subscriptionExpireUnix = pending.subscriptionExpireUnix;
	meta.subscriptionUploadBytes = pending.subscriptionUploadBytes;
	meta.subscriptionDownloadBytes = pending.subscriptionDownloadBytes;
	meta.subscriptionTotalBytes = pending.subscriptionTotalBytes;
	meta.subscriptionSupportUrl = pending.subscriptionSupportUrl;
	meta.subscriptionProfileTitle = pending.subscriptionProfileTitle;
	meta.subscriptionAnnounce = pending.subscriptionAnnounce;
	meta.subscriptionProviderId = pending.subscriptionProviderId;
	meta.subscriptionUserId = pending.subscriptionUserId;
	meta.subscriptionIconUrl = pending.subscriptionIconUrl;
	meta.hasSubscriptionCard = pending.hasSubscriptionCard;

	if (!pending.refreshSourceUrl.empty())
	{
		ApplyRefreshResult(
			std::move(pending.nodes),
			std::move(pending.errors),
			pending.refreshSourceUrl,
			meta);
	}
	else
	{
		ApplyImportResult(
			std::move(pending.nodes),
			pending.duplicatesSkipped,
			std::move(pending.errors),
			meta);
	}
}

namespace
{
	bool NodeFromSubscriptionUrl(const VpnNode& node, const std::string& sourceUrl)
	{
		if (!sourceUrl.empty() && node.sourceUrl == sourceUrl)
			return true;
		return false;
	}

	bool NodeLooksLikeCapybaraGroup(const VpnNode& node)
	{
		const std::string group = node.group;
		if (group.find("Capybara") != std::string::npos || group.find("Copybara") != std::string::npos)
			return true;
		if (node.server.find("capynode.") != std::string::npos
			|| node.server.find("capycore.") != std::string::npos)
			return true;
		if (node.name.find("ОБХОД") != std::string::npos || node.name.find("обход") != std::string::npos)
			return true;
		return false;
	}
}

void UiVpnPage::ApplyRefreshResult(
	std::vector<VpnNode> importedNodes,
	std::vector<std::string> errors,
	const std::string& sourceUrl,
	const VpnImportResult& meta)
{
	for (VpnNode& node : importedNodes)
		VpnImport::NormalizeNodeDisplay(node);

	const std::string activeUri =
		(m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_nodes.size()))
			? m_nodes[static_cast<size_t>(m_activeIndex)].originalUri
			: std::string {};

	std::unordered_map<std::string, VpnNode> previousByUri;
	const bool refreshCapybara = !importedNodes.empty()
		&& (importedNodes.front().group.find("Capybara") != std::string::npos
			|| importedNodes.front().server.find("capynode.") != std::string::npos
			|| importedNodes.front().server.find("capycore.") != std::string::npos);

	auto shouldReplace = [&](const VpnNode& node) {
		return NodeFromSubscriptionUrl(node, sourceUrl)
			|| (refreshCapybara && NodeLooksLikeCapybaraGroup(node));
	};

	// Keep group position: insert refreshed block where the old group started.
	std::vector<VpnNode> kept;
	kept.reserve(m_nodes.size());
	size_t insertAt = kept.size();
	bool sawGroup = false;
	for (const VpnNode& node : m_nodes)
	{
		if (shouldReplace(node))
		{
			if (!sawGroup)
			{
				insertAt = kept.size();
				sawGroup = true;
			}
			previousByUri[node.originalUri] = node;
			continue;
		}
		kept.push_back(node);
	}
	if (!sawGroup)
		insertAt = kept.size();

	std::vector<VpnNode> refreshed;
	refreshed.reserve(importedNodes.size());
	for (VpnNode& node : importedNodes)
	{
		const auto it = previousByUri.find(node.originalUri);
		if (it != previousByUri.end())
		{
			node.pingMs = it->second.pingMs;
			node.speedMbps = it->second.speedMbps;
			node.alive = it->second.alive;
			node.lastUsed = it->second.lastUsed;
			node.pingHistory = it->second.pingHistory;
			node.speedHistory = it->second.speedHistory;
			if (node.country.empty())
				node.country = it->second.country;
		}
		node.sourceUrl = sourceUrl;
		refreshed.push_back(std::move(node));
	}

	const size_t addedCount = refreshed.size();
	kept.insert(
		kept.begin() + static_cast<std::ptrdiff_t>(insertAt),
		std::make_move_iterator(refreshed.begin()),
		std::make_move_iterator(refreshed.end()));
	m_nodes = std::move(kept);

	m_activeIndex = FindNodeIndexByUri(activeUri);
	if (m_activeIndex < 0 && !m_nodes.empty())
		m_activeIndex = 0;
	ClearSelection();
	if (meta.hasSubscriptionCard)
	{
		VpnStoreSettings settings = BuildStoreSettings();
		settings.lastSubscriptionUrl = sourceUrl;
		ApplySubscriptionMetaToSettings(settings, meta);
		m_store.Save(m_nodes, &settings);
	}
	else
	{
		SaveStore();
	}

	char buffer[256] = {};
	if (!errors.empty() && addedCount == 0)
		snprintf(buffer, sizeof buffer, "Обновление подписки не удалось: %s", errors.front().c_str());
	else
		snprintf(buffer, sizeof buffer, "Подписка обновлена: серверов %zu", addedCount);

	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = buffer;
		m_importRunning.store(false);
	}
	AppLog::Instance().Append(LogSource::VpnRouting, std::string("Импорт (UI): ") + buffer);
}

void UiVpnPage::ApplyImportResult(
	std::vector<VpnNode> importedNodes,
	int duplicatesSkipped,
	std::vector<std::string> errors,
	const VpnImportResult& meta)
{
	size_t addedCount = 0;
	std::string importedSourceUrl;
	std::unordered_set<std::string> existingUris;
	existingUris.reserve(m_nodes.size() + importedNodes.size());
	for (const VpnNode& existing : m_nodes)
	{
		if (!existing.originalUri.empty())
			existingUris.insert(existing.originalUri);
	}

	for (VpnNode& node : importedNodes)
	{
		VpnImport::NormalizeNodeDisplay(node);
		if (importedSourceUrl.empty() && !node.sourceUrl.empty())
			importedSourceUrl = node.sourceUrl;
		if (!node.originalUri.empty() && !existingUris.insert(node.originalUri).second)
		{
			++duplicatesSkipped;
			continue;
		}
		m_nodes.push_back(std::move(node));
		++addedCount;
	}

	if (!m_pendingActivateUri.empty())
	{
		const int activateIndex = FindNodeIndexByUri(m_pendingActivateUri);
		if (activateIndex >= 0)
			SetActiveServer(activateIndex);
		m_pendingActivateUri.clear();
	}

	if (m_activeIndex < 0 && !m_nodes.empty())
		m_activeIndex = 0;

	if (meta.hasSubscriptionCard)
	{
		VpnStoreSettings settings = BuildStoreSettings();
		if (!importedSourceUrl.empty())
			settings.lastSubscriptionUrl = importedSourceUrl;
		ApplySubscriptionMetaToSettings(settings, meta);
		m_store.Save(m_nodes, &settings);
	}
	else
	{
		// Plain URI list / GitHub file — do not attach subscription card meta or hijack last URL.
		SaveStore();
	}

	char buffer[256] = {};
	if (addedCount > 0)
	{
		if (duplicatesSkipped > 0)
		{
			snprintf(
				buffer,
				sizeof buffer,
				"Импортировано серверов: %zu. Дубликаты пропущены: %d",
				addedCount,
				duplicatesSkipped);
		}
		else
		{
			snprintf(buffer, sizeof buffer, "Импортировано серверов: %zu", addedCount);
		}
	}
	else if (!importedNodes.empty())
	{
		snprintf(buffer, sizeof buffer, "Все серверы уже в списке. Дубликаты пропущены: %d", duplicatesSkipped);
	}
	else if (!errors.empty())
	{
		snprintf(buffer, sizeof buffer, "Импорт не удался: %s", errors.front().c_str());
	}
	else
	{
		snprintf(buffer, sizeof buffer, "В буфере обмена не найдено поддерживаемых VPN-ссылок.");
	}

	std::lock_guard<std::mutex> lock(m_importMutex);
	m_importStatus = buffer;
	m_importRunning.store(false);
	AppLog::Instance().Append(LogSource::VpnRouting, std::string("Импорт (UI): ") + buffer);
	for (const std::string& error : errors)
	{
		if (!error.empty())
			AppLog::Instance().Append(LogSource::VpnRouting, std::string("Импорт ошибка: ") + error);
	}
}

void UiVpnPage::StartImportFromClipboard()
{
	if (m_importRunning.load())
		return;

	std::string clipboardText;
	if (!VpnImport::ReadClipboardUtf8(clipboardText))
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = "Буфер обмена пуст или недоступен.";
		AppLog::Instance().Append(LogSource::VpnRouting, "Импорт: буфер обмена пуст или недоступен.");
		return;
	}

	StartImportFromText(clipboardText, "Импорт из буфера обмена...");
}

void UiVpnPage::ImportSubscriptionUrl(const std::string& urlOrText)
{
	std::string text = urlOrText;
	while (!text.empty() && (text.front() == '"' || text.front() == '\'' || text.front() == ' ' || text.front() == '\t'))
		text.erase(text.begin());
	while (!text.empty() && (text.back() == '"' || text.back() == '\'' || text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
		text.pop_back();
	if (text.empty() || m_importRunning.load())
		return;

	EnsureStoreLoaded();

	auto startsWithHttp = [](const std::string& s) {
		if (s.size() < 7)
			return false;
		std::string head = s.substr(0, 8);
		for (char& c : head)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
	};

	AppLog::Instance().Append(LogSource::VpnRouting, "Protocol import: " + text);
	if (startsWithHttp(text))
	{
		m_pendingActivateUri.clear();
		StartRefreshSubscriptions(text);
	}
	else
	{
		m_pendingActivateUri = text;
		StartImportFromText(text, "Импорт сервера по протоколу...");
	}
}

void UiVpnPage::StartImportFromText(const std::string& text, const char* statusLabel)
{
	if (m_importRunning.load() || text.empty())
		return;

	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = statusLabel ? statusLabel : "Импорт...";
	}
	m_importRunning.store(true);
	AppLog::Instance().Append(
		LogSource::VpnRouting,
		std::string("Импорт: запуск (") + std::to_string(text.size()) + " байт).");

	const int nextNodeIndex = static_cast<int>(m_nodes.size()) + 1;
	std::thread([this, text, nextNodeIndex]()
	{
		const VpnImportResult result = VpnImport::ImportFromText(text, nextNodeIndex);
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			m_pendingImport.nodes = std::move(result.nodes);
			m_pendingImport.duplicatesSkipped = result.duplicatesSkipped;
			m_pendingImport.errors = std::move(result.errors);
			m_pendingImport.subscriptionExpireUnix = result.subscriptionExpireUnix;
			m_pendingImport.subscriptionUploadBytes = result.subscriptionUploadBytes;
			m_pendingImport.subscriptionDownloadBytes = result.subscriptionDownloadBytes;
			m_pendingImport.subscriptionTotalBytes = result.subscriptionTotalBytes;
			m_pendingImport.subscriptionSupportUrl = result.subscriptionSupportUrl;
			m_pendingImport.subscriptionProfileTitle = result.subscriptionProfileTitle;
			m_pendingImport.subscriptionAnnounce = result.subscriptionAnnounce;
			m_pendingImport.subscriptionProviderId = result.subscriptionProviderId;
			m_pendingImport.subscriptionUserId = result.subscriptionUserId;
			m_pendingImport.subscriptionIconUrl = result.subscriptionIconUrl;
			m_pendingImport.hasSubscriptionCard = result.hasSubscriptionCard;
			m_pendingImport.ready = true;
		}
	}).detach();
}

void UiVpnPage::StartRefreshSubscriptions(const std::string& preferredSourceUrl)
{
	if (m_importRunning.load())
		return;

	std::string sourceUrl = preferredSourceUrl;
	if (sourceUrl.empty())
	{
		for (const VpnNode& node : m_nodes)
		{
			if (!node.sourceUrl.empty())
			{
				sourceUrl = node.sourceUrl;
				break;
			}
		}
	}
	if (sourceUrl.empty())
	{
		VpnStoreSettings settings;
		m_store.LoadSettings(settings);
		sourceUrl = settings.lastSubscriptionUrl;
	}
	if (sourceUrl.empty())
	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = "Нет сохранённой ссылки подписки. Сначала импортируйте URL.";
		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = "Обновление подписки...";
	}
	m_importRunning.store(true);
	AppLog::Instance().Append(LogSource::VpnRouting, "Импорт: обновление подписки " + sourceUrl);

	const int nextNodeIndex = 1;
	std::thread([this, sourceUrl, nextNodeIndex]()
	{
		const VpnImportResult result = VpnImport::ImportFromText(sourceUrl, nextNodeIndex);
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			m_pendingImport.nodes = std::move(result.nodes);
			m_pendingImport.duplicatesSkipped = result.duplicatesSkipped;
			m_pendingImport.errors = std::move(result.errors);
			m_pendingImport.refreshSourceUrl = sourceUrl;
			m_pendingImport.subscriptionExpireUnix = result.subscriptionExpireUnix;
			m_pendingImport.subscriptionUploadBytes = result.subscriptionUploadBytes;
			m_pendingImport.subscriptionDownloadBytes = result.subscriptionDownloadBytes;
			m_pendingImport.subscriptionTotalBytes = result.subscriptionTotalBytes;
			m_pendingImport.subscriptionSupportUrl = result.subscriptionSupportUrl;
			m_pendingImport.subscriptionProfileTitle = result.subscriptionProfileTitle;
			m_pendingImport.subscriptionAnnounce = result.subscriptionAnnounce;
			m_pendingImport.subscriptionProviderId = result.subscriptionProviderId;
			m_pendingImport.subscriptionUserId = result.subscriptionUserId;
			m_pendingImport.subscriptionIconUrl = result.subscriptionIconUrl;
			m_pendingImport.hasSubscriptionCard = result.hasSubscriptionCard;
			m_pendingImport.ready = true;
		}
	}).detach();
}

void UiVpnPage::SetToolbarStatus(const std::string& text)
{
	std::lock_guard<std::mutex> lock(m_probeMutex);
	m_toolbarStatus = text;
}

void UiVpnPage::PushPingHistory(VpnNode& node, int pingMs)
{
	VpnNodeHistoryEntry entry;
	entry.time = VpnNodeProbe::NowTimeLabel();
	if (pingMs < 0)
		entry.value = "timeout";
	else
		entry.value = std::to_string(pingMs) + " ms";
	node.pingHistory.push_back(entry);
	if (node.pingHistory.size() > 24)
		node.pingHistory.erase(node.pingHistory.begin());
}

void UiVpnPage::PushSpeedHistory(VpnNode& node, float speedMbps)
{
	VpnNodeHistoryEntry entry;
	entry.time = VpnNodeProbe::NowTimeLabel();
	if (speedMbps < 0.f)
		entry.value = "fail";
	else
	{
		char buf[32];
		snprintf(buf, sizeof buf, "%.1f MB/s", speedMbps);
		entry.value = buf;
	}
	node.speedHistory.push_back(entry);
	if (node.speedHistory.size() > 24)
		node.speedHistory.erase(node.speedHistory.begin());
}

void UiVpnPage::ApplyPendingProbeResults()
{
	std::vector<PendingProbeResult> pending;
	{
		std::lock_guard<std::mutex> lock(m_probeMutex);
		pending.swap(m_pendingProbe);
	}

	bool changed = false;
	for (const PendingProbeResult& item : pending)
	{
		if (!item.ready)
			continue;
		if (item.nodeIndex < 0 || item.nodeIndex >= static_cast<int>(m_nodes.size()))
			continue;
		VpnNode& node = m_nodes[static_cast<size_t>(item.nodeIndex)];
		if (item.pingMs != -2)
		{
			node.pingMs = item.pingMs;
			node.alive = item.pingMs >= 0 ? 1 : 0;
			PushPingHistory(node, item.pingMs);
			m_probeFlash[item.nodeIndex] = 1.f;
			changed = true;
		}
		if (item.speedMbps > -1.5f)
		{
			node.speedMbps = item.speedMbps;
			if (!item.live)
				PushSpeedHistory(node, item.speedMbps);
			m_probeFlash[item.nodeIndex] = 1.f;
			changed = true;
		}
	}

	if (changed)
		m_probeDirty = true;

	// Don't block UI frames with disk I/O while probes are in flight.
	if (m_probeDirty && !AnyProbeBusy())
	{
		SaveStore();
		m_probeDirty = false;
	}
}

bool UiVpnPage::HasSelection() const
{
	return !m_selectedSet.empty();
}

int UiVpnPage::SelectionCount() const
{
	return static_cast<int>(m_selectedSet.size());
}

std::vector<int> UiVpnPage::SelectedIndicesSorted() const
{
	std::vector<int> indices;
	indices.reserve(m_selectedSet.size());
	const int nodeCount = static_cast<int>(m_nodes.size());
	for (int index : m_selectedSet)
	{
		if (index >= 0 && index < nodeCount)
			indices.push_back(index);
	}
	std::sort(indices.begin(), indices.end());
	return indices;
}

void UiVpnPage::ClearSelection()
{
	m_selectedSet.clear();
	m_selected = -1;
}

void UiVpnPage::SelectOnly(int index)
{
	m_selectedSet.clear();
	if (index < 0 || index >= static_cast<int>(m_nodes.size()))
	{
		m_selected = -1;
		return;
	}
	m_selectedSet.insert(index);
	m_selected = index;
}

void UiVpnPage::ToggleSelect(int index)
{
	if (index < 0 || index >= static_cast<int>(m_nodes.size()))
		return;

	const auto it = m_selectedSet.find(index);
	if (it != m_selectedSet.end())
	{
		m_selectedSet.erase(it);
		if (m_selected == index)
		{
			if (m_selectedSet.empty())
				m_selected = -1;
			else
				m_selected = *m_selectedSet.begin();
		}
	}
	else
	{
		m_selectedSet.insert(index);
		m_selected = index;
	}
}

void UiVpnPage::SelectRangeInOrder(const std::vector<int>& orderedIndices, int clickedIndex)
{
	if (orderedIndices.empty())
	{
		SelectOnly(clickedIndex);
		return;
	}

	int anchorPos = -1;
	int clickPos = -1;
	for (int i = 0; i < static_cast<int>(orderedIndices.size()); ++i)
	{
		if (orderedIndices[static_cast<size_t>(i)] == m_selected)
			anchorPos = i;
		if (orderedIndices[static_cast<size_t>(i)] == clickedIndex)
			clickPos = i;
	}

	if (clickPos < 0)
	{
		SelectOnly(clickedIndex);
		return;
	}
	if (anchorPos < 0)
	{
		SelectOnly(clickedIndex);
		return;
	}

	const int lo = (std::min)(anchorPos, clickPos);
	const int hi = (std::max)(anchorPos, clickPos);
	m_selectedSet.clear();
	for (int i = lo; i <= hi; ++i)
		m_selectedSet.insert(orderedIndices[static_cast<size_t>(i)]);
	// Keep existing anchor for further Shift ranges.
}

void UiVpnPage::OpenSelectedDetails()
{
	if (m_selected < 0 || m_selected >= static_cast<int>(m_nodes.size()))
		return;
	m_detailIndex = m_selected;
	m_view = View::Detail;
}

void UiVpnPage::StartPing(bool selectedOnly)
{
	if (m_pingRunning.load() || m_nodes.empty())
		return;

	std::vector<int> indices;
	if (selectedOnly)
	{
		indices = SelectedIndicesSorted();
		if (indices.empty())
			return;
	}
	else
	{
		for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
			indices.push_back(i);
	}
	StartPingIndices(std::move(indices), "##toolbar");
}

void UiVpnPage::StartTcpPingIndices(std::vector<int> indices)
{
	if (m_pingRunning.load() || indices.empty())
		return;

	std::vector<std::pair<int, std::pair<std::string, int>>> targets;
	targets.reserve(indices.size());
	for (int index : indices)
	{
		if (index < 0 || index >= static_cast<int>(m_nodes.size()))
			continue;
		const VpnNode& node = m_nodes[static_cast<size_t>(index)];
		targets.push_back({ index, { node.server, node.port } });
	}
	if (targets.empty())
		return;

	const bool resumeVpn =
		m_vpnEnabled
		&& m_manager
		&& m_manager->IsRunning();
	const std::vector<VpnNode> nodesSnapshot = m_nodes;
	const VpnStoreSettings settings = BuildStoreSettings();
	const int originalActive = m_activeIndex;

	m_pingCancel.store(false);
	m_pingRunning.store(true);
	m_pingTestRunning = true;
	m_pingGroupKey = "##toolbar";
	SetToolbarStatus(
		targets.size() == 1
			? (resumeVpn ? "Пауза VPN → TCP ping..." : "TCP ping...")
			: (resumeVpn ? "Пауза VPN → TCP ping группы..." : "TCP ping группы..."));

	std::thread([this, targets, resumeVpn, nodesSnapshot, settings, originalActive]()
	{
		VpnNodeProbe::BeginPingIoLane();
		if (resumeVpn && m_manager)
		{
			SetToolbarStatus("Остановка VPN для TCP ping...");
			m_manager->RequestStop();
			for (int i = 0; i < 80; ++i)
			{
				if (m_pingCancel.load()
					|| (!m_manager->IsRunning() && !m_manager->IsOperationInFlight()))
					break;
				Sleep(50);
			}
			Sleep(150);
		}

		if (m_pingCancel.load())
		{
			if (resumeVpn && m_manager && m_vpnEnabled)
				m_manager->RequestStart(nodesSnapshot, originalActive, settings);
			m_pingTestRunning = false;
			m_pingRunning.store(false);
			m_pingGroupKey.clear();
			SetToolbarStatus("TCP ping остановлен.");
			return;
		}

		SetToolbarStatus(targets.size() == 1 ? "TCP ping с ПК..." : "TCP ping с ПК (параллельно)...");

		std::atomic<int> ok { 0 };
		std::atomic<size_t> nextIndex { 0 };
		constexpr size_t kWorkers = 10;
		const size_t workerCount = (std::min)(kWorkers, targets.size());
		std::vector<std::thread> workers;
		workers.reserve(workerCount);

		for (size_t w = 0; w < workerCount; ++w)
		{
			workers.emplace_back([this, &targets, &ok, &nextIndex]()
			{
				VpnNodeProbe::BeginPingIoLane();
				while (!m_pingCancel.load())
				{
					const size_t i = nextIndex.fetch_add(1);
					if (i >= targets.size())
						break;

					const auto& target = targets[i];
					const int pingMs = VpnNodeProbe::TcpPingMs(
						target.second.first,
						target.second.second,
						5000,
						&m_pingCancel);
					if (pingMs >= 0)
						ok.fetch_add(1);

					PendingProbeResult result;
					result.nodeIndex = target.first;
					result.pingMs = pingMs;
					result.speedMbps = -2.f;
					result.ready = true;
					{
						std::lock_guard<std::mutex> lock(m_probeMutex);
						m_pendingProbe.push_back(result);
					}
				}
			});
		}

		for (std::thread& worker : workers)
		{
			if (worker.joinable())
				worker.join();
		}

		if (resumeVpn && m_manager && m_vpnEnabled && !m_pingCancel.load())
		{
			SetToolbarStatus("Восстановление VPN...");
			m_manager->RequestStart(nodesSnapshot, originalActive, settings);
		}

		char status[96];
		if (m_pingCancel.load())
			snprintf(status, sizeof status, "TCP ping остановлен.");
		else
			snprintf(status, sizeof status, "TCP ping: %d/%zu OK", ok.load(), targets.size());
		SetToolbarStatus(status);
		m_pingTestRunning = false;
		m_pingRunning.store(false);
		m_pingGroupKey.clear();
	}).detach();
}

void UiVpnPage::StartPingIndices(std::vector<int> indices, std::string groupKey)
{
	if (m_pingRunning.load() || indices.empty())
		return;

	std::vector<std::pair<int, std::pair<std::string, int>>> targets;
	targets.reserve(indices.size());
	for (int index : indices)
	{
		if (index < 0 || index >= static_cast<int>(m_nodes.size()))
			continue;
		const VpnNode& node = m_nodes[static_cast<size_t>(index)];
		targets.push_back({ index, { node.server, node.port } });
	}
	if (targets.empty())
		return;

	// ICMP/TCP go to resolved public IPs — do not stop VPN, so speed tests
	// on another group can run in parallel.
	m_pingGroupKey = groupKey.empty() ? "##toolbar" : std::move(groupKey);
	m_pingCancel.store(false);
	m_pingRunning.store(true);
	m_pingTestRunning = true;
	SetToolbarStatus(
		targets.size() == 1
			? "ping (ICMP→TCP)..."
			: "ping (ICMP→TCP), параллельно...");

	std::thread([this, targets]()
	{
		VpnNodeProbe::BeginPingIoLane();

		std::atomic<int> ok { 0 };
		std::atomic<int> icmpOk { 0 };
		std::atomic<int> tcpOk { 0 };
		std::atomic<size_t> nextIndex { 0 };
		constexpr size_t kWorkers = 10;
		const size_t workerCount = (std::min)(kWorkers, targets.size());
		std::vector<std::thread> workers;
		workers.reserve(workerCount);

		for (size_t w = 0; w < workerCount; ++w)
		{
			workers.emplace_back([this, &targets, &ok, &icmpOk, &tcpOk, &nextIndex]()
			{
				VpnNodeProbe::BeginPingIoLane();
				while (!m_pingCancel.load())
				{
					const size_t i = nextIndex.fetch_add(1);
					if (i >= targets.size())
						break;

					const auto& target = targets[i];
					VpnNodeProbe::PingKind kind = VpnNodeProbe::PingKind::Failed;
					const int pingMs = VpnNodeProbe::PingWithFallbackMs(
						target.second.first,
						target.second.second,
						4000,
						5000,
						&kind,
						&m_pingCancel);
					if (m_pingCancel.load())
						break;
					if (pingMs >= 0)
					{
						ok.fetch_add(1);
						if (kind == VpnNodeProbe::PingKind::Icmp)
							icmpOk.fetch_add(1);
						else if (kind == VpnNodeProbe::PingKind::Tcp)
							tcpOk.fetch_add(1);
					}

					PendingProbeResult result;
					result.nodeIndex = target.first;
					result.pingMs = pingMs;
					result.speedMbps = -2.f;
					result.ready = true;
					{
						std::lock_guard<std::mutex> lock(m_probeMutex);
						m_pendingProbe.push_back(result);
					}
				}
			});
		}

		for (std::thread& worker : workers)
		{
			if (worker.joinable())
				worker.join();
		}

		char status[128];
		if (m_pingCancel.load())
			snprintf(status, sizeof status, "ping остановлен.");
		else if (tcpOk.load() > 0)
		{
			snprintf(
				status,
				sizeof status,
				"ping: %d/%zu OK (ICMP %d, TCP %d)",
				ok.load(),
				targets.size(),
				icmpOk.load(),
				tcpOk.load());
		}
		else
			snprintf(status, sizeof status, "ping: %d/%zu OK", ok.load(), targets.size());
		SetToolbarStatus(status);
		m_pingTestRunning = false;
		m_pingRunning.store(false);
		m_pingGroupKey.clear();
	}).detach();
}

void UiVpnPage::StartRealPingIndices(std::vector<int> indices)
{
	if (m_pingRunning.load() || m_speedRunning.load() || indices.empty() || !m_manager)
		return;

	std::vector<int> cleaned;
	cleaned.reserve(indices.size());
	for (int index : indices)
	{
		if (index >= 0 && index < static_cast<int>(m_nodes.size()))
			cleaned.push_back(index);
	}
	if (cleaned.empty())
		return;

	const bool resumeVpn = m_vpnEnabled && m_manager->IsRunning();
	const std::vector<VpnNode> nodesSnapshot = m_nodes;
	VpnStoreSettings settings = BuildStoreSettings();
	const int originalActive = m_activeIndex;
	const VpnRoutingPreset preset = VpnRouting::PresetFromWorkMode(settings.workMode);

	m_pingCancel.store(false);
	m_pingRunning.store(true);
	m_pingTestRunning = true;
	m_pingGroupKey = "##toolbar";
	SetToolbarStatus(
		cleaned.size() == 1 ? "RealPing..." : "RealPing группы...");

	std::thread([this, cleaned, resumeVpn, nodesSnapshot, settings, originalActive, preset]()
	{
		VpnNodeProbe::BeginPingIoLane();
		constexpr int kBatchSize = 10;
		constexpr const char* kPingUrl = "https://www.gstatic.com/generate_204";
		constexpr int kPingTimeoutMs = 5000;

		auto waitIdle = [this]()
		{
			for (int i = 0; i < 120; ++i)
			{
				if (m_pingCancel.load())
					break;
				if (!m_manager->IsOperationInFlight()
					&& m_manager->GetRunStatus() != VpnRunStatus::Starting)
					break;
				Sleep(50);
			}
		};

		if (m_manager->IsRunning() || m_manager->IsOperationInFlight())
		{
			SetToolbarStatus("RealPing: остановка VPN...");
			m_manager->RequestStop();
			waitIdle();
			for (int i = 0; i < 80; ++i)
			{
				if (m_pingCancel.load() || !m_manager->IsRunning())
					break;
				Sleep(50);
			}
			Sleep(150);
		}

		if (m_pingCancel.load())
		{
			if (resumeVpn && m_vpnEnabled)
				m_manager->RequestStart(nodesSnapshot, originalActive, settings);
			m_pingTestRunning = false;
			m_pingRunning.store(false);
			m_pingGroupKey.clear();
			SetToolbarStatus("RealPing остановлен.");
			return;
		}

		int ok = 0;
		const std::wstring cacheDir = ZapretPaths::GetCacheDirectory();
		VpnStoreSettings probeSettings = settings;
		probeSettings.transportMode = 0; // force Proxy DNS path; TUN already disabled in builder

		for (size_t batchStart = 0; batchStart < cleaned.size(); batchStart += static_cast<size_t>(kBatchSize))
		{
			if (m_pingCancel.load())
				break;

			const size_t batchEnd = (std::min)(cleaned.size(), batchStart + static_cast<size_t>(kBatchSize));
			std::vector<int> batch(cleaned.begin() + static_cast<std::ptrdiff_t>(batchStart),
				cleaned.begin() + static_cast<std::ptrdiff_t>(batchEnd));

			char status[128];
			snprintf(
				status,
				sizeof status,
				"RealPing [%zu/%zu]...",
				batchEnd,
				cleaned.size());
			SetToolbarStatus(status);

			const int mixedPort = VpnManager::AllocateFreeTcpPort(VpnManager::kDefaultMixedPort);
			int apiPort = VpnManager::AllocateFreeTcpPort(VpnManager::kDefaultApiPort);
			if (apiPort == mixedPort)
				apiPort = VpnManager::AllocateFreeTcpPort(0);
			const int portBase = VpnManager::AllocateFreeTcpPort(11800);

			std::vector<VpnConfigBuilder::ParallelProbeEndpoint> endpoints;
			std::string buildError;
			if (!VpnConfigBuilder::WriteParallelProbeConfig(
					nodesSnapshot,
					batch,
					originalActive,
					preset,
					probeSettings,
					cacheDir,
					mixedPort,
					apiPort,
					portBase,
					endpoints,
					buildError))
			{
				SetToolbarStatus(buildError.empty() ? "RealPing: ошибка конфига." : buildError);
				for (int index : batch)
				{
					PendingProbeResult result;
					result.nodeIndex = index;
					result.pingMs = -1;
					result.speedMbps = -2.f;
					result.ready = true;
					std::lock_guard<std::mutex> lock(m_probeMutex);
					m_pendingProbe.push_back(result);
				}
				continue;
			}

			if (!m_manager->StartFromExistingConfig(mixedPort, apiPort))
			{
				SetToolbarStatus(
					m_manager->GetErrorMessage().empty()
						? "RealPing: не удалось запустить mihomo."
						: m_manager->GetErrorMessage());
				for (int index : batch)
				{
					PendingProbeResult result;
					result.nodeIndex = index;
					result.pingMs = -1;
					result.speedMbps = -2.f;
					result.ready = true;
					std::lock_guard<std::mutex> lock(m_probeMutex);
					m_pendingProbe.push_back(result);
				}
				continue;
			}

			Sleep(1000); // v2rayN core warm-up

			if (m_pingCancel.load())
			{
				m_manager->Stop();
				break;
			}

			const int probeApiPort = m_manager->GetApiPort();
			std::atomic<int> batchOk { 0 };
			std::vector<std::thread> workers;
			workers.reserve(endpoints.size());
			for (const VpnConfigBuilder::ParallelProbeEndpoint& endpoint : endpoints)
			{
				workers.emplace_back([this, endpoint, &batchOk, kPingUrl, kPingTimeoutMs, probeApiPort]()
				{
					if (m_pingCancel.load())
						return;
					// mihomo /proxies/{name}/delay — как Clash Meta / v2rayN core delay,
					// без раздувания WinInet HTTP-proxy RTT.
					int pingMs = MihomoApi::GetProxyDelayMs(
						probeApiPort,
						endpoint.proxyTag,
						kPingUrl,
						kPingTimeoutMs);
					if (pingMs < 0)
					{
						pingMs = VpnNodeProbe::HttpRealPingMs(
							"127.0.0.1",
							endpoint.port,
							kPingUrl,
							kPingTimeoutMs);
					}
					if (pingMs >= 0)
						batchOk.fetch_add(1);

					PendingProbeResult result;
					result.nodeIndex = endpoint.nodeIndex;
					result.pingMs = pingMs;
					result.speedMbps = -2.f;
					result.ready = true;
					{
						std::lock_guard<std::mutex> lock(m_probeMutex);
						m_pendingProbe.push_back(result);
					}
				});
			}

			for (std::thread& worker : workers)
			{
				if (worker.joinable())
					worker.join();
			}
			ok += batchOk.load();

			m_manager->Stop();
			waitIdle();
			Sleep(100);
		}

		if (resumeVpn && m_vpnEnabled && !m_pingCancel.load())
		{
			SetToolbarStatus("Восстановление VPN...");
			m_manager->RequestStart(nodesSnapshot, originalActive, settings);
		}
		else if (!resumeVpn)
		{
			// Leave stopped after probe; SyncVpnRuntime won't restart unless toggle on.
			m_lastAppliedVpnEnabled = false;
		}

		char done[96];
		if (m_pingCancel.load())
			snprintf(done, sizeof done, "RealPing остановлен.");
		else
			snprintf(done, sizeof done, "RealPing: %d/%zu OK", ok, cleaned.size());
		SetToolbarStatus(done);
		m_pingTestRunning = false;
		m_pingRunning.store(false);
		m_pingGroupKey.clear();
	}).detach();
}

void UiVpnPage::StartSpeedTest(bool selectedOnly)
{
	if (m_speedRunning.load())
		return;

	std::vector<int> indices;
	if (selectedOnly)
	{
		indices = SelectedIndicesSorted();
		if (indices.empty())
			return;
	}
	else
	{
		if (!HasActiveServer())
		{
			SetToolbarStatus("Нет активного сервера для теста скорости.");
			return;
		}
		indices.push_back(m_activeIndex);
	}
	StartSpeedTestIndices(std::move(indices), "##toolbar");
}

void UiVpnPage::StartSpeedTestIndices(std::vector<int> indices, std::string groupKey)
{
	if (m_speedRunning.load() || indices.empty() || !m_manager)
		return;

	std::vector<int> cleaned;
	cleaned.reserve(indices.size());
	for (int index : indices)
	{
		if (index >= 0 && index < static_cast<int>(m_nodes.size()))
			cleaned.push_back(index);
	}
	if (cleaned.empty())
		return;

	const bool resumeVpn = m_vpnEnabled && m_manager->IsRunning();
	const std::vector<VpnNode> nodesSnapshot = m_nodes;
	VpnStoreSettings settings = BuildStoreSettings();
	const int originalActive = m_activeIndex;
	const VpnRoutingPreset preset = VpnRouting::PresetFromWorkMode(settings.workMode);

	m_speedGroupKey = groupKey.empty() ? "##toolbar" : std::move(groupKey);
	m_speedCancel.store(false);
	m_speedRunning.store(true);
	m_speedTestRunning = true;
	SetToolbarStatus(
		cleaned.size() == 1 ? "Тест скорости..." : "Тест скорости (параллельно)...");

	std::thread([this, cleaned, resumeVpn, nodesSnapshot, settings, originalActive, preset]()
	{
		VpnNodeProbe::BeginSpeedIoLane();

		// Parallel speed probes per pass (was 5 like v2rayN default).
		constexpr int kBatchSize = 10;
		constexpr const char* kUrl = "https://cachefly.cachefly.net/50mb.test";
		constexpr const char* kPingUrl = "https://www.google.com/generate_204";
		constexpr int kTimeoutMs = 10000;
		constexpr int kPingTimeoutMs = 9000;

		auto waitIdle = [this]()
		{
			for (int i = 0; i < 120; ++i)
			{
				if (m_speedCancel.load())
					break;
				if (!m_manager->IsOperationInFlight()
					&& m_manager->GetRunStatus() != VpnRunStatus::Starting)
					break;
				Sleep(50);
			}
		};

		if (m_manager->IsRunning() || m_manager->IsOperationInFlight())
		{
			SetToolbarStatus("Тест скорости: остановка VPN...");
			m_manager->RequestStop();
			waitIdle();
			for (int i = 0; i < 80; ++i)
			{
				if (m_speedCancel.load() || !m_manager->IsRunning())
					break;
				Sleep(50);
			}
			Sleep(150);
		}

		if (m_speedCancel.load())
		{
			if (resumeVpn && m_vpnEnabled)
				m_manager->RequestStart(nodesSnapshot, originalActive, settings);
			m_speedTestRunning = false;
			m_speedRunning.store(false);
			SetToolbarStatus("Тест скорости остановлен.");
			return;
		}

		int ok = 0;
		const std::wstring cacheDir = ZapretPaths::GetCacheDirectory();
		VpnStoreSettings probeSettings = settings;
		probeSettings.transportMode = 0;

		for (size_t batchStart = 0; batchStart < cleaned.size(); batchStart += static_cast<size_t>(kBatchSize))
		{
			if (m_speedCancel.load())
				break;

			const size_t batchEnd = (std::min)(cleaned.size(), batchStart + static_cast<size_t>(kBatchSize));
			std::vector<int> batch(
				cleaned.begin() + static_cast<std::ptrdiff_t>(batchStart),
				cleaned.begin() + static_cast<std::ptrdiff_t>(batchEnd));

			char status[160];
			snprintf(
				status,
				sizeof status,
				"Скорость [%zu/%zu] (×%zu)...",
				batchEnd,
				cleaned.size(),
				batch.size());
			SetToolbarStatus(status);

			const int mixedPort = VpnManager::AllocateFreeTcpPort(VpnManager::kDefaultMixedPort);
			int apiPort = VpnManager::AllocateFreeTcpPort(VpnManager::kDefaultApiPort);
			if (apiPort == mixedPort)
				apiPort = VpnManager::AllocateFreeTcpPort(0);
			const int portBase = VpnManager::AllocateFreeTcpPort(11800);

			std::vector<VpnConfigBuilder::ParallelProbeEndpoint> endpoints;
			std::string buildError;
			if (!VpnConfigBuilder::WriteParallelProbeConfig(
					nodesSnapshot,
					batch,
					originalActive,
					preset,
					probeSettings,
					cacheDir,
					mixedPort,
					apiPort,
					portBase,
					endpoints,
					buildError))
			{
				SetToolbarStatus(buildError.empty() ? "Тест скорости: ошибка конфига." : buildError);
				for (int index : batch)
				{
					PendingProbeResult result;
					result.nodeIndex = index;
					result.pingMs = -1;
					result.speedMbps = -1.f;
					result.ready = true;
					std::lock_guard<std::mutex> lock(m_probeMutex);
					m_pendingProbe.push_back(result);
				}
				continue;
			}

			if (!m_manager->StartFromExistingConfig(mixedPort, apiPort))
			{
				SetToolbarStatus(
					m_manager->GetErrorMessage().empty()
						? "Тест скорости: не удалось запустить mihomo."
						: m_manager->GetErrorMessage());
				for (int index : batch)
				{
					PendingProbeResult result;
					result.nodeIndex = index;
					result.pingMs = -1;
					result.speedMbps = -1.f;
					result.ready = true;
					std::lock_guard<std::mutex> lock(m_probeMutex);
					m_pendingProbe.push_back(result);
				}
				continue;
			}

			for (int warm = 0; warm < 20 && !m_speedCancel.load(); ++warm)
				Sleep(50); // v2rayN core warm-up (~1s), abortable

			if (m_speedCancel.load())
			{
				m_manager->Stop();
				break;
			}

			std::atomic<int> batchOk { 0 };
			std::vector<std::thread> workers;
			workers.reserve(endpoints.size());

			for (const VpnConfigBuilder::ParallelProbeEndpoint& endpoint : endpoints)
			{
				workers.emplace_back([this, endpoint, &batchOk, kUrl, kPingUrl, kTimeoutMs, kPingTimeoutMs]()
				{
					VpnNodeProbe::BeginSpeedIoLane();
					if (m_speedCancel.load())
						return;

					const int nodeIndex = endpoint.nodeIndex;
					auto pushLiveSpeed = [this, nodeIndex](float peakMBps)
					{
						if (peakMBps < 0.f)
							return;
						PendingProbeResult result;
						result.nodeIndex = nodeIndex;
						result.pingMs = -2;
						result.speedMbps = peakMBps;
						result.ready = true;
						result.live = true;
						{
							std::lock_guard<std::mutex> lock(m_probeMutex);
							for (auto it = m_pendingProbe.begin(); it != m_pendingProbe.end();)
							{
								if (it->nodeIndex == nodeIndex && it->pingMs == -2 && it->live)
									it = m_pendingProbe.erase(it);
								else
									++it;
							}
							m_pendingProbe.push_back(result);
						}
					};

					int pingMs = VpnNodeProbe::HttpRealPingMs(
						"127.0.0.1",
						endpoint.port,
						kPingUrl,
						kPingTimeoutMs);

					{
						PendingProbeResult pingResult;
						pingResult.nodeIndex = nodeIndex;
						pingResult.pingMs = pingMs;
						pingResult.speedMbps = -2.f;
						pingResult.ready = true;
						std::lock_guard<std::mutex> lock(m_probeMutex);
						m_pendingProbe.push_back(pingResult);
					}

					float peakMBps = -1.f;
					if (pingMs > 0 && !m_speedCancel.load())
					{
						peakMBps = VpnNodeProbe::MeasureDownloadPeakMBps(
							"127.0.0.1",
							endpoint.port,
							kUrl,
							kTimeoutMs,
							&m_speedCancel,
							pushLiveSpeed);
					}

					if (peakMBps >= 0.f)
						batchOk.fetch_add(1);

					PendingProbeResult result;
					result.nodeIndex = nodeIndex;
					result.pingMs = -2;
					result.speedMbps = peakMBps;
					result.ready = true;
					{
						std::lock_guard<std::mutex> lock(m_probeMutex);
						m_pendingProbe.push_back(result);
					}
				});
			}

			for (std::thread& worker : workers)
			{
				if (worker.joinable())
					worker.join();
			}
			ok += batchOk.load();

			m_manager->Stop();
			waitIdle();
			Sleep(100);
		}

		if (resumeVpn && m_vpnEnabled && !m_speedCancel.load())
		{
			SetToolbarStatus("Восстановление VPN...");
			m_manager->RequestStart(nodesSnapshot, originalActive, settings);
		}
		else if (!resumeVpn)
		{
			m_lastAppliedVpnEnabled = false;
		}

		char done[96];
		if (m_speedCancel.load())
			snprintf(done, sizeof done, "Тест скорости остановлен.");
		else
			snprintf(done, sizeof done, "Скорость: %d/%zu OK", ok, cleaned.size());
		SetToolbarStatus(done);

		m_speedTestRunning = false;
		m_speedRunning.store(false);
		m_speedGroupKey.clear();
	}).detach();
}

void UiVpnPage::StopPing()
{
	if (!m_pingRunning.load() && !m_pingTestRunning)
		return;
	m_pingCancel.store(true);
	VpnNodeProbe::AbortPingIo();
	SetToolbarStatus("Остановка ping...");
}

void UiVpnPage::StopSpeed()
{
	if (!m_speedRunning.load() && !m_speedTestRunning)
		return;
	m_speedCancel.store(true);
	VpnNodeProbe::AbortSpeedIo();
	SetToolbarStatus("Остановка теста скорости...");
}

void UiVpnPage::StopProbe()
{
	StopPing();
	StopSpeed();
}

bool UiVpnPage::AnyProbeBusy() const
{
	return m_pingRunning.load() || m_speedRunning.load() || m_pingTestRunning || m_speedTestRunning;
}

void UiVpnPage::DeleteSelectedServer()
{
	const std::vector<int> selected = SelectedIndicesSorted();
	if (selected.empty() || AnyProbeBusy())
		return;

	if (selected.size() == 1)
	{
		const int removed = selected.front();
		m_nodes.erase(m_nodes.begin() + removed);
		if (m_activeIndex == removed)
			m_activeIndex = m_nodes.empty() ? -1 : 0;
		else if (m_activeIndex > removed)
			--m_activeIndex;
		if (m_detailIndex == removed)
		{
			m_detailIndex = -1;
			m_view = View::List;
		}
		else if (m_detailIndex > removed)
			--m_detailIndex;
		ClearSelection();
		SaveStore();
		SetToolbarStatus("Сервер удалён.");
		return;
	}

	DeleteGroupServers(selected);
}

void UiVpnPage::DeleteGroupServers(const std::vector<int>& indices)
{
	if (indices.empty() || AnyProbeBusy())
		return;

	std::vector<int> ordered = indices;
	std::sort(ordered.begin(), ordered.end(), std::greater<int>());

	int removedCount = 0;
	for (int index : ordered)
	{
		if (index < 0 || index >= static_cast<int>(m_nodes.size()))
			continue;

		m_nodes.erase(m_nodes.begin() + index);
		++removedCount;

		if (m_activeIndex == index)
			m_activeIndex = m_nodes.empty() ? -1 : (std::min)(index, static_cast<int>(m_nodes.size()) - 1);
		else if (m_activeIndex > index)
			--m_activeIndex;

		if (m_detailIndex == index)
		{
			m_detailIndex = -1;
			m_view = View::List;
		}
		else if (m_detailIndex > index)
			--m_detailIndex;
	}

	if (removedCount <= 0)
		return;

	if (m_activeIndex >= static_cast<int>(m_nodes.size()))
		m_activeIndex = m_nodes.empty() ? -1 : static_cast<int>(m_nodes.size()) - 1;

	ClearSelection();
	SaveStore();
	char status[96];
	snprintf(status, sizeof status, "Удалено серверов: %d", removedCount);
	SetToolbarStatus(status);
}

void UiVpnPage::RenameGroup(const std::string& oldGroupKey, const std::string& newDisplayName)
{
	std::string trimmed = newDisplayName;
	while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
		trimmed.erase(trimmed.begin());
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
		trimmed.pop_back();
	if (trimmed.empty())
	{
		SetToolbarStatus("Имя группы не может быть пустым.");
		return;
	}

	std::string newKey = trimmed;
	if (newKey == "Моё импортированное" || newKey == "Imported")
		newKey = "Imported";

	if (newKey == oldGroupKey)
		return;

	const std::string storedGroup = (newKey == "Imported") ? std::string {} : newKey;
	int renamed = 0;
	for (VpnNode& node : m_nodes)
	{
		const std::string key = node.group.empty() ? "Imported" : node.group;
		if (key != oldGroupKey)
			continue;
		node.group = storedGroup;
		++renamed;
	}
	if (renamed <= 0)
		return;

	if (auto it = m_groupOpen.find(oldGroupKey); it != m_groupOpen.end())
	{
		m_groupOpen[newKey] = it->second;
		m_groupOpen.erase(it);
	}
	if (auto it = m_groupAppear.find(oldGroupKey); it != m_groupAppear.end())
	{
		m_groupAppear[newKey] = it->second;
		m_groupAppear.erase(it);
	}

	SaveStore();
	SetToolbarStatus("Группа переименована.");
}

void UiVpnPage::ExportOutboundJson(int nodeIndex)
{
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
		return;

	const VpnNode& node = m_nodes[static_cast<size_t>(nodeIndex)];
	const std::string json = VpnNodeProbe::BuildOutboundJson(node);
	if (VpnNodeProbe::CopyUtf8ToClipboard(json))
		SetToolbarStatus("Outbound JSON скопирован в буфер.");
	else
		SetToolbarStatus("Не удалось скопировать JSON в буфер.");
}

void UiVpnPage::ExportRuntimeConfig(int nodeIndex)
{
	if (nodeIndex < 0 || nodeIndex >= static_cast<int>(m_nodes.size()))
		return;

	const VpnNode& node = m_nodes[static_cast<size_t>(nodeIndex)];
	const VpnStoreSettings settings = BuildStoreSettings();
	const VpnRoutingPreset preset = VpnRouting::PresetFromWorkMode(settings.workMode);
	const std::wstring cacheDir = ZapretPaths::GetCacheDirectory();
	const int mixedPort = m_manager ? m_manager->GetMixedPort() : VpnManager::kDefaultMixedPort;
	const int apiPort = m_manager ? m_manager->GetApiPort() : VpnManager::kDefaultApiPort;
	std::string error;
	if (!VpnConfigBuilder::WriteRuntimeConfig(node, preset, settings, cacheDir, mixedPort, apiPort, error, false))
	{
		SetToolbarStatus(error.empty() ? "Не удалось собрать runtime конфиг." : error);
		return;
	}

	const std::filesystem::path configPath = std::filesystem::path(cacheDir) / L"config.yaml";
	std::ifstream input(configPath, std::ios::binary);
	if (!input)
	{
		SetToolbarStatus("config.yaml не найден после сборки.");
		return;
	}
	std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (VpnNodeProbe::CopyUtf8ToClipboard(body))
		SetToolbarStatus("Runtime config.yaml скопирован в буфер.");
	else
		SetToolbarStatus("Конфиг собран, но буфер недоступен.");
}

void UiVpnPage::UpdateRuntime()
{
	EnsureStoreLoaded();
	ApplyPendingImportIfAny();
	ApplyPendingGeoLookups();
	ApplyPendingProbeResults();
	SyncVpnRuntime();
}

bool UiVpnPage::HasActiveServer() const
{
	return m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_nodes.size());
}

std::string UiVpnPage::GetActiveServerLabel() const
{
	if (!HasActiveServer())
		return "Сервер не выбран";
	const VpnNode& node = m_nodes[static_cast<size_t>(m_activeIndex)];
	if (!node.name.empty())
		return node.name;
	if (!node.server.empty())
		return node.server;
	return "Сервер #" + std::to_string(m_activeIndex + 1);
}

std::string UiVpnPage::GetActiveServerShareUri() const
{
	if (!HasActiveServer())
		return {};
	return m_nodes[static_cast<size_t>(m_activeIndex)].originalUri;
}

void UiVpnPage::SetWorkModeFromTray(int workMode)
{
	EnsureStoreLoaded();
	if (workMode < 1 || workMode > 4)
		return;
	if (m_workMode == workMode)
		return;
	m_workMode = workMode;
	SaveStore();
}

void UiVpnPage::SetTransportModeFromTray(int transportMode)
{
	EnsureStoreLoaded();
	if (transportMode < 0 || transportMode > 1)
		return;
	if (m_transportMode == transportMode)
		return;
	m_transportMode = transportMode;
	SaveStore();
}

std::string UiVpnPage::GetServerTrayLabel(int index) const
{
	if (index < 0 || index >= static_cast<int>(m_nodes.size()))
		return {};
	const VpnNode& node = m_nodes[static_cast<size_t>(index)];
	std::string label;
	if (!node.country.empty())
	{
		label = node.country;
		label += " · ";
	}
	if (!node.name.empty())
		label += node.name;
	else if (!node.server.empty())
		label += node.server;
	else
		label += "Сервер #" + std::to_string(index + 1);
	constexpr size_t kMaxLen = 48;
	if (label.size() > kMaxLen)
	{
		label.resize(kMaxLen - 1);
		label += "…";
	}
	return label;
}

void UiVpnPage::SelectServerFromTray(int nodeIndex)
{
	EnsureStoreLoaded();
	SetActiveServer(nodeIndex);
}

std::string UiVpnPage::GetActiveServerPresenceLabel() const
{
	if (!HasActiveServer() || !m_vpnEnabled)
		return {};

	const VpnNode& node = m_nodes[static_cast<size_t>(m_activeIndex)];
	std::string country = VpnGeo::CountryCodeToName(node.country);
	if (country.empty() && !node.country.empty())
		country = node.country; // fallback ISO only if name missing
	if (country.empty())
		country = "Сервер";

	const VpnStoreSettings settings = BuildStoreSettings();
	const char* transport = settings.transportMode == 0 ? "Proxy" : "TUN";
	const char* routing = settings.workMode == 4 ? "Своя маршрутизация" : "RUv1";

	// Discord often renders flag emoji as "pl"/"PL" — no flag, plain text only.
	char label[160] = {};
	snprintf(label, sizeof label, "%s · %s · %s", country.c_str(), transport, routing);

	constexpr size_t kMaxDiscordDetails = 120;
	std::string out = label;
	if (out.size() > kMaxDiscordDetails)
		out.resize(kMaxDiscordDetails);
	return out;
}

void UiVpnPage::SyncVpnRuntime()
{
	if (!m_manager)
		return;

	// Don't fight per-node proxy switches during real-ping / speed probes.
	if (m_speedTestRunning || m_speedRunning.load())
		return;

	// Module binary replace requires VPN stopped — don't restart mid-update.
	if (VpnModuleUpdateApply::Instance().IsApplyingAny())
		return;

	// Wait out in-flight start/stop — Start() begins with Stop(), which briefly
	// reports Stopped and used to cancel autostart via the failure path below.
	if (m_manager->IsOperationInFlight()
		|| m_manager->GetRunStatus() == VpnRunStatus::Starting)
		return;

	if (m_vpnEnabled
		&& m_lastAppliedVpnEnabled
		&& !m_manager->IsRunning()
		&& m_manager->GetRunStatus() == VpnRunStatus::Stopped
		&& !m_manager->GetErrorMessage().empty())
	{
		std::string readyErr;
		if (!m_manager->IsRuntimeReady(readyErr))
			return; // still waiting for rules/wintun — readiness loop below

		// Keep the toggle on and allow retry (TUN may need a moment).
		AppLog::Instance().Append(
			LogSource::VpnRouting,
			std::string("VPN старт не удался, повтор: ") + m_manager->GetErrorMessage());
		m_lastAppliedVpnEnabled = false;
		m_vpnRetryAfterTick = GetTickCount64() + 2000;
		return;
	}

	if (!m_vpnEnabled)
	{
		if (m_lastAppliedVpnEnabled)
		{
			m_manager->RequestStop();
			m_lastAppliedVpnEnabled = false;
		}
		m_vpnRetryAfterTick = 0;
		return;
	}

	const VpnStoreSettings settings = BuildStoreSettings();
	const bool transportUpToDate =
		m_lastAppliedVpnEnabled &&
		SameTransportSettings(settings, m_lastAppliedSettings) &&
		m_activeIndex == m_lastAppliedActiveIndex &&
		m_manager->IsRunning();

	if (transportUpToDate
		&& settings.routingRevision != m_manager->GetAppliedRoutingRevision())
	{
		m_manager->RequestReload(m_nodes, m_activeIndex, settings);
		return;
	}

	const bool runtimeUpToDate =
		transportUpToDate &&
		settings.routingRevision == m_manager->GetAppliedRoutingRevision();

	if (runtimeUpToDate)
		return;

	if (m_vpnRetryAfterTick != 0 && GetTickCount64() < m_vpnRetryAfterTick)
		return;

	if (!HasActiveServer())
		return;

	std::string readyError;
	if (!m_manager->IsRuntimeReady(readyError))
	{
		if (!m_waitingForRuntime)
		{
			m_waitingForRuntime = true;
			AppLog::Instance().Append(LogSource::VpnRouting, readyError);
		}
		return; // wait by readiness, not by timer
	}
	if (m_waitingForRuntime)
	{
		m_waitingForRuntime = false;
		AppLog::Instance().Append(LogSource::VpnRouting, "VPN runtime готов, запускаем...");
	}

	m_manager->RequestStart(m_nodes, m_activeIndex, settings);
	m_lastAppliedVpnEnabled = true;
	m_lastAppliedWorkMode = m_workMode;
	m_lastAppliedActiveIndex = m_activeIndex;
	m_lastAppliedSettings = settings;
	m_vpnRetryAfterTick = 0;
}

void UiVpnPage::DrawContent(ThemeManager& theme, FontManager& fonts, float width)
{
	EnsureStoreLoaded();
	ApplyPendingImportIfAny();
	ApplyPendingGeoLookups();
	ApplyPendingProbeResults();
	QueueCountryLookups();

	if (m_view == View::Detail && (m_detailIndex < 0 || m_detailIndex >= static_cast<int>(m_nodes.size())))
		m_view = View::List;

	// VPN tooltips appear twice as fast as the global 2.5s delay.
	ImGuiStyle& style = ImGui::GetStyle();
	const float prevHoverDelay = style.HoverDelayNormal;
	style.HoverDelayNormal = prevHoverDelay * 0.5f;

	if (m_view == View::List
		&& UiCommon::IsMouseNavForwardClicked()
		&& m_detailIndex >= 0
		&& m_detailIndex < static_cast<int>(m_nodes.size()))
	{
		m_view = View::Detail;
	}

	if (m_view == View::List)
		DrawListView(theme, fonts, width);
	else
		DrawDetailView(theme, fonts, width);

	style.HoverDelayNormal = prevHoverDelay;
	SyncVpnRuntime();
}

void UiVpnPage::DrawListView(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	const UiAccentColors accents = theme.GetAccents();
	const float deltaTime = ImGui::GetIO().DeltaTime;

	// Smooth fade of "new probe result" row highlights.
	for (auto it = m_probeFlash.begin(); it != m_probeFlash.end(); )
	{
		it->second -= deltaTime / 1.35f;
		if (it->second <= 0.01f)
			it = m_probeFlash.erase(it);
		else
			++it;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, UiMetrics::kRowGap));
	if (DrawServersPageHeader(fonts, width, m_vpnEnabled, m_vpnMix, m_fixDiscord, colors))
		SaveStore();

	DrawVpnModulesUpdateRow(m_manager, colors, accents);

	const float filterGap = UiMetrics::kGridGap;
	const float transportW = (std::max)(150.f, width * 0.22f);
	const float filterSearchW = width * 0.30f;
	const float comboW = (std::max)(120.f, width - filterSearchW - transportW - filterGap * 2.f);

	UiCommon::PushInputStyle(colors);
	ImGui::SetNextItemWidth(filterSearchW);
	if (ImGui::InputTextWithHint("##search", "Поиск серверов и групп", m_search, sizeof m_search))
		ClearSelection();
	ImGui::SameLine(0.f, filterGap);
	ImGui::SetNextItemWidth(comboW);
	const int previousWorkMode = m_workMode;
	int uiWorkMode = WorkModeToUiIndex(m_workMode);
	ImGui::Combo("##work_mode", &uiWorkMode, kWorkModes, 4);
	m_workMode = UiIndexToWorkMode(uiWorkMode);
	ImGui::SameLine(0.f, filterGap);
	ImGui::SetNextItemWidth(transportW);
	const int previousTransportMode = m_transportMode;
	ImGui::Combo("##transport_mode", &m_transportMode, kTransportModes, 2);
	if (m_workMode != previousWorkMode || m_transportMode != previousTransportMode)
		SaveStore();
	UiCommon::PopInputStyle();

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));

	const bool hasSelection = HasSelection();
	const int selectionCount = SelectionCount();
	const bool hasAnchor =
		m_selected >= 0 && m_selected < static_cast<int>(m_nodes.size());
	const bool hasActive = m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_nodes.size());

	const ImGuiIO& io = ImGui::GetIO();
	const bool wantImportShortcut = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !io.WantTextInput;
	const bool wantDeleteShortcut =
		!io.WantTextInput
		&& hasSelection
		&& !AnyProbeBusy()
		&& ImGui::IsKeyPressed(ImGuiKey_Delete);

	if (ToolbarIconButton(fonts, 0xE710, "Импорт из буфера (Ctrl+V)", colors, !m_importRunning.load())
		|| wantImportShortcut)
	{
		StartImportFromClipboard();
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE735, "Сделать активным", colors, hasAnchor)
		&& hasAnchor)
	{
		SetActiveServer(m_selected);
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE70F, "Редактировать", colors, hasAnchor && !AnyProbeBusy()))
		OpenSelectedDetails();
	ImGui::SameLine(0.f, 2.f);
	if (m_pingTestRunning)
	{
		if (ToolbarIconButton(fonts, 0xE711, "Остановить ping", colors))
			StopPing();
	}
	else if (ToolbarIconButton(
			fonts,
			0xE724,
			selectionCount > 1 ? "ping выбранных (ICMP→TCP)" : "ping (ICMP→TCP)",
			colors,
			hasSelection && !m_pingRunning.load()))
	{
		StartPing(true);
	}
	ImGui::SameLine(0.f, 2.f);
	if (m_speedTestRunning)
	{
		if (ToolbarIconButton(fonts, 0xE711, "Остановить тест скорости", colors))
			StopSpeed();
	}
	else if (ToolbarIconButton(
			fonts,
			0xE9F5,
			selectionCount > 1 ? "Тест скорости выбранных" : "Тест скорости выбранного",
			colors,
			hasSelection && !m_speedRunning.load()))
	{
		StartSpeedTest(true);
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(
			fonts,
			0xE769,
			"Тест скорости активного",
			colors,
			hasActive && !m_speedRunning.load()))
		StartSpeedTest(false);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE74E, "Экспорт outbound JSON", colors, hasAnchor))
		ExportOutboundJson(m_selected);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE943, "Экспорт runtime конфига", colors, hasAnchor))
		ExportRuntimeConfig(m_selected);
	ImGui::SameLine(0.f, 2.f);
	if ((ToolbarIconButton(
			 fonts,
			 0xE74D,
			 selectionCount > 1 ? "Удалить выбранные (Delete)" : "Удалить выбранный (Delete)",
			 colors,
			 hasSelection && !AnyProbeBusy())
			|| wantDeleteShortcut)
		&& hasSelection)
	{
		DeleteSelectedServer();
	}
	ImGui::SameLine(0.f, 2.f);
	const bool canMoveUp = hasAnchor && m_selected > 0 && !AnyProbeBusy();
	const bool canMoveDown = hasAnchor && m_selected + 1 < static_cast<int>(m_nodes.size()) && !AnyProbeBusy();
	if (ToolbarIconButton(fonts, 0xE70E, "Переместить вверх", colors, canMoveUp))
	{
		std::swap(m_nodes[static_cast<size_t>(m_selected)], m_nodes[static_cast<size_t>(m_selected - 1)]);
		if (m_activeIndex == m_selected)
			m_activeIndex = m_selected - 1;
		else if (m_activeIndex == m_selected - 1)
			m_activeIndex = m_selected;
		const int newIndex = m_selected - 1;
		if (m_detailIndex == m_selected)
			m_detailIndex = newIndex;
		else if (m_detailIndex == newIndex)
			m_detailIndex = m_selected;
		SelectOnly(newIndex);
		SaveStore();
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE70D, "Переместить вниз", colors, canMoveDown))
	{
		std::swap(m_nodes[static_cast<size_t>(m_selected)], m_nodes[static_cast<size_t>(m_selected + 1)]);
		if (m_activeIndex == m_selected)
			m_activeIndex = m_selected + 1;
		else if (m_activeIndex == m_selected + 1)
			m_activeIndex = m_selected;
		const int newIndex = m_selected + 1;
		if (m_detailIndex == m_selected)
			m_detailIndex = newIndex;
		else if (m_detailIndex == newIndex)
			m_detailIndex = m_selected;
		SelectOnly(newIndex);
		SaveStore();
	}

	if (selectionCount > 1)
	{
		ImGui::SameLine(0.f, 10.f);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::Text("Выбрано: %d", selectionCount);
		ImGui::PopStyleColor();
	}

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));

	{
		std::string status;
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			if (!m_importStatus.empty())
				status = m_importStatus;
		}
		if (status.empty())
		{
			std::lock_guard<std::mutex> lock(m_probeMutex);
			status = m_toolbarStatus;
		}
		if (!status.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
			ImGui::TextWrapped("%s", status.c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0.f, 4.f));
		}
	}

	if (m_manager)
	{
		if (!m_manager->GetErrorMessage().empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, accents.fail);
			ImGui::TextWrapped("%s", m_manager->GetErrorMessage().c_str());
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0.f, 4.f));
		}
		else if (!m_manager->GetStatusMessage().empty() && m_manager->IsRunning())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, accents.ok);
			if (hasActive)
			{
				const VpnNode& activeNode = m_nodes[static_cast<size_t>(m_activeIndex)];
				char statusLine[256];
				snprintf(
					statusLine,
					sizeof statusLine,
					"%s Активный профиль: %s",
					m_manager->GetStatusMessage().c_str(),
					activeNode.name.c_str());
				ImGui::TextWrapped("%s", statusLine);
			}
			else
			{
				ImGui::TextWrapped("%s", m_manager->GetStatusMessage().c_str());
			}
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0.f, 4.f));
		}
	}

	const float tableWidth = width;

	std::vector<std::string> groupOrder;
	groupOrder.reserve(8);
	for (const VpnNode& node : m_nodes)
	{
		const std::string& group = node.group.empty() ? "Imported" : node.group;
		if (std::find(groupOrder.begin(), groupOrder.end(), group) == groupOrder.end())
			groupOrder.push_back(group);
	}
	if (groupOrder.empty())
		groupOrder.push_back("Imported");

	char pingBuf[24];
	char speedBuf[24];
	int displayIndex = 0;

	VpnStoreSettings uiSettings;
	m_store.LoadSettings(uiSettings);
	const std::string subscriptionRemaining = FormatSubscriptionRemaining(uiSettings.subscriptionExpireUnix);

	for (const std::string& groupName : groupOrder)
	{
		std::vector<int> groupIndices;
		groupIndices.reserve(m_nodes.size());
		std::string groupSourceUrl;
		const bool groupNameHit = GroupMatchesSearch(groupName, m_search);
		for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
		{
			const VpnNode& node = m_nodes[static_cast<size_t>(i)];
			const std::string& group = node.group.empty() ? "Imported" : node.group;
			if (group != groupName)
				continue;
			if (!groupNameHit && !MatchesSearch(node, m_search))
				continue;
			groupIndices.push_back(i);
			if (groupSourceUrl.empty() && !node.sourceUrl.empty())
				groupSourceUrl = node.sourceUrl;
		}
		if (groupIndices.empty())
			continue;

		// source_url may be empty on older caches — still treat Capybara as subscription group.
		bool showSubscriptionActions = !groupSourceUrl.empty();
		if (!showSubscriptionActions)
		{
			if (groupName.find("Capybara") != std::string::npos
				|| groupName.find("Copybara") != std::string::npos)
			{
				showSubscriptionActions = true;
			}
			else
			{
				for (int gi : groupIndices)
				{
					if (NodeLooksLikeCapybaraGroup(m_nodes[static_cast<size_t>(gi)]))
					{
						showSubscriptionActions = true;
						break;
					}
				}
			}
		}

		std::string refreshUrl = groupSourceUrl;
		if (refreshUrl.empty())
			refreshUrl = uiSettings.lastSubscriptionUrl;

		// Remaining time belongs only to the subscription that owns card meta —
		// never bleed onto plain URI lists that happen to have a source_url.
		const bool groupMatchesSubscription =
			!groupSourceUrl.empty()
			&& !uiSettings.lastSubscriptionUrl.empty()
			&& groupSourceUrl == uiSettings.lastSubscriptionUrl;
		const bool legacySubscriptionWithoutUrl =
			groupSourceUrl.empty()
			&& uiSettings.lastSubscriptionUrl.empty()
			&& showSubscriptionActions
			&& uiSettings.subscriptionExpireUnix > 0;
		const bool showRemainingInTitle =
			!subscriptionRemaining.empty()
			&& (groupMatchesSubscription || legacySubscriptionWithoutUrl);
		const bool canRenameGroup = !(groupMatchesSubscription || legacySubscriptionWithoutUrl);

		const char* groupLabel = DisplayGroupName(groupName);
		char groupTitle[256];
		if (showRemainingInTitle)
		{
			snprintf(
				groupTitle,
				sizeof groupTitle,
				"%s  (%zu)  ·  %s",
				groupLabel,
				groupIndices.size(),
				subscriptionRemaining.c_str());
		}
		else
		{
			snprintf(groupTitle, sizeof groupTitle, "%s  (%zu)", groupLabel, groupIndices.size());
		}

		ImGui::PushID(groupName.c_str());

		const float stripH = ImGui::GetFrameHeight();
		const ImVec2 headerBtnSize(stripH, stripH);
		constexpr float kGroupBtnGap = 4.f;
		// delete + ping + speed; subscription groups also get refresh.
		const int headerBtnCount = showSubscriptionActions ? 4 : 3;
		const float headerBtnsW =
			static_cast<float>(headerBtnCount) * stripH
			+ static_cast<float>(headerBtnCount - 1) * kGroupBtnGap;
		const ImVec2 headerRowPos = ImGui::GetCursorScreenPos();
		const float headerRowW = ImGui::GetContentRegionAvail().x;
		const float headerW = (std::max)(80.f, headerRowW - headerBtnsW - kGroupBtnGap);

		float btnX = headerRowPos.x + headerRowW - headerBtnsW;
		if (showSubscriptionActions)
		{
			ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
			const bool canRefresh = !m_importRunning.load() && !AnyProbeBusy();
			if (HeaderIconButton(fonts, 0xE72C, "refresh_sub", "Обновить подписку", colors, headerBtnSize, canRefresh))
				StartRefreshSubscriptions(refreshUrl);
			btnX += stripH + kGroupBtnGap;
		}

		ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
		const bool canDeleteGroup = !groupIndices.empty() && !AnyProbeBusy();
		const char* deleteTip = showSubscriptionActions
			? "Удалить группу подписки"
			: "Удалить группу";
		if (HeaderIconButton(fonts, 0xE74D, "delete_group", deleteTip, colors, headerBtnSize, canDeleteGroup))
			DeleteGroupServers(groupIndices);
		btnX += stripH + kGroupBtnGap;

		ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
		const bool pingHere = m_pingTestRunning && m_pingGroupKey == groupName;
		if (pingHere)
		{
			if (HeaderIconButton(fonts, 0xE711, "stop_ping_group", "Остановить ping", colors, headerBtnSize, true))
				StopPing();
		}
		else
		{
			const bool canPingGroup = !groupIndices.empty() && !m_pingRunning.load();
			if (HeaderIconButton(fonts, 0xE895, "ping_group", "ping группы (ICMP→TCP)", colors, headerBtnSize, canPingGroup))
				StartPingIndices(groupIndices, groupName);
		}
		btnX += stripH + kGroupBtnGap;

		ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
		const bool speedHere = m_speedTestRunning && m_speedGroupKey == groupName;
		if (speedHere)
		{
			if (HeaderIconButton(fonts, 0xE711, "stop_speed_group", "Остановить тест скорости", colors, headerBtnSize, true))
				StopSpeed();
		}
		else
		{
			const bool canSpeedGroup = !groupIndices.empty() && !m_speedRunning.load();
			if (HeaderIconButton(fonts, 0xE9F5, "speed_group", "Тест скорости группы", colors, headerBtnSize, canSpeedGroup))
				StartSpeedTestIndices(groupIndices, groupName);
		}

		ImGui::SetCursorScreenPos(headerRowPos);
		ImGui::PushStyleColor(ImGuiCol_Header, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.navHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UiMetrics::kCardRadius);
		bool& open = m_groupOpen.try_emplace(groupName, false).first->second;
		if (ImGui::BeginChild(
			"##group_hdr",
			ImVec2(headerW, stripH),
			ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			// Own open state: CollapsingHeader returns false when the child is clipped
			// (SkipItems), which would otherwise drop the table and spring-scroll to top.
			ImGui::SetNextItemOpen(open, ImGuiCond_Always);
			open = ImGui::CollapsingHeader(groupTitle);
			if (canRenameGroup)
			{
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					UiCommon::SetItemTooltip("ПКМ — переименовать группу");
				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					m_renameGroupKey = groupName;
					strncpy_s(m_renameGroupBuf, sizeof m_renameGroupBuf, groupLabel, _TRUNCATE);
					m_renameGroupRequestOpen = true;
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);

		ImGui::SetCursorScreenPos({
			headerRowPos.x,
			headerRowPos.y + stripH + ImGui::GetStyle().ItemSpacing.y });
		ImGui::Dummy(ImVec2(headerRowW, 0.f));

		if (!open)
		{
			m_groupAppear[groupName] = 0.f;
			ImGui::PopID();
			ImGui::Dummy(ImVec2(0.f, 4.f));
			continue;
		}

		float& groupAppear = m_groupAppear.try_emplace(groupName, 0.f).first->second;
		{
			const float dt = ImGui::GetIO().DeltaTime;
			groupAppear += (1.f - groupAppear) * (1.f - expf(-dt * 7.f));
			if (groupAppear > 0.999f)
				groupAppear = 1.f;
		}

		// Card only for the subscription URL that produced card headers (3x-ui/Remnawave),
		// never for plain GitHub/file URI dumps that happen to share global settings leftovers.
		const bool hasCardMeta =
			uiSettings.subscriptionExpireUnix > 0
			|| !uiSettings.subscriptionAnnounce.empty()
			|| !uiSettings.subscriptionProfileTitle.empty();
		const bool showProviderCard = groupMatchesSubscription && hasCardMeta;

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, groupAppear);
		// Opaque monolith — translucent ChildBg caused darkening while scrolling.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, colors.tileBg);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
		ImGui::BeginChild(
			"##group_block",
			ImVec2(tableWidth, 0.f),
			ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

		if (showProviderCard)
		{
			DrawSubscriptionProviderCard(
				fonts,
				ImGui::GetContentRegionAvail().x,
				DisplayGroupName(groupName),
				uiSettings,
				colors,
				accents,
				1.f);
			ImGui::PushStyleColor(ImGuiCol_Separator, UiCommon::WithAlpha(colors.tileBorder, 0.45f));
			ImGui::Separator();
			ImGui::PopStyleColor();
			ImGui::Dummy(ImVec2(0.f, 6.f));
		}

		UiCommon::PushTableStyle(colors);
		if (ImGui::BeginTable(
			"##servers",
			9,
			UiCommon::StretchableTableFlags(false),
			ImVec2(-1.f, 0.f)))
		{
			ImGui::TableSetupColumn("№", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoSort, kColNum);
			ImGui::TableSetupColumn("Название", ImGuiTableColumnFlags_WidthStretch, 1.6f);
			ImGui::TableSetupColumn("IP Сервера", ImGuiTableColumnFlags_WidthStretch, 1.8f);
			ImGui::TableSetupColumn("Страна", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, 44.f);
			ImGui::TableSetupColumn("Порт", ImGuiTableColumnFlags_WidthStretch, 0.6f);
			ImGui::TableSetupColumn("тип протокола", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("TLS", ImGuiTableColumnFlags_WidthStretch, 0.5f);
			ImGui::TableSetupColumn("Пинг", ImGuiTableColumnFlags_WidthStretch, 0.7f);
			ImGui::TableSetupColumn("Скорость", ImGuiTableColumnFlags_WidthStretch, 0.8f);
			UiCommon::TableHeadersRowCentered(colors);

			const float rowContentH = ImGui::GetTextLineHeight();
			const float rowMinH = UiCommon::TableRowMinHeight(rowContentH);
			const int groupCount = static_cast<int>(groupIndices.size());
			const int displayBase = displayIndex;
			displayIndex += groupCount;

			ImGuiListClipper clipper;
			clipper.Begin(groupCount);
			while (clipper.Step())
			{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const int i = groupIndices[static_cast<size_t>(row)];
				const VpnNode& node = m_nodes[static_cast<size_t>(i)];
				const int rowNumber = displayBase + row + 1;
				ImGui::PushID(i);

				const float stepRowH = clipper.ItemsHeight > 0.f ? clipper.ItemsHeight : rowMinH;
				ImGui::TableNextRow(ImGuiTableRowFlags_None, stepRowH);

				float flash = 0.f;
				const auto flashIt = m_probeFlash.find(i);
				if (flashIt != m_probeFlash.end())
					flash = flashIt->second;
				// Ease-out for a softer fade.
				const float flashEase = flash * flash;
				if (flashEase > 0.01f)
				{
					const ImVec4 flashTint = (node.alive == 0) ? accents.fail : accents.ok;
					ImGui::TableSetBgColor(
						ImGuiTableBgTarget_RowBg0,
						ImGui::GetColorU32(UiCommon::WithAlpha(flashTint, flashEase * 0.32f)));
				}

				const ImVec4 rowColor = node.alive == 0 ? accents.fail : colors.textPrimary;
				const bool rowSelected = m_selectedSet.find(i) != m_selectedSet.end();

				char numBuf[12];
				snprintf(numBuf, sizeof numBuf, "%d", rowNumber);

				ImGui::TableSetColumnIndex(0);
				ImGui::PushStyleColor(ImGuiCol_Text, rowColor);
				if (UiCommon::TableRowSelectable(numBuf, rowSelected, rowContentH))
				{
					const ImGuiIO& rowIo = ImGui::GetIO();
					if (rowIo.KeyShift)
						SelectRangeInOrder(groupIndices, i);
					else if (rowIo.KeyCtrl)
						ToggleSelect(i);
					else
						SelectOnly(i);

					if (ImGui::IsMouseDoubleClicked(0) && !rowIo.KeyCtrl && !rowIo.KeyShift)
					{
						SelectOnly(i);
						m_detailIndex = i;
						m_view = View::Detail;
					}
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					const bool keepMulti =
						SelectionCount() > 1 && m_selectedSet.find(i) != m_selectedSet.end();
					if (!keepMulti)
						SelectOnly(i);
					ImGui::OpenPopup("##vpn_row_menu");
				}
				if (ImGui::BeginPopup("##vpn_row_menu"))
				{
					const bool probeBusy = AnyProbeBusy();
					const std::vector<int> menuSelection = SelectedIndicesSorted();
					const bool multiMenu = menuSelection.size() > 1;
					const int menuTarget = multiMenu
						? (m_selected >= 0 ? m_selected : i)
						: i;

					if (!multiMenu && ImGui::MenuItem("Сделать активным"))
						SetActiveServer(menuTarget);
					if (!multiMenu && ImGui::MenuItem("Редактировать", nullptr, false, !probeBusy))
					{
						SelectOnly(menuTarget);
						OpenSelectedDetails();
					}
					if (!multiMenu)
						ImGui::Separator();
					if (ImGui::MenuItem(
							multiMenu ? "ping (ICMP→TCP) выбранных" : "ping (ICMP→TCP)",
							nullptr,
							false,
							!probeBusy))
						StartPingIndices(menuSelection);
					if (ImGui::MenuItem(
							multiMenu ? "TCP ping выбранных" : "TCP ping",
							nullptr,
							false,
							!probeBusy))
						StartTcpPingIndices(menuSelection);
					if (ImGui::MenuItem(
							multiMenu ? "RealPing выбранных" : "RealPing",
							nullptr,
							false,
							!probeBusy))
						StartRealPingIndices(menuSelection);
					if (ImGui::MenuItem(
							multiMenu ? "Тест скорости выбранных" : "Тест скорости",
							nullptr,
							false,
							!probeBusy))
						StartSpeedTestIndices(menuSelection);
					ImGui::Separator();
					if (!multiMenu && ImGui::MenuItem("Экспорт outbound JSON"))
						ExportOutboundJson(menuTarget);
					if (ImGui::MenuItem(
							multiMenu ? "Удалить выбранные" : "Удалить",
							nullptr,
							false,
							!probeBusy))
					{
						DeleteGroupServers(menuSelection);
					}
					ImGui::EndPopup();
				}

				ImGui::TableSetColumnIndex(1);
				UiCommon::TableAlignTextY(rowContentH);
				if (i == m_activeIndex)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, accents.ok);
					char nameBuf[128];
					snprintf(nameBuf, sizeof nameBuf, "● %s", node.name.c_str());
					ImGui::TextUnformatted(nameBuf);
					ImGui::PopStyleColor();
				}
				else
				{
					ImGui::TextUnformatted(node.name.c_str());
				}

				ImGui::TableSetColumnIndex(2);
				UiCommon::TableAlignTextY(rowContentH);
				DrawServerHostCell(node.server);
				ImGui::TableSetColumnIndex(3);
				UiCommon::TableAlignTextY(rowContentH);
				DrawCountryFlagCell(node.country, rowContentH);
				ImGui::TableSetColumnIndex(4);
				UiCommon::TableAlignTextY(rowContentH);
				{
					char portBuf[16];
					snprintf(portBuf, sizeof portBuf, "%d", node.port);
					UiCommon::TableTextAligned(portBuf, UiCommon::UiTableAlign::Center);
				}
				ImGui::TableSetColumnIndex(5);
				UiCommon::TableAlignTextY(rowContentH);
				UiCommon::TableTextAligned(node.scheme.c_str(), UiCommon::UiTableAlign::Center);
				ImGui::TableSetColumnIndex(6);
				UiCommon::TableAlignTextY(rowContentH);
				UiCommon::TableTextAligned(node.tls ? "TLS" : "—", UiCommon::UiTableAlign::Center);

				ImGui::TableSetColumnIndex(7);
				UiCommon::TableAlignTextY(rowContentH);
				{
					const ImVec4 pingBright = (node.alive == 0) ? accents.fail : accents.ok;
					const ImVec4 pingColor = LerpVec4(rowColor, pingBright, flashEase);
					ImGui::PushStyleColor(ImGuiCol_Text, pingColor);
					UiCommon::TableTextAligned(FormatPing(node.pingMs, pingBuf, sizeof pingBuf), UiCommon::UiTableAlign::Center);
					ImGui::PopStyleColor();
				}

				ImGui::TableSetColumnIndex(8);
				UiCommon::TableAlignTextY(rowContentH);
				{
					const ImVec4 speedBright = accents.ok;
					const ImVec4 speedColor = LerpVec4(rowColor, speedBright, flashEase);
					ImGui::PushStyleColor(ImGuiCol_Text, speedColor);
					UiCommon::TableTextAligned(FormatSpeed(node.speedMbps, speedBuf, sizeof speedBuf), UiCommon::UiTableAlign::Center);
					ImGui::PopStyleColor();
				}

				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			}

			ImGui::EndTable();
		}
		UiCommon::PopTableStyle();

		ImGui::EndChild();
		ImGui::PopStyleVar(2); // rounding + padding
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(); // groupAppear alpha
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));
	}

	if (m_renameGroupRequestOpen)
	{
		ImGui::OpenPopup("##vpn_rename_group");
		m_renameGroupRequestOpen = false;
	}

	{
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
	}
	{
		const ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	}
	if (ImGui::BeginPopupModal(
			"##vpn_rename_group",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
	{
		constexpr float kDialogW = 340.f;
		constexpr float kBtnW = 112.f;
		const float btnH = UiMetrics::kSmallBtnHeight;

		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::TextUnformatted("Переименовать группу");
		ImGui::PopStyleColor();
		ImGui::Dummy({ 0.f, 2.f });
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
		ImGui::TextUnformatted("Новое имя для выбранной группы серверов.");
		ImGui::PopStyleColor();

		ImGui::Dummy({ kDialogW, 0.f });
		ImGui::Dummy({ 0.f, 6.f });

		UiCommon::PushInputStyle(colors);
		ImGui::SetNextItemWidth(kDialogW);
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		const bool enter = ImGui::InputText(
			"##rename_group_input",
			m_renameGroupBuf,
			sizeof m_renameGroupBuf,
			ImGuiInputTextFlags_EnterReturnsTrue);
		UiCommon::PopInputStyle();

		ImGui::Dummy({ 0.f, 14.f });
		const float rowX = ImGui::GetCursorPosX();
		const bool cancel = UiCommon::SecondaryButton("Отмена", { kBtnW, btnH }, colors);
		ImGui::SameLine(0.f, 0.f);
		ImGui::SetCursorPosX(rowX + kDialogW - kBtnW);
		const bool ok =
			UiCommon::AccentButton("Сохранить", { kBtnW, btnH }, colors.navActive, colors)
			|| enter;

		if (ok)
		{
			RenameGroup(m_renameGroupKey, m_renameGroupBuf);
			ImGui::CloseCurrentPopup();
		}
		else if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(5);
	ImGui::PopStyleColor(4);

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kCardGap));
	ImGui::PopStyleVar();
}

void UiVpnPage::DrawDetailView(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	VpnNode& node = m_nodes[static_cast<size_t>(m_detailIndex)];

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, UiMetrics::kRowGap));

	if (UiCommon::SecondaryButton("<- Назад", ImVec2(100.f, UiMetrics::kSmallBtnHeight), colors)
		|| UiCommon::IsMouseNavBackClicked())
		m_view = View::List;
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Серверы  >  Детали сервера");
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0.f, 4.f));
	UiCommon::PageTitle(
		fonts,
		0xf0ac,
		node.name.c_str(),
		nullptr,
		colors,
		UiCommon::TitleIconFont::Solid);

	const float actionBtnW = 110.f;
	if (m_pingTestRunning)
	{
		if (UiCommon::SecondaryButton("Стоп ping", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors))
			StopPing();
	}
	else if (UiCommon::SecondaryButton("Пинг", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors, !m_pingRunning.load()))
	{
		SelectOnly(m_detailIndex);
		StartPing(true);
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (m_speedTestRunning)
	{
		if (UiCommon::SecondaryButton("Стоп", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors))
			StopSpeed();
	}
	else if (UiCommon::SecondaryButton("Тест скорости", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors, !m_speedRunning.load()))
	{
		SelectOnly(m_detailIndex);
		StartSpeedTest(true);
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("JSON", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors))
		ExportOutboundJson(m_detailIndex);
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("Конфиг", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors))
		ExportRuntimeConfig(m_detailIndex);

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));

	if (UiCommon::BeginCard("##node_info", width, colors))
	{
		char endpointBuf[160];
		snprintf(endpointBuf, sizeof endpointBuf, "%s:%d  (%s)", node.server.c_str(), node.port, node.scheme.c_str());
		UiCommon::SectionHeader(node.name.c_str(), colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		UiCommon::InfoLine("", endpointBuf, colors);

		char detailsBuf[256];
		if (!node.country.empty())
		{
			snprintf(
				detailsBuf,
				sizeof detailsBuf,
				"Группа: %s  |  Страна: %s  |  Теги: %s",
				node.group.c_str(),
				node.country.c_str(),
				!node.tags.empty() ? node.tags.c_str() : "—");
		}
		else
		{
			snprintf(
				detailsBuf,
				sizeof detailsBuf,
				"Группа: %s  |  Страна: —  |  Теги: %s",
				node.group.c_str(),
				!node.tags.empty() ? node.tags.c_str() : "—");
		}
		UiCommon::CaptionText(detailsBuf, colors, width);

		char statusBuf[160];
		if (node.alive < 0 && node.pingMs < 0)
			snprintf(statusBuf, sizeof statusBuf, "Не тестировался");
		else
		{
			char pingBuf[24];
			char speedBuf[24];
			snprintf(
				statusBuf,
				sizeof statusBuf,
				"Пинг: %s  |  Скорость: %s  |  Статус: %s",
				FormatPing(node.pingMs, pingBuf, sizeof pingBuf),
				FormatSpeed(node.speedMbps, speedBuf, sizeof speedBuf),
				node.alive > 0 ? "OK" : (node.alive == 0 ? "Недоступен" : "—"));
		}
		ImGui::Dummy(ImVec2(0.f, 4.f));
		UiCommon::CaptionText(statusBuf, colors, width);
	}
	UiCommon::EndCard();
	UiCommon::CardGap();

	const float halfW = (width - UiMetrics::kGridGap) * 0.5f;

	if (UiCommon::BeginCard("##ping_history", halfW, colors))
	{
		UiCommon::SectionHeader("История пинга", colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		if (ImGui::BeginTable("##ping_hist", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Время", ImGuiTableColumnFlags_WidthFixed, 72.f);
			ImGui::TableSetupColumn("Пинг", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			for (const VpnNodeHistoryEntry& entry : node.pingHistory)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(entry.time.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.value.c_str());
			}
			ImGui::EndTable();
		}
	}
	UiCommon::EndCard();

	ImGui::SameLine(0.f, UiMetrics::kGridGap);

	if (UiCommon::BeginCard("##speed_history", halfW, colors))
	{
		UiCommon::SectionHeader("История скорости", colors);
		ImGui::Dummy(ImVec2(0.f, 4.f));
		if (ImGui::BeginTable("##speed_hist", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Время", ImGuiTableColumnFlags_WidthFixed, 72.f);
			ImGui::TableSetupColumn("Скорость", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();
			for (const VpnNodeHistoryEntry& entry : node.speedHistory)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(entry.time.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(entry.value.c_str());
			}
			ImGui::EndTable();
		}
	}
	UiCommon::EndCard();

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kCardGap));
	ImGui::PopStyleVar();
}
