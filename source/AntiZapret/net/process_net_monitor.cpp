#include "net/process_net_monitor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <Tcpestats.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
	// Reject impossible per-connection deltas (EStats garbage / reconnects).
	constexpr double kMaxConnBytesPerSec = 512.0 * 1024.0 * 1024.0; // 512 MiB/s

	std::uint64_t CounterDelta(std::uint64_t current, std::uint64_t previous)
	{
		if (current < previous)
			return 0;
		return current - previous;
	}

	bool EnableTcpEstats(MIB_TCPROW& row)
	{
		TCP_ESTATS_DATA_RW_v0 rw = {};
		rw.EnableCollection = TRUE;
		return SetPerTcpConnectionEStats(
				   &row,
				   TcpConnectionEstatsData,
				   reinterpret_cast<PUCHAR>(&rw),
				   0,
				   sizeof(rw),
				   0)
			== NO_ERROR;
	}

	// Returns false when collection is off / unavailable — Rod must be ignored (MSDN).
	bool ReadTcpEstats(MIB_TCPROW& row, std::uint64_t& outBytesIn, std::uint64_t& outBytesOut)
	{
		outBytesIn = 0;
		outBytesOut = 0;

		TCP_ESTATS_DATA_RW_v0 rw = {};
		TCP_ESTATS_DATA_ROD_v0 rod = {};
		const ULONG status = GetPerTcpConnectionEStats(
			&row,
			TcpConnectionEstatsData,
			reinterpret_cast<PUCHAR>(&rw),
			0,
			sizeof(rw),
			nullptr,
			0,
			0,
			reinterpret_cast<PUCHAR>(&rod),
			0,
			sizeof(rod));
		if (status != NO_ERROR || !rw.EnableCollection)
			return false;

		outBytesIn = rod.DataBytesIn;
		outBytesOut = rod.DataBytesOut;
		return true;
	}
}

void ProcessNetMonitor::Update(float deltaTime)
{
	if (deltaTime < 0.f)
		deltaTime = 0.f;
	m_sampleTimer += deltaTime;
	constexpr float kInterval = 1.f;
	if (m_hasBaseline && m_sampleTimer < kInterval)
		return;
	m_sampleTimer = 0.f;
	Sample();
}

