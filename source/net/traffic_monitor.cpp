#include "net/traffic_monitor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace
{
	bool IsValidLinkSpeedBits(DWORD bitsPerSec)
	{
		if (bitsPerSec == 0 || bitsPerSec == 0xFFFFFFFFu)
			return false;
		constexpr DWORD kMaxPlausible = 100u * 1000u * 1000u * 1000u;
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

	// Measure a single interface — summing all adapters double-counts VPN/TUN + NIC.
	bool QueryPrimaryInterfaceCounters(
		DWORD& ifIndexOut,
		std::uint64_t& inOut,
		std::uint64_t& outOut,
		float& linkSpeedBitsOut)
	{
		const DWORD ifIndex = ResolveDefaultInterfaceIndex();
		if (ifIndex == 0)
			return false;

		MIB_IFROW row {};
		row.dwIndex = ifIndex;
		if (GetIfEntry(&row) != NO_ERROR)
			return false;

		if (row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL)
			return false;

		ifIndexOut = ifIndex;
		inOut = row.dwInOctets;
		outOut = row.dwOutOctets;
		linkSpeedBitsOut = IsValidLinkSpeedBits(row.dwSpeed) ? static_cast<float>(row.dwSpeed) : 0.f;
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
	while (m_sampleTimer >= kSampleIntervalSec)
	{
		m_sampleTimer -= kSampleIntervalSec;
		Sample();

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

void TrafficMonitor::Sample()
{
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

	const float interval = kSampleIntervalSec;
	m_downloadBps = static_cast<float>(deltaIn) / interval;
	m_uploadBps = static_cast<float>(deltaOut) / interval;
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
