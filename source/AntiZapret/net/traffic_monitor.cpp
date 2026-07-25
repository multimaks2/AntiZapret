#include "net/traffic_monitor.h"

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
#include <netioapi.h>

#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
	bool IsValidLinkSpeedBits(ULONG64 bitsPerSec)
	{
		if (bitsPerSec == 0 || bitsPerSec == 0xFFFFFFFFFFFFFFFFull)
			return false;
		constexpr ULONG64 kMaxPlausible = 100ull * 1000ull * 1000ull * 1000ull;
		return bitsPerSec <= kMaxPlausible;
	}

	std::uint64_t CounterDelta(std::uint64_t current, std::uint64_t previous)
	{
		return current >= previous ? current - previous : current;
	}

	DWORD ResolveDefaultInterfaceIndex()
	{
		DWORD ifIndex = 0;
		if (GetBestInterface(INADDR_ANY, &ifIndex) == NO_ERROR && ifIndex != 0)
			return ifIndex;

		ULONG size = 0;
		if (GetIpForwardTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0)
			return 0;

		std::vector<BYTE> buffer(size);
		auto* table = reinterpret_cast<PMIB_IPFORWARDTABLE>(buffer.data());
		if (GetIpForwardTable(table, &size, FALSE) != NO_ERROR)
			return 0;

		DWORD bestMetric = 0xFFFFFFFFu;
		DWORD bestIf = 0;
		for (DWORD i = 0; i < table->dwNumEntries; ++i)
		{
			const MIB_IPFORWARDROW& row = table->table[i];
			if (row.dwForwardDest != 0)
				continue;
			if (row.dwForwardMetric1 < bestMetric)
			{
				bestMetric = row.dwForwardMetric1;
				bestIf = row.dwForwardIfIndex;
			}
		}
		return bestIf;
	}

	bool IsLoopbackOrTunnel(const MIB_IF_ROW2& row)
	{
		if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK)
			return true;
		if (row.Type == IF_TYPE_TUNNEL)
			return true;
		if (row.InterfaceAndOperStatusFlags.EndPointInterface)
			return true;
		return false;
	}

	// Prefer the default route NIC; fall back to the busiest up non-tunnel adapter.
	bool QueryPrimaryInterfaceCounters(
		DWORD& ifIndexOut,
		std::uint64_t& inOut,
		std::uint64_t& outOut,
		float& linkSpeedBitsOut)
	{
		const DWORD preferred = ResolveDefaultInterfaceIndex();

		PMIB_IF_TABLE2 table = nullptr;
		if (GetIfTable2(&table) != NO_ERROR || !table)
			return false;

		DWORD bestIndex = 0;
		ULONG64 bestIn = 0;
		ULONG64 bestOut = 0;
		ULONG64 bestLink = 0;
		ULONG64 bestScore = 0;
		bool foundPreferred = false;

		for (ULONG i = 0; i < table->NumEntries; ++i)
		{
			const MIB_IF_ROW2& row = table->Table[i];
			if (row.OperStatus != IfOperStatusUp)
				continue;
			if (IsLoopbackOrTunnel(row))
				continue;

			const DWORD idx = row.InterfaceIndex;
			const ULONG64 score = row.InOctets + row.OutOctets;
			const bool isPreferred = (preferred != 0 && idx == preferred);

			if (isPreferred)
			{
				foundPreferred = true;
				bestIndex = idx;
				bestIn = row.InOctets;
				bestOut = row.OutOctets;
				bestLink = row.ReceiveLinkSpeed;
				break;
			}

			if (!foundPreferred && score >= bestScore)
			{
				bestScore = score;
				bestIndex = idx;
				bestIn = row.InOctets;
				bestOut = row.OutOctets;
				bestLink = row.ReceiveLinkSpeed;
			}
		}

		FreeMibTable(table);

		if (bestIndex == 0)
			return false;

		ifIndexOut = bestIndex;
		inOut = bestIn;
		outOut = bestOut;
		linkSpeedBitsOut = IsValidLinkSpeedBits(bestLink) ? static_cast<float>(bestLink) : 0.f;
		return true;
	}

	float ExpSmooth(float current, float target, float deltaTime, float speed)
	{
		const float alpha = 1.f - std::exp(-deltaTime * speed);
		return current + (target - current) * alpha;
	}

	float PeakHold(float peak, float current, float deltaTime, float decaySpeed)
	{
		if (current >= peak)
			return current;
		return ExpSmooth(peak, current, deltaTime, decaySpeed);
	}
}

float TrafficMonitor::NiceScaleMax(float valueBps)
{
	const float minScale = 1024.f;
	float value = (std::max)(valueBps, minScale);
	const float exponent = std::floor(std::log10(value));
	const float base = std::pow(10.f, exponent);
	const float normalized = value / base;

	static constexpr float kSteps[] = {
		1.f, 1.2f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 5.f, 6.f, 8.f, 10.f
	};
	float nice = 10.f;
	for (float step : kSteps)
	{
		if (normalized <= step + 0.001f)
		{
			nice = step;
			break;
		}
	}

	return nice * base;
}

