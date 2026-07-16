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
#include "vpn/vpn_node_probe.h"
#include "vpn/vpn_routing.h"
#include "zapret/zapret_paths.h"
#include "imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace
{
	const char* kWorkModes[] = {
		"региональные прессеты",
		"RUv1- Заблокированное",
		"RUv1- Все, кроме рф",
		"RUv1- Все",
		"Своя Маршрутизация",
	};

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

	bool MatchesSearch(const VpnNode& node, const char* query)
	{
		if (!query || !query[0])
			return true;

		auto contains = [query](const char* hay) {
			return hay && strstr(hay, query) != nullptr;
		};

		char portBuf[16];
		snprintf(portBuf, sizeof portBuf, "%d", node.port);
		return contains(node.name.c_str()) || contains(node.scheme.c_str()) || contains(node.server.c_str()) ||
			contains(node.group.c_str()) || contains(node.tags.c_str()) || contains(portBuf);
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

		if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("%s", tooltip);

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		ImGui::PopID();

		if (!enabled)
			ImGui::EndDisabled();
		return pressed;
	}

	const char* DisplayGroupName(const std::string& groupName)
	{
		if (groupName.empty() || groupName == "Imported")
			return "Моё импортированное";
		return groupName.c_str();
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
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !countryName.empty())
				ImGui::SetTooltip("%s", countryName.c_str());
			return;
		}

		if (!normalized.empty())
			VpnFlagIcons::Instance().RequestFlag(countryCode);

		UiCommon::TableTextAligned(
			normalized.empty() ? "—" : normalized.c_str(),
			UiCommon::UiTableAlign::Center);
		if (!normalized.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !countryName.empty())
			ImGui::SetTooltip("%s", countryName.c_str());
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
		ImGui::SetTooltip("%s", tip.c_str());
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
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(
				"Прогоняет Discord (домены, голос UDP и Discord.exe)\n"
				"через VPN на любой стратегии RUv1.");
		}
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
		wchar_t iconWide[] = { static_cast<wchar_t>(0xE705), 0 };
		char iconUtf8[8] = {};
		WideCharToMultiByte(CP_UTF8, 0, iconWide, 1, iconUtf8, static_cast<int>(sizeof iconUtf8), nullptr, nullptr);
		ImFont* iconFont = fonts.GetIconFont();
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
}

void UiVpnPage::EnsureStoreLoaded()
{
	if (m_storeLoaded)
		return;

	VpnStoreSettings settings;
	m_store.Load(m_nodes, &settings);
	m_workMode = settings.workMode;
	m_fixDiscord = settings.fixDiscord;

	bool normalized = false;
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

	if (changed)
		SaveStore();
}

