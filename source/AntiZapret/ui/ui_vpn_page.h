#pragma once

#include "vpn/vpn_import.h"
#include "vpn/vpn_node.h"
#include "vpn/vpn_store.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class FontManager;
class ThemeManager;
class VpnManager;

class UiVpnPage
{
public:
	enum class View
	{
		List,
		Detail
	};

	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

	void SetManager(VpnManager* manager) { m_manager = manager; }
	void UpdateRuntime();
	bool IsVpnEnabled() const { return m_vpnEnabled; }
	void SetVpnEnabled(bool enabled) { m_vpnEnabled = enabled; }
	bool HasActiveServer() const;
	std::string GetActiveServerLabel() const;
	// Discord Rich Presence details: "Country · Name" or empty if no active server.
	std::string GetActiveServerPresenceLabel() const;
	// Share-URI (vless/vmess/...) of the active/default server for Discord import.
	std::string GetActiveServerShareUri() const;
	// Deep-link / protocol import: subscription URL or share-URI text.
	void ImportSubscriptionUrl(const std::string& urlOrText);

	// Tray menu helpers.
	void EnsureStoreLoadedForTray() { EnsureStoreLoaded(); }
	int GetWorkMode() const { return m_workMode; }
	void SetWorkModeFromTray(int workMode);
	int GetTransportMode() const { return m_transportMode; }
	void SetTransportModeFromTray(int transportMode);
	int GetActiveServerIndex() const { return m_activeIndex; }
	int GetServerCount() const { return static_cast<int>(m_nodes.size()); }
	std::string GetServerTrayLabel(int index) const;
	void SelectServerFromTray(int nodeIndex);

private:
	void EnsureStoreLoaded();
	void SaveStore();
	void StartImportFromClipboard();
	void StartImportFromText(const std::string& text, const char* statusLabel);
	void StartRefreshSubscriptions(const std::string& sourceUrl = {});
	void ApplyPendingImportIfAny();
	void ApplyRefreshResult(
		std::vector<VpnNode> importedNodes,
		std::vector<std::string> errors,
		const std::string& sourceUrl,
		const VpnImportResult& meta);
	void ApplyPendingGeoLookups();
	void ApplyPendingProbeResults();
	void QueueCountryLookups();
	void ApplyImportResult(
		std::vector<VpnNode> importedNodes,
		int duplicatesSkipped,
		std::vector<std::string> errors,
		const VpnImportResult& meta);
	void ApplySubscriptionMetaToSettings(VpnStoreSettings& settings, const VpnImportResult& meta);
	void DrawListView(ThemeManager& theme, FontManager& fonts, float width);
	void DrawDetailView(ThemeManager& theme, FontManager& fonts, float width);
	void SyncVpnRuntime();
	void SyncDiscordRouteWithFixDiscord();
	void SetActiveServer(int nodeIndex);
	VpnStoreSettings BuildStoreSettings() const;
	int FindNodeIndexByUri(const std::string& uri) const;

	void StartPing(bool selectedOnly);
	void StartPingIndices(std::vector<int> indices, std::string groupKey = {});
	void StartTcpPingIndices(std::vector<int> indices);
	void StartRealPingIndices(std::vector<int> indices);
	void StartSpeedTest(bool selectedOnly);
	void StartSpeedTestIndices(std::vector<int> indices, std::string groupKey = {});
	void StopPing();
	void StopSpeed();
	void StopProbe();
	bool AnyProbeBusy() const;
	void DeleteSelectedServer();
	void DeleteGroupServers(const std::vector<int>& indices);
	void RenameGroup(const std::string& oldGroupKey, const std::string& newDisplayName);
	void ExportOutboundJson(int nodeIndex);
	void ExportRuntimeConfig(int nodeIndex);
	void OpenSelectedDetails();
	void PushPingHistory(VpnNode& node, int pingMs);
	void PushSpeedHistory(VpnNode& node, float speedMbps);
	void SetToolbarStatus(const std::string& text);