void TrafficMonitor::Update(float deltaTime)
{
	if (deltaTime <= 0.f)
		return;

	m_sampleTimer += deltaTime;
	// One sample per due interval, using the real elapsed time — never burst-sample
	// with a fake 0.6s divisor (that creates fake spikes + under-reads).
	if (m_sampleTimer >= kSampleIntervalSec)
	{
		const float elapsed = m_sampleTimer;
		m_sampleTimer = 0.f;
		Sample(elapsed);

		if (m_hasBaseline)
		{
			m_downloadHistory[m_historyIndex] = m_downloadBps;
			m_uploadHistory[m_historyIndex] = m_uploadBps;
			m_historyIndex = (m_historyIndex + 1) % kHistorySize;
			if (m_historyIndex == 0)
				m_historyFilled = true;
		}
	}

	UpdateDisplaySmoothing(deltaTime);
}

void TrafficMonitor::UpdateDisplaySmoothing(float deltaTime)
{
	if (!m_hasBaseline)
		return;

	m_displayDownloadBps = ExpSmooth(m_displayDownloadBps, m_downloadBps, deltaTime, 1.8f);
	m_displayUploadBps = ExpSmooth(m_displayUploadBps, m_uploadBps, deltaTime, 1.8f);

	m_peakDownloadBps = PeakHold(m_peakDownloadBps, m_downloadBps, deltaTime, 0.28f);
	m_peakUploadBps = PeakHold(m_peakUploadBps, m_uploadBps, deltaTime, 0.28f);

	float windowPeak = (std::max)(m_displayDownloadBps, m_displayUploadBps);
	windowPeak = (std::max)(windowPeak, m_downloadBps);
	windowPeak = (std::max)(windowPeak, m_uploadBps);
	const size_t count = GetHistorySampleCount();
	for (size_t i = 0; i < count; ++i)
	{
		windowPeak = (std::max)(windowPeak, GetDownloadSampleAt(i));
		windowPeak = (std::max)(windowPeak, GetUploadSampleAt(i));
	}

	const float targetScale = NiceScaleMax(windowPeak * 1.18f);
	const float scaleSpeed = targetScale > m_displayScaleMax ? 5.f : 1.4f;
	m_displayScaleMax = ExpSmooth(m_displayScaleMax, targetScale, deltaTime, scaleSpeed);
	if (targetScale < m_displayScaleMax * 0.92f)
		m_displayScaleMax = ExpSmooth(m_displayScaleMax, targetScale, deltaTime, 2.2f);
}

void TrafficMonitor::Sample(float elapsedSec)
{
	if (elapsedSec < 0.05f)
		elapsedSec = 0.05f;

	DWORD ifIndex = 0;
	std::uint64_t totalIn = 0;
	std::uint64_t totalOut = 0;
	float linkSpeedBits = 0.f;
	if (!QueryPrimaryInterfaceCounters(ifIndex, totalIn, totalOut, linkSpeedBits))
		return;

	if (linkSpeedBits > 0.f)
		m_linkSpeedBitsPerSec = linkSpeedBits;

	// Default route can switch (VPN on/off) — restart baseline to avoid a fake spike.
	if (!m_hasBaseline || ifIndex != m_activeIfIndex)
	{
		m_activeIfIndex = static_cast<std::uint32_t>(ifIndex);
		m_lastInOctets = totalIn;
		m_lastOutOctets = totalOut;
		m_hasBaseline = true;
		m_downloadBps = 0.f;
		m_uploadBps = 0.f;
		return;
	}

	const std::uint64_t deltaIn = CounterDelta(totalIn, m_lastInOctets);
	const std::uint64_t deltaOut = CounterDelta(totalOut, m_lastOutOctets);
	m_lastInOctets = totalIn;
	m_lastOutOctets = totalOut;

	m_sessionBytesIn += deltaIn;
	m_sessionBytesOut += deltaOut;

	m_downloadBps = static_cast<float>(static_cast<double>(deltaIn) / static_cast<double>(elapsedSec));
	m_uploadBps = static_cast<float>(static_cast<double>(deltaOut) / static_cast<double>(elapsedSec));
}

float TrafficMonitor::GetLinkCapacityBytesPerSec() const
{
	if (m_linkSpeedBitsPerSec <= 0.f)
		return 0.f;
	return m_linkSpeedBitsPerSec / 8.f;
}

size_t TrafficMonitor::GetHistorySampleCount() const
{
	return m_historyFilled ? kHistorySize : m_historyIndex;
}

float TrafficMonitor::GetDownloadSampleAt(size_t chronologicalIndex) const
{
	const size_t count = GetHistorySampleCount();
	if (count == 0 || chronologicalIndex >= count)
		return 0.f;
	const size_t start = m_historyFilled ? m_historyIndex : 0;
	return m_downloadHistory[(start + chronologicalIndex) % kHistorySize];
}

float TrafficMonitor::GetUploadSampleAt(size_t chronologicalIndex) const
{
	const size_t count = GetHistorySampleCount();
	if (count == 0 || chronologicalIndex >= count)
		return 0.f;
	const size_t start = m_historyFilled ? m_historyIndex : 0;
	return m_uploadHistory[(start + chronologicalIndex) % kHistorySize];
}
