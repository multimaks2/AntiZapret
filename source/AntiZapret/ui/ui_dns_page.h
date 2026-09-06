#pragma once

#include "net/dns_manager.h"
#include "ui/ui_smooth_scroll.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class FontManager;
class ThemeManager;

class UiDnsPage
{
public:
	UiDnsPage();
	~UiDnsPage();

	void DrawContent(ThemeManager& theme, FontManager& fonts, float width);

private:
	struct ServerRow
	{
		std::string name;
		std::string ipv4;
		std::string provider;
		std::string tags;
		int latencyMs = -1;
		bool probed = false;
	};

	void EnsureInitialized();
	void RefreshAdapters();
	void StartProbe();
	void ApplySelection();
	void ApplyFastest();
	void SetPrimary(int index, bool applyNow);
	void SetAlternate(int index, bool applyNow);
	void ToggleSelect(int index);
	void RestoreDhcp();
	void PumpProbeResults();
	void SortServersByLatency();
	bool MatchesSearch(const ServerRow& row) const;

	bool AdapterValid() const;
	const DnsManager::AdapterInfo* SelectedAdapter() const;

	std::vector<DnsManager::AdapterInfo> m_adapters;
	std::vector<ServerRow> m_servers;
	char m_search[128] = {};
	int m_adapterIndex = -1;
	int m_primaryIndex = -1;
	int m_alternateIndex = -1;
	bool m_initialized = false;
	bool m_wasProbing = false;

	UiSmoothScroll m_listScroll;

	std::atomic<bool> m_probing { false };
	std::atomic<bool> m_probeCancel { false };
	std::atomic<int> m_probeDone { 0 };
	std::atomic<int> m_probeTotal { 0 };

	std::mutex m_probeMutex;
	std::vector<int> m_probeLatencies;

	std::atomic<bool> m_applying { false };
	std::atomic<bool> m_needRefreshAdapters { false };
	std::string m_status;
	bool m_statusOk = true;

	static constexpr uint32_t kDnsIcon = 0xf233;
};