	bool HasSelection() const;
	int SelectionCount() const;
	std::vector<int> SelectedIndicesSorted() const;
	void ClearSelection();
	void SelectOnly(int index);
	void ToggleSelect(int index);
	void SelectRangeInOrder(const std::vector<int>& orderedIndices, int clickedIndex);

	View m_view = View::List;
	char m_search[128] = {};
	int m_workMode = 1;
	int m_transportMode = 1; // 0 = Proxy, 1 = Tunnel
	int m_selected = -1; // anchor for single-target actions / Shift range
	std::unordered_set<int> m_selectedSet;
	int m_activeIndex = -1;
	int m_detailIndex = -1;
	bool m_speedTestRunning = false;
	bool m_pingTestRunning = false;
	bool m_vpnEnabled = false;
	float m_vpnMix = 0.f;
	bool m_fixDiscord = false;
	bool m_storeLoaded = false;

	std::vector<VpnNode> m_nodes;
	VpnStore m_store;
	VpnManager* m_manager = nullptr;
	bool m_lastAppliedVpnEnabled = false;
	int m_lastAppliedWorkMode = 1;
	int m_lastAppliedActiveIndex = -1;
	VpnStoreSettings m_lastAppliedSettings {};
	uint64_t m_vpnRetryAfterTick = 0;
	bool m_waitingForRuntime = false;

	std::atomic<bool> m_importRunning { false };
	std::mutex m_importMutex;
	std::string m_importStatus;

	struct PendingImportResult
	{
		std::vector<VpnNode> nodes;
		int duplicatesSkipped = 0;
		std::vector<std::string> errors;
		std::string refreshSourceUrl; // non-empty => replace subscription nodes
		long long subscriptionExpireUnix = 0;
		long long subscriptionUploadBytes = 0;
		long long subscriptionDownloadBytes = 0;
		long long subscriptionTotalBytes = 0;
		std::string subscriptionSupportUrl;
		std::string subscriptionProfileTitle;
		std::string subscriptionAnnounce;
		std::string subscriptionProviderId;
		std::string subscriptionUserId;
		std::string subscriptionIconUrl;
		bool hasSubscriptionCard = false;
		bool ready = false;
	};
	PendingImportResult m_pendingImport;
	// After protocol share-URI import, select this server as active.
	std::string m_pendingActivateUri;

	struct PendingGeoResult
	{
		int nodeIndex = -1;
		std::string countryCode;
	};
	std::mutex m_geoMutex;
	std::vector<PendingGeoResult> m_pendingGeo;
	std::unordered_set<std::string> m_geoInFlight;

	std::atomic<bool> m_pingRunning { false };
	std::atomic<bool> m_pingCancel { false };
	std::atomic<bool> m_speedRunning { false };
	std::atomic<bool> m_speedCancel { false };
	std::string m_pingGroupKey;
	std::string m_speedGroupKey;
	std::mutex m_probeMutex;
	std::string m_toolbarStatus;

	struct PendingProbeResult
	{
		int nodeIndex = -1;
		int pingMs = -2; // -2 = unchanged
		float speedMbps = -2.f;
		bool ready = false;
		bool live = false; // progress sample — update UI only, skip history/disk spam
	};
	std::vector<PendingProbeResult> m_pendingProbe;
	// 1 → 0 fade after a probe result lands (row highlight).
	std::unordered_map<int, float> m_probeFlash;
	bool m_probeDirty = false;
	// Persistent group open state (avoid CollapsingHeader false when clipped).
	std::unordered_map<std::string, bool> m_groupOpen;
	// 0..1 appear animation per group (list fade-in).
	std::unordered_map<std::string, float> m_groupAppear;
	// RMB rename for non-subscription groups.
	std::string m_renameGroupKey;
	char m_renameGroupBuf[128] = {};
	bool m_renameGroupRequestOpen = false;
};
