#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_store.h"
#include "vpn/vpn_transport_settings.h"

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

enum class VpnRunStatus
{
	Stopped,
	Starting,
	Running
};

class VpnManager
{
public:
	static constexpr int kDefaultMixedPort = 10800;
	static constexpr int kDefaultApiPort = 19090;

	VpnManager();
	~VpnManager();

	void Update(float deltaTime);

	bool Start(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings);
	void RequestStart(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings);
	bool Reload(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings);
	void RequestReload(const std::vector<VpnNode>& nodes, int activeIndex, const VpnStoreSettings& settings);
	void RequestReloadFromStore();
	void Stop();
	void RequestStop();

	// RealPing: launch mihomo against already-written config.yaml (no system proxy).
	bool StartFromExistingConfig(int mixedPort, int apiPort);
	// Prefer preferred when free; otherwise ephemeral (v2rayN GetFreePort).
	static int AllocateFreeTcpPort(int preferred);

	VpnRunStatus GetRunStatus() const { return m_runStatus; }
	bool IsOperationInFlight() const { return m_opInFlight.load(); }
	bool IsRunning() const { return m_runStatus == VpnRunStatus::Running; }
	int GetAppliedRoutingRevision() const { return m_appliedRoutingRevision; }
	int GetMixedPort() const { return m_mixedPort; }
	int GetApiPort() const { return m_apiPort; }
	const std::string& GetErrorMessage() const { return m_lastError; }
	const std::string& GetStatusMessage() const { return m_statusMessage; }
	bool IsRuntimeReady(std::string& outError) const;

private:
	bool LaunchProcess();
	void RefreshRunStatus();
	void ApplySystemProxy(bool enable);
	void RestoreSystemProxy();
	bool ResolveActiveNode(const std::vector<VpnNode>& nodes, int activeIndex, const VpnNode*& outNode, std::string& outError) const;
	bool WriteAndReloadConfig(
		const VpnNode& node,
		const VpnStoreSettings& settings,
		bool coldStart,
		std::string& outError);
	void MarkSettingsApplied(const VpnStoreSettings& settings);
	int ResolveActiveIndex(const std::vector<VpnNode>& nodes, const VpnStoreSettings& settings) const;
	void RunReloadWorker();
	void EnsurePortsAllocated();

	HANDLE m_process = nullptr;
	mutable std::mutex m_processMutex;

	VpnRunStatus m_runStatus = VpnRunStatus::Stopped;
	std::string m_lastError;
	std::string m_statusMessage;
	std::mutex m_opMutex;
	std::atomic<bool> m_opInFlight { false };
	std::mutex m_reloadQueueMutex;
	std::atomic<bool> m_reloadWorkerRunning { false };
	std::atomic<bool> m_reloadQueued { false };
	std::vector<VpnNode> m_reloadNodes;
	int m_reloadActiveIndex = -1;
	VpnStoreSettings m_reloadSettings {};
	int m_appliedRoutingRevision = -1;
	int m_mixedPort = kDefaultMixedPort;
	int m_apiPort = kDefaultApiPort;
	float m_statusPollTimer = 0.f;

	bool m_savedProxySettings = false;
	DWORD m_savedProxyEnable = 0;
	std::wstring m_savedProxyServer;
};