void UiVpnPage::QueueCountryLookups()
{
	bool cacheUpdated = false;
	for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
	{
		VpnNode& node = m_nodes[static_cast<size_t>(i)];
		if (!node.country.empty() || node.server.empty() || !VpnGeo::IsPublicIp(node.server))
			continue;

		if (!node.country.empty())
			VpnFlagIcons::Instance().RequestFlag(node.country);

		const std::string cached = VpnGeo::GetCachedCountryCode(node.server);
		if (!cached.empty())
		{
			node.country = cached;
			cacheUpdated = true;
			continue;
		}

		{
			std::lock_guard<std::mutex> lock(m_geoMutex);
			if (m_geoInFlight.count(node.server) > 0)
				continue;
			m_geoInFlight.insert(node.server);
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

	if (cacheUpdated)
		SaveStore();
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

	if (!pending.refreshSourceUrl.empty())
	{
		ApplyRefreshResult(
			std::move(pending.nodes),
			std::move(pending.errors),
			pending.refreshSourceUrl,
			pending.subscriptionExpireUnix);
	}
	else
	{
		ApplyImportResult(
			std::move(pending.nodes),
			pending.duplicatesSkipped,
			std::move(pending.errors),
			pending.subscriptionExpireUnix);
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
	long long subscriptionExpireUnix)
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

	for (const VpnNode& node : m_nodes)
	{
		if (NodeFromSubscriptionUrl(node, sourceUrl) || (refreshCapybara && NodeLooksLikeCapybaraGroup(node)))
			previousByUri[node.originalUri] = node;
	}

	m_nodes.erase(
		std::remove_if(
			m_nodes.begin(),
			m_nodes.end(),
			[&](const VpnNode& node)
			{
				return NodeFromSubscriptionUrl(node, sourceUrl)
					|| (refreshCapybara && NodeLooksLikeCapybaraGroup(node));
			}),
		m_nodes.end());

	size_t addedCount = 0;
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
		m_nodes.push_back(std::move(node));
		++addedCount;
	}

	m_activeIndex = FindNodeIndexByUri(activeUri);
	if (m_activeIndex < 0 && !m_nodes.empty())
		m_activeIndex = 0;
	m_selected = -1;
	{
		VpnStoreSettings settings = BuildStoreSettings();
		settings.lastSubscriptionUrl = sourceUrl;
		if (subscriptionExpireUnix > 0)
			settings.subscriptionExpireUnix = subscriptionExpireUnix;
		m_store.Save(m_nodes, &settings);
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
	long long subscriptionExpireUnix)
{
	size_t addedCount = 0;
	std::string importedSourceUrl;
	for (VpnNode& node : importedNodes)
	{
		VpnImport::NormalizeNodeDisplay(node);
		if (importedSourceUrl.empty() && !node.sourceUrl.empty())
			importedSourceUrl = node.sourceUrl;
		const bool alreadyExists = std::any_of(
			m_nodes.begin(),
			m_nodes.end(),
			[&node](const VpnNode& existing) { return existing.originalUri == node.originalUri; });
		if (alreadyExists)
		{
			++duplicatesSkipped;
			continue;
		}
		m_nodes.push_back(std::move(node));
		++addedCount;
	}

	if (m_activeIndex < 0 && !m_nodes.empty())
		m_activeIndex = 0;

	if (!importedSourceUrl.empty() || subscriptionExpireUnix > 0)
	{
		VpnStoreSettings settings = BuildStoreSettings();
		if (!importedSourceUrl.empty())
			settings.lastSubscriptionUrl = importedSourceUrl;
		if (subscriptionExpireUnix > 0)
			settings.subscriptionExpireUnix = subscriptionExpireUnix;
		m_store.Save(m_nodes, &settings);
	}
	else
	{
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

	{
		std::lock_guard<std::mutex> lock(m_importMutex);
		m_importStatus = "Импорт из буфера обмена...";
	}
	m_importRunning.store(true);
	AppLog::Instance().Append(
		LogSource::VpnRouting,
		"Импорт: запуск из буфера обмена (" + std::to_string(clipboardText.size()) + " байт).");

	const int nextNodeIndex = static_cast<int>(m_nodes.size()) + 1;
	std::thread([this, clipboardText, nextNodeIndex]()
	{
		const VpnImportResult result = VpnImport::ImportFromText(clipboardText, nextNodeIndex);
		{
			std::lock_guard<std::mutex> lock(m_importMutex);
			m_pendingImport.nodes = std::move(result.nodes);
			m_pendingImport.duplicatesSkipped = result.duplicatesSkipped;
			m_pendingImport.errors = std::move(result.errors);
			m_pendingImport.subscriptionExpireUnix = result.subscriptionExpireUnix;
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

	if (pending.empty())
		return;

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
			PushSpeedHistory(node, item.speedMbps);
			m_probeFlash[item.nodeIndex] = 1.f;
			changed = true;
		}
	}

	if (changed)
		SaveStore();
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
	if (m_probeRunning.load() || m_nodes.empty())
		return;
	if (selectedOnly && (m_selected < 0 || m_selected >= static_cast<int>(m_nodes.size())))
		return;

	std::vector<int> indices;
	if (selectedOnly)
		indices.push_back(m_selected);
	else
	{
		for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
			indices.push_back(i);
	}
	StartPingIndices(std::move(indices));
}

void UiVpnPage::StartPingIndices(std::vector<int> indices)
{
	if (m_probeRunning.load() || indices.empty())
		return;

	const bool useRealPing =
		m_vpnEnabled
		&& m_manager
		&& m_manager->IsRunning();

	if (!useRealPing)
	{
		// VPN off: direct Tcping to real IP (DoH). With TUN on this path is unreliable.
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

		m_probeCancel.store(false);
		m_probeRunning.store(true);
		SetToolbarStatus(targets.size() == 1 ? "Tcping..." : "Tcping группы...");

		std::thread([this, targets]()
		{
			int ok = 0;
			for (const auto& target : targets)
			{
				if (m_probeCancel.load())
					break;
				const int pingMs = VpnNodeProbe::TcpPingMs(target.second.first, target.second.second, 5000);
				if (pingMs >= 0)
					++ok;

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

			char status[96];
			snprintf(status, sizeof status, "Tcping завершён: %d/%zu OK", ok, targets.size());
			SetToolbarStatus(status);
			m_probeRunning.store(false);
		}).detach();
		return;
	}

	// v2rayN Realping: switch node → HTTP GET via mixed-port → best of 2.
	const std::vector<VpnNode> nodesSnapshot = m_nodes;
	const VpnStoreSettings settings = BuildStoreSettings();
	const int originalActive = m_activeIndex;

	m_probeCancel.store(false);
	m_probeRunning.store(true);
	SetToolbarStatus(
		indices.size() == 1 ? "Реальная задержка..." : "Реальная задержка группы...");

	std::thread([this, indices, nodesSnapshot, settings, originalActive]()
	{
		constexpr const char* kPingUrl = "https://www.google.com/generate_204";
		int ok = 0;

		for (size_t n = 0; n < indices.size(); ++n)
		{
			if (m_probeCancel.load())
				break;

			const int index = indices[n];
			if (index < 0 || index >= static_cast<int>(nodesSnapshot.size()))
				continue;

			const VpnNode& node = nodesSnapshot[static_cast<size_t>(index)];
			char status[160];
			snprintf(
				status,
				sizeof status,
				"Задержка [%zu/%zu]: %s...",
				n + 1,
				indices.size(),
				node.name.c_str());
			SetToolbarStatus(status);

			int pingMs = -1;
			if (m_manager->Reload(nodesSnapshot, index, settings))
			{
				Sleep(1000);
				if (!m_probeCancel.load())
				{
					pingMs = VpnNodeProbe::HttpRealPingMs(
						"127.0.0.1",
						VpnManager::kMixedPort,
						kPingUrl,
						9000);
				}
			}

			PendingProbeResult result;
			result.nodeIndex = index;
			result.pingMs = pingMs;
			result.speedMbps = -2.f;
			result.ready = true;
			{
				std::lock_guard<std::mutex> lock(m_probeMutex);
				m_pendingProbe.push_back(result);
			}

			if (pingMs >= 0)
			{
				++ok;
				snprintf(status, sizeof status, "%s: %d ms", node.name.c_str(), pingMs);
				SetToolbarStatus(status);
			}
		}

		if (originalActive >= 0 && originalActive < static_cast<int>(nodesSnapshot.size()))
			m_manager->Reload(nodesSnapshot, originalActive, settings);

		char done[96];
		if (m_probeCancel.load())
			snprintf(done, sizeof done, "Задержка остановлена.");
		else
			snprintf(done, sizeof done, "Реальная задержка: %d/%zu OK", ok, indices.size());
		SetToolbarStatus(done);
		m_probeRunning.store(false);
	}).detach();
}

void UiVpnPage::StartSpeedTest(bool selectedOnly)
{
	if (m_probeRunning.load())
		return;
	if (!m_vpnEnabled || !m_manager || !m_manager->IsRunning())
	{
		SetToolbarStatus("Для теста скорости включите VPN.");
		return;
	}

	std::vector<int> indices;
	if (selectedOnly)
	{
		if (m_selected < 0 || m_selected >= static_cast<int>(m_nodes.size()))
			return;
		indices.push_back(m_selected);
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
	StartSpeedTestIndices(std::move(indices));
}

void UiVpnPage::StartSpeedTestIndices(std::vector<int> indices)
{
	if (m_probeRunning.load() || indices.empty())
		return;
	if (!m_vpnEnabled || !m_manager || !m_manager->IsRunning())
	{
		SetToolbarStatus("Для теста скорости включите VPN.");
		return;
	}

	const std::vector<VpnNode> nodesSnapshot = m_nodes;
	const VpnStoreSettings settings = BuildStoreSettings();
	const int originalActive = m_activeIndex;

	m_probeCancel.store(false);
	m_probeRunning.store(true);
	m_speedTestRunning = true;
	SetToolbarStatus(
		indices.size() == 1 ? "Тест скорости..." : "Тест скорости группы...");

	std::thread([this, indices, nodesSnapshot, settings, originalActive]()
	{
		// v2rayN: per-node proxy switch → settle → Cachefly peak MB/s.
		constexpr const char* kUrl = "https://cachefly.cachefly.net/50mb.test";
		constexpr int kTimeoutMs = 10000;
		int ok = 0;

		for (size_t n = 0; n < indices.size(); ++n)
		{
			if (m_probeCancel.load())
				break;

			const int index = indices[n];
			if (index < 0 || index >= static_cast<int>(nodesSnapshot.size()))
				continue;

			const VpnNode& node = nodesSnapshot[static_cast<size_t>(index)];
			char status[160];
			snprintf(
				status,
				sizeof status,
				"Скорость [%zu/%zu]: %s...",
				n + 1,
				indices.size(),
				node.name.c_str());
			SetToolbarStatus(status);

			float peakMBps = -1.f;
			if (m_manager->Reload(nodesSnapshot, index, settings))
			{
				Sleep(1000);
				if (!m_probeCancel.load())
				{
					peakMBps = VpnNodeProbe::MeasureDownloadPeakMBps(
						"127.0.0.1",
						VpnManager::kMixedPort,
						kUrl,
						kTimeoutMs,
						&m_probeCancel);
				}
			}

			PendingProbeResult result;
			result.nodeIndex = index;
			result.pingMs = -2;
			result.speedMbps = peakMBps;
			result.ready = true;
			{
				std::lock_guard<std::mutex> lock(m_probeMutex);
				m_pendingProbe.push_back(result);
			}

			if (peakMBps >= 0.f)
			{
				++ok;
				snprintf(status, sizeof status, "%s: %.1f MB/s", node.name.c_str(), peakMBps);
				SetToolbarStatus(status);
			}
		}

		if (originalActive >= 0 && originalActive < static_cast<int>(nodesSnapshot.size()))
			m_manager->Reload(nodesSnapshot, originalActive, settings);

		char done[96];
		if (m_probeCancel.load())
			snprintf(done, sizeof done, "Тест скорости остановлен.");
		else
			snprintf(done, sizeof done, "Скорость группы: %d/%zu OK", ok, indices.size());
		SetToolbarStatus(done);

		m_speedTestRunning = false;
		m_probeRunning.store(false);
	}).detach();
}

void UiVpnPage::StopSpeedTest()
{
	m_probeCancel.store(true);
	m_speedTestRunning = false;
	SetToolbarStatus("Остановка теста...");
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
	const std::wstring vpnDir = ZapretPaths::GetVpnDirectory();
	std::string error;
	if (!VpnConfigBuilder::WriteRuntimeConfig(node, preset, settings, vpnDir, error, false))
	{
		SetToolbarStatus(error.empty() ? "Не удалось собрать runtime конфиг." : error);
		return;
	}

	const std::filesystem::path configPath = std::filesystem::path(vpnDir) / L"config.yaml";
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

void UiVpnPage::SyncVpnRuntime()
{
	if (!m_manager)
		return;

	// Don't fight per-node proxy switches during real-ping / speed probes.
	if (m_speedTestRunning || m_probeRunning.load())
		return;

	if (m_vpnEnabled
		&& m_lastAppliedVpnEnabled
		&& !m_manager->IsRunning()
		&& m_manager->GetRunStatus() == VpnRunStatus::Stopped
		&& !m_manager->GetErrorMessage().empty())
	{
		m_vpnEnabled = false;
		m_lastAppliedVpnEnabled = false;
		return;
	}

	if (!m_vpnEnabled)
	{
		if (m_lastAppliedVpnEnabled)
		{
			m_manager->RequestStop();
			m_lastAppliedVpnEnabled = false;
		}
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

	if (m_manager->IsOperationInFlight())
		return;

	m_manager->RequestStart(m_nodes, m_activeIndex, settings);
	m_lastAppliedVpnEnabled = true;
	m_lastAppliedWorkMode = m_workMode;
	m_lastAppliedActiveIndex = m_activeIndex;
	m_lastAppliedSettings = settings;
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

	if (m_view == View::List)
		DrawListView(theme, fonts, width);
	else
		DrawDetailView(theme, fonts, width);

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

	const float filterSearchW = width * 0.38f;
	const float filterGap = UiMetrics::kGridGap;
	const float comboW = (std::max)(120.f, width - filterSearchW - filterGap);

	UiCommon::PushInputStyle(colors);
	ImGui::SetNextItemWidth(filterSearchW);
	if (ImGui::InputTextWithHint("##search", "Поиск серверов", m_search, sizeof m_search))
		m_selected = -1;
	ImGui::SameLine(0.f, filterGap);
	ImGui::SetNextItemWidth(comboW);
	const int previousWorkMode = m_workMode;
	ImGui::Combo("##work_mode", &m_workMode, kWorkModes, 5);
	if (m_workMode != previousWorkMode)
		SaveStore();
	UiCommon::PopInputStyle();

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));

	const bool hasSelection = m_selected >= 0 && m_selected < static_cast<int>(m_nodes.size());
	const bool hasActive = m_activeIndex >= 0 && m_activeIndex < static_cast<int>(m_nodes.size());

	const ImGuiIO& io = ImGui::GetIO();
	const bool wantImportShortcut = io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !io.WantTextInput;

	if (ToolbarIconButton(fonts, 0xE710, "Импорт из буфера (Ctrl+V)", colors, !m_importRunning.load())
		|| wantImportShortcut)
	{
		StartImportFromClipboard();
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE735, "Сделать активным", colors, hasSelection)
		&& hasSelection)
	{
		SetActiveServer(m_selected);
	}
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE70F, "Редактировать", colors, hasSelection && !m_probeRunning.load()))
		OpenSelectedDetails();
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE724, "Реальная задержка выбранных", colors, hasSelection && !m_probeRunning.load()))
		StartPing(true);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE9F5, "Тест скорости выбранных", colors, hasSelection && !m_probeRunning.load()))
		StartSpeedTest(true);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE769, "Тест скорости активного", colors, hasActive && !m_probeRunning.load()))
		StartSpeedTest(false);
	ImGui::SameLine(0.f, 2.f);
	if (m_speedTestRunning && ToolbarIconButton(fonts, 0xE711, "Остановить тест скорости", colors))
		StopSpeedTest();
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE74E, "Экспорт outbound JSON", colors, hasSelection))
		ExportOutboundJson(m_selected);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE943, "Экспорт runtime конфига", colors, hasSelection))
		ExportRuntimeConfig(m_selected);
	ImGui::SameLine(0.f, 2.f);
	if (ToolbarIconButton(fonts, 0xE74D, "Удалить выбранные", colors, hasSelection && !m_probeRunning.load()))
	{
		m_nodes.erase(m_nodes.begin() + m_selected);
		if (m_activeIndex == m_selected)
			m_activeIndex = m_nodes.empty() ? -1 : 0;
		else if (m_activeIndex > m_selected)
			--m_activeIndex;
		m_selected = -1;
		SaveStore();
	}
	ImGui::SameLine(0.f, 2.f);
	const bool canMoveUp = hasSelection && m_selected > 0 && !m_probeRunning.load();
	const bool canMoveDown = hasSelection && m_selected + 1 < static_cast<int>(m_nodes.size()) && !m_probeRunning.load();
	if (ToolbarIconButton(fonts, 0xE70E, "Переместить вверх", colors, canMoveUp))
	{
		std::swap(m_nodes[static_cast<size_t>(m_selected)], m_nodes[static_cast<size_t>(m_selected - 1)]);
		if (m_activeIndex == m_selected)
			m_activeIndex = m_selected - 1;
		else if (m_activeIndex == m_selected - 1)
			m_activeIndex = m_selected;
		--m_selected;
		if (m_detailIndex == m_selected + 1)
			m_detailIndex = m_selected;
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
		++m_selected;
		if (m_detailIndex == m_selected - 1)
			m_detailIndex = m_selected;
		SaveStore();
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
		for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
		{
			const VpnNode& node = m_nodes[static_cast<size_t>(i)];
			const std::string& group = node.group.empty() ? "Imported" : node.group;
			if (group != groupName)
				continue;
			if (!MatchesSearch(node, m_search))
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

		const char* groupLabel = DisplayGroupName(groupName);
		char groupTitle[256];
		if (showSubscriptionActions && !subscriptionRemaining.empty())
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
		// ping + speed; subscription groups also get refresh.
		const int headerBtnCount = showSubscriptionActions ? 3 : 2;
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
			const bool canRefresh = !m_importRunning.load();
			if (HeaderIconButton(fonts, 0xE72C, "refresh_sub", "Обновить подписку", colors, headerBtnSize, canRefresh))
				StartRefreshSubscriptions(refreshUrl);
			btnX += stripH + kGroupBtnGap;
		}

		ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
		const bool canPingGroup =
			!groupIndices.empty()
			&& !m_probeRunning.load()
			&& m_vpnEnabled
			&& m_manager
			&& m_manager->IsRunning();
		if (HeaderIconButton(fonts, 0xE895, "ping_group", "Реальная задержка группы (как v2rayN)", colors, headerBtnSize, canPingGroup))
			StartPingIndices(groupIndices);
		btnX += stripH + kGroupBtnGap;

		ImGui::SetCursorScreenPos({ btnX, headerRowPos.y });
		const bool canSpeedGroup =
			!groupIndices.empty()
			&& !m_probeRunning.load()
			&& m_vpnEnabled
			&& m_manager
			&& m_manager->IsRunning();
		if (HeaderIconButton(fonts, 0xE9F5, "speed_group", "Тест скорости группы", colors, headerBtnSize, canSpeedGroup))
			StartSpeedTestIndices(groupIndices);

		ImGui::SetCursorScreenPos(headerRowPos);
		ImGui::PushStyleColor(ImGuiCol_Header, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, colors.navHover);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, colors.navActive);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.textPrimary);
		ImGui::BeginChild(
			"##group_hdr",
			ImVec2(headerW, stripH),
			ImGuiChildFlags_None,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const bool open = ImGui::CollapsingHeader(groupTitle, ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::EndChild();
		ImGui::PopStyleColor(4);

		ImGui::SetCursorScreenPos({
			headerRowPos.x,
			headerRowPos.y + stripH + ImGui::GetStyle().ItemSpacing.y });
		ImGui::Dummy(ImVec2(headerRowW, 0.f));

		if (!open)
		{
			ImGui::PopID();
			ImGui::Dummy(ImVec2(0.f, 4.f));
			continue;
		}

		ImGui::PushStyleColor(ImGuiCol_ChildBg, UiCommon::WithAlpha(colors.tileBg, 0.55f));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
		ImGui::BeginChild("##group_card", ImVec2(tableWidth, 0.f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

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

			for (int i : groupIndices)
			{
				const VpnNode& node = m_nodes[static_cast<size_t>(i)];
				++displayIndex;
				ImGui::PushID(i);

				const float rowContentH = ImGui::GetTextLineHeight();
				ImGui::TableNextRow(ImGuiTableRowFlags_None, UiCommon::TableRowMinHeight(rowContentH));

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
				const bool rowSelected = m_selected == i;

				char numBuf[8];
				snprintf(numBuf, sizeof numBuf, "%d", displayIndex);

				ImGui::TableSetColumnIndex(0);
				ImGui::PushStyleColor(ImGuiCol_Text, rowColor);
				if (UiCommon::TableRowSelectable(numBuf, rowSelected, rowContentH))
				{
					m_selected = i;
					if (ImGui::IsMouseDoubleClicked(0))
					{
						m_detailIndex = i;
						m_view = View::Detail;
					}
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

			ImGui::EndTable();
		}
		UiCommon::PopTableStyle();

		ImGui::EndChild();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		ImGui::PopID();
		ImGui::Dummy(ImVec2(0.f, UiMetrics::kSectionGap));
	}

	ImGui::Dummy(ImVec2(0.f, UiMetrics::kCardGap));
	ImGui::PopStyleVar();
}

void UiVpnPage::DrawDetailView(ThemeManager& theme, FontManager& fonts, float width)
{
	const UiThemeColors colors = theme.GetColors();
	VpnNode& node = m_nodes[static_cast<size_t>(m_detailIndex)];

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, UiMetrics::kRowGap));

	if (UiCommon::SecondaryButton("<- Назад", ImVec2(100.f, UiMetrics::kSmallBtnHeight), colors))
		m_view = View::List;
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	ImGui::PushStyleColor(ImGuiCol_Text, colors.textMuted);
	ImGui::TextUnformatted("Серверы  >  Детали сервера");
	ImGui::PopStyleColor();

	ImGui::Dummy(ImVec2(0.f, 4.f));
	UiCommon::PageTitle(fonts, 0xE705, node.name.c_str(), nullptr, colors);

	const float actionBtnW = 110.f;
	if (UiCommon::SecondaryButton("Пинг", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors, !m_probeRunning.load()))
	{
		m_selected = m_detailIndex;
		StartPing(true);
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (UiCommon::SecondaryButton("Тест скорости", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors, !m_probeRunning.load()))
	{
		m_selected = m_detailIndex;
		StartSpeedTest(true);
	}
	ImGui::SameLine(0.f, UiMetrics::kGridGap);
	if (m_speedTestRunning && UiCommon::SecondaryButton("Остановить", ImVec2(actionBtnW, UiMetrics::kSmallBtnHeight), colors))
		StopSpeedTest();
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
