#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class TrafficMonitor
{
public:
	// (kHistorySize - 1) * kSampleIntervalSec == 60s — one-minute graph window.
	static constexpr size_t kHistorySize = 101;
	static constexpr float kSampleIntervalSec = 0.6f;

	void Update(float deltaTime);

	float GetDownloadBps() const { return m_downloadBps; }
	float GetUploadBps() const { return m_uploadBps; }
	float GetDisplayDownloadBps() const { return m_displayDownloadBps; }
	float GetDisplayUploadBps() const { return m_displayUploadBps; }
	float GetPeakDownloadBps() const { return m_peakDownloadBps; }
	float GetPeakUploadBps() const { return m_peakUploadBps; }
	float GetDisplayScaleMax() const { return m_displayScaleMax; }
	// Windows link speed is bits/sec (e.g. 1e9 for gigabit).
	float GetLinkSpeedBitsPerSec() const { return m_linkSpeedBitsPerSec; }
	float GetLinkCapacityBytesPerSec() const;
	float GetScrollPhase() const { return m_sampleTimer / kSampleIntervalSec; }

	std::uint64_t GetSessionBytesIn() const { return m_sessionBytesIn; }
	std::uint64_t GetSessionBytesOut() const { return m_sessionBytesOut; }

	size_t GetHistorySampleCount() const;
	float GetDownloadSampleAt(size_t chronologicalIndex) const;
	float GetUploadSampleAt(size_t chronologicalIndex) const;

private:
	void Sample();
	void UpdateDisplaySmoothing(float deltaTime);
	static float NiceScaleMax(float valueBps);

	std::array<float, kHistorySize> m_downloadHistory {};
	std::array<float, kHistorySize> m_uploadHistory {};
	size_t m_historyIndex = 0;
	bool m_historyFilled = false;

	std::uint64_t m_lastInOctets = 0;
	std::uint64_t m_lastOutOctets = 0;
	bool m_hasBaseline = false;
	std::uint32_t m_activeIfIndex = 0;

	float m_sampleTimer = 0.f;
	float m_downloadBps = 0.f;
	float m_uploadBps = 0.f;
	float m_displayDownloadBps = 0.f;
	float m_displayUploadBps = 0.f;
	float m_peakDownloadBps = 0.f;
	float m_peakUploadBps = 0.f;
	float m_displayScaleMax = 1024.f;
	float m_linkSpeedBitsPerSec = 0.f;

	std::uint64_t m_sessionBytesIn = 0;
	std::uint64_t m_sessionBytesOut = 0;
};