std::string ProcessNetMonitor::ResolveProcessName(std::uint32_t pid)
{
	if (pid == 0)
		return "System";

	HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (proc)
	{
		wchar_t path[MAX_PATH] = {};
		DWORD size = MAX_PATH;
		if (QueryFullProcessImageNameW(proc, 0, path, &size) && path[0])
		{
			CloseHandle(proc);
			const wchar_t* base = path;
			for (const wchar_t* p = path; *p; ++p)
			{
				if (*p == L'\\' || *p == L'/')
					base = p + 1;
			}
			char utf8[MAX_PATH] = {};
			WideCharToMultiByte(CP_UTF8, 0, base, -1, utf8, MAX_PATH, nullptr, nullptr);
			if (utf8[0])
				return utf8;
		}
		else
			CloseHandle(proc);
	}

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return "pid:" + std::to_string(pid);

	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof(pe);
	std::string name;
	if (Process32FirstW(snap, &pe))
	{
		do
		{
			if (pe.th32ProcessID == pid)
			{
				char utf8[MAX_PATH] = {};
				WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, utf8, MAX_PATH, nullptr, nullptr);
				name = utf8[0] ? utf8 : ("pid:" + std::to_string(pid));
				break;
			}
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return name.empty() ? ("pid:" + std::to_string(pid)) : name;
}

void ProcessNetMonitor::Sample()
{
	struct Acc
	{
		float down = 0.f;
		float up = 0.f;
		int connections = 0;
	};
	std::unordered_map<std::uint32_t, Acc> byPid;
	std::unordered_map<ConnKey, ConnStats, ConnKeyHash> nextConn;

	static LARGE_INTEGER s_freq = {};
	static LARGE_INTEGER s_lastTick = {};
	LARGE_INTEGER now = {};
	if (s_freq.QuadPart == 0)
		QueryPerformanceFrequency(&s_freq);
	QueryPerformanceCounter(&now);
	double dtSec = 1.0;
	if (m_hasBaseline && s_lastTick.QuadPart != 0 && s_freq.QuadPart != 0)
		dtSec = (std::max)(0.2, static_cast<double>(now.QuadPart - s_lastTick.QuadPart) / static_cast<double>(s_freq.QuadPart));
	s_lastTick = now;

	ULONG size = 0;
	DWORD err = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0);
	if (err == ERROR_INSUFFICIENT_BUFFER && size > 0)
	{
		std::vector<BYTE> buffer(size);
		auto* table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
		if (GetExtendedTcpTable(table, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_CONNECTIONS, 0) == NO_ERROR)
		{
			for (DWORD i = 0; i < table->dwNumEntries; ++i)
			{
				const auto& row = table->table[i];
				if (row.dwState != MIB_TCP_STATE_ESTAB)
					continue;

				MIB_TCPROW tcpRow = {};
				tcpRow.dwState = row.dwState;
				tcpRow.dwLocalAddr = row.dwLocalAddr;
				tcpRow.dwLocalPort = row.dwLocalPort;
				tcpRow.dwRemoteAddr = row.dwRemoteAddr;
				tcpRow.dwRemotePort = row.dwRemotePort;

				ConnKey key;
				key.localAddr = row.dwLocalAddr;
				key.localPort = row.dwLocalPort;
				key.remoteAddr = row.dwRemoteAddr;
				key.remotePort = row.dwRemotePort;
				key.pid = row.dwOwningPid;

				Acc& acc = byPid[row.dwOwningPid];
				++acc.connections;

				// Enable once per connection identity; ignore failures (often needs admin).
				if (m_enabledConn.find(key) == m_enabledConn.end())
				{
					if (EnableTcpEstats(tcpRow))
						m_enabledConn.insert(key);
					else
						continue;
				}

				std::uint64_t bytesIn = 0;
				std::uint64_t bytesOut = 0;
				if (!ReadTcpEstats(tcpRow, bytesIn, bytesOut))
					continue;

				ConnStats stats;
				stats.bytesIn = bytesIn;
				stats.bytesOut = bytesOut;
				nextConn[key] = stats;

				if (!m_hasBaseline)
					continue;

				const auto it = m_prevConn.find(key);
				if (it == m_prevConn.end())
					continue;

				const double din = static_cast<double>(CounterDelta(bytesIn, it->second.bytesIn));
				const double dout = static_cast<double>(CounterDelta(bytesOut, it->second.bytesOut));
				const double downBps = din / dtSec;
				const double upBps = dout / dtSec;
				if (downBps > kMaxConnBytesPerSec || upBps > kMaxConnBytesPerSec)
					continue;

				acc.down += static_cast<float>(downBps);
				acc.up += static_cast<float>(upBps);
			}
		}
	}

	size = 0;
	err = GetExtendedUdpTable(nullptr, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
	if (err == ERROR_INSUFFICIENT_BUFFER && size > 0)
	{
		std::vector<BYTE> buffer(size);
		auto* table = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());
		if (GetExtendedUdpTable(table, &size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
		{
			for (DWORD i = 0; i < table->dwNumEntries; ++i)
				++byPid[table->table[i].dwOwningPid].connections;
		}
	}

	// Drop enable tracking for closed sockets.
	for (auto it = m_enabledConn.begin(); it != m_enabledConn.end();)
	{
		if (nextConn.find(*it) == nextConn.end())
			it = m_enabledConn.erase(it);
		else
			++it;
	}

	m_prevConn.swap(nextConn);
	m_hasBaseline = true;

	std::vector<ProcessNetEntry> ranked;
	ranked.reserve(byPid.size());
	for (const auto& pair : byPid)
	{
		ProcessNetEntry entry;
		entry.pid = pair.first;
		entry.downloadBps = pair.second.down;
		entry.uploadBps = pair.second.up;
		entry.connectionCount = pair.second.connections;

		auto nameIt = m_nameCache.find(entry.pid);
		if (nameIt == m_nameCache.end())
		{
			const std::string name = ResolveProcessName(entry.pid);
			m_nameCache.emplace(entry.pid, name);
			entry.name = name;
		}
		else
			entry.name = nameIt->second;

		ranked.push_back(std::move(entry));
	}

	std::sort(ranked.begin(), ranked.end(), [](const ProcessNetEntry& a, const ProcessNetEntry& b) {
		const float sa = a.downloadBps + a.uploadBps;
		const float sb = b.downloadBps + b.uploadBps;
		if (sa != sb)
			return sa > sb;
		if (a.connectionCount != b.connectionCount)
			return a.connectionCount > b.connectionCount;
		return a.name < b.name;
	});

	if (ranked.size() > kTopCount)
		ranked.resize(kTopCount);
	m_top = std::move(ranked);

	if (m_nameCache.size() > 256)
		m_nameCache.clear();
}
