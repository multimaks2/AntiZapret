#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_store.h"

#include <atomic>
#include <mutex>
#include <string>
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

private:
	void EnsureStoreLoaded();
	void SaveStore();
	void StartImportFromClipboard();
	void ApplyPendingImportIfAny();
	void ApplyPendingGeoLookups();
	void ApplyPendingProbeResults();
	void QueueCountryLookups();
	void ApplyImportResult(std::vector<VpnNode> importedNodes, int duplicatesSkipped, std::vector<std::string> errors);
	void DrawListView(ThemeManager& theme, FontManager& fonts, float width);
	void DrawDetailView(ThemeManager& theme, FontManager& fonts, float width);
	void SyncVpnRuntime();
	void SetActiveServer(int nodeIndex);
	VpnStoreSettings BuildStoreSettings() const;
	int FindNodeIndexByUri(const std::string& uri) const;

	void StartPing(bool selectedOnly);
	void StartSpeedTest(bool selectedOnly);
	void StopSpeedTest();
	void ExportOutboundJson(int nodeIndex);
	void ExportRuntimeConfig(int nodeIndex);
	void OpenSelectedDetails();
	void PushPingHistory(VpnNode& node, int pingMs);
	void PushSpeedHistory(VpnNode& node, float speedMbps);
	void SetToolbarStatus(const std::string& text);

	View m_view = View::List;
	char m_search[128] = {};
	int m_workMode = 1;
	int m_selected = -1;
	int m_activeIndex = -1;
	int m_detailIndex = -1;
	bool m_speedTestRunning = false;
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

	std::atomic<bool> m_importRunning { false };
	std::mutex m_importMutex;
	std::string m_importStatus;

	struct PendingImportResult
	{
		std::vector<VpnNode> nodes;
		int duplicatesSkipped = 0;
		std::vector<std::string> errors;
		bool ready = false;
	};
	PendingImportResult m_pendingImport;

	struct PendingGeoResult
	{
		int nodeIndex = -1;
		std::string countryCode;
	};
	std::mutex m_geoMutex;
	std::vector<PendingGeoResult> m_pendingGeo;
	std::unordered_set<std::string> m_geoInFlight;

	std::atomic<bool> m_probeRunning { false };
	std::atomic<bool> m_probeCancel { false };
	std::mutex m_probeMutex;
	std::string m_toolbarStatus;

	struct PendingProbeResult
	{
		int nodeIndex = -1;
		int pingMs = -2; // -2 = unchanged
		float speedMbps = -2.f;
		bool ready = false;
	};
	std::vector<PendingProbeResult> m_pendingProbe;
};
