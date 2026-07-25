#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ProcessNetEntry
{
	std::uint32_t pid = 0;
	std::string name;
	float downloadBps = 0.f;
	float uploadBps = 0.f;
	int connectionCount = 0;
};

// Samples TCP (and UDP presence) per process for Debug home monitor.
// Release callers may own an instance; Update still runs cheaply (~1 Hz).
class ProcessNetMonitor
{
public:
	static constexpr size_t kTopCount = 8;

	void Update(float deltaTime);

	const std::vector<ProcessNetEntry>& GetTopProcesses() const { return m_top; }

private:
	void Sample();
	static std::string ResolveProcessName(std::uint32_t pid);

	float m_sampleTimer = 0.f;
	bool m_hasBaseline = false;
	std::vector<ProcessNetEntry> m_top;

	struct ConnKey
	{
		std::uint32_t localAddr = 0;
		std::uint32_t localPort = 0;
		std::uint32_t remoteAddr = 0;
		std::uint32_t remotePort = 0;
		std::uint32_t pid = 0;
		bool operator==(const ConnKey& o) const
		{
			return localAddr == o.localAddr && localPort == o.localPort
				&& remoteAddr == o.remoteAddr && remotePort == o.remotePort
				&& pid == o.pid;
		}
	};

	struct ConnKeyHash
	{
		size_t operator()(const ConnKey& k) const
		{
			size_t h = k.localAddr;
			h = h * 131u + k.localPort;
			h = h * 131u + k.remoteAddr;
			h = h * 131u + k.remotePort;
			h = h * 131u + k.pid;
			return h;
		}
	};

	struct ConnStats
	{
		std::uint64_t bytesIn = 0;
		std::uint64_t bytesOut = 0;
	};

	std::unordered_map<ConnKey, ConnStats, ConnKeyHash> m_prevConn;
	std::unordered_set<ConnKey, ConnKeyHash> m_enabledConn;
	std::unordered_map<std::uint32_t, std::string> m_nameCache;
};
