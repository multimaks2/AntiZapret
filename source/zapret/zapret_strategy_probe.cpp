#include "zapret/zapret_strategy_probe.h"

#include "zapret/zapret_connectivity.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace ZapretStrategyProbe
{
namespace
{
	constexpr TargetDef kDefaultTargets[] = {
		{ "Discord Main", "https://discord.com", TargetKind::Https, TargetGroup::Discord },
		{ "Discord Gateway", "https://gateway.discord.gg", TargetKind::Https, TargetGroup::Discord },
		{ "Discord CDN", "https://cdn.discordapp.com", TargetKind::Https, TargetGroup::Discord },
		{ "Discord Updates", "https://updates.discord.com", TargetKind::Https, TargetGroup::Discord },
		{ "YouTube Web", "https://www.youtube.com", TargetKind::Https, TargetGroup::YouTube },
		{ "YouTube Short", "https://youtu.be", TargetKind::Https, TargetGroup::YouTube },
		{ "YouTube Image", "https://i.ytimg.com", TargetKind::Https, TargetGroup::YouTube },
		{ "YouTube Video Redirect", "https://redirector.googlevideo.com", TargetKind::Https, TargetGroup::YouTube },
		{ "Google Main", "https://www.google.com", TargetKind::Https, TargetGroup::Other },
		{ "Google Gstatic", "https://www.gstatic.com", TargetKind::Https, TargetGroup::Other },
		{ "Cloudflare Web", "https://www.cloudflare.com", TargetKind::Https, TargetGroup::Other },
		{ "Cloudflare CDN", "https://cdnjs.cloudflare.com", TargetKind::Https, TargetGroup::Other },
		{ "Cloudflare DNS 1.1.1.1", "1.1.1.1", TargetKind::Ping, TargetGroup::Dns },
		{ "Cloudflare DNS 1.0.0.1", "1.0.0.1", TargetKind::Ping, TargetGroup::Dns },
		{ "Google DNS 8.8.8.8", "8.8.8.8", TargetKind::Ping, TargetGroup::Dns },
		{ "Google DNS 8.8.4.4", "8.8.4.4", TargetKind::Ping, TargetGroup::Dns },
		{ "Quad9 DNS 9.9.9.9", "9.9.9.9", TargetKind::Ping, TargetGroup::Dns },
	};

	TargetResult ProbeOne(const TargetDef& def)
	{
		TargetResult result;
		result.name = def.name;
		result.kind = def.kind;
		result.group = def.group;

		if (def.kind == TargetKind::Ping)
		{
			const int pingMs = ZapretConnectivity::MeasureIcmpPingMs(def.value, 1500);
			result.pingMs = pingMs;
			result.ok = pingMs >= 0;
			if (result.ok)
			{
				char buf[32] = {};
				snprintf(buf, sizeof buf, "%d ms", pingMs);
				result.detail = buf;
			}
			else
			{
				result.detail = "Timeout";
			}
			return result;
		}

		const bool ok = ZapretConnectivity::ProbeHttpsUrl(def.value, 5000);
		result.ok = ok;
		result.detail = ok ? "OK" : "FAIL";
		return result;
	}
}

const TargetDef* GetDefaultTargets(size_t& outCount)
{
	outCount = sizeof(kDefaultTargets) / sizeof(kDefaultTargets[0]);
	return kDefaultTargets;
}

FullProbeResult RunFullStandardProbe(std::atomic<bool>* cancelFlag)
{
	FullProbeResult out;
	size_t count = 0;
	const TargetDef* defs = GetDefaultTargets(count);
	if (!defs || count == 0)
		return out;

	out.targets.resize(count);
	std::atomic<size_t> nextIndex { 0 };

	constexpr int kMaxParallel = 8;
	const int workerCount = static_cast<int>((std::min)(static_cast<size_t>(kMaxParallel), count));
	std::vector<std::thread> workers;
	workers.reserve(static_cast<size_t>(workerCount));

	for (int w = 0; w < workerCount; ++w)
	{
		workers.emplace_back([&]() {
			for (;;)
			{
				if (cancelFlag && cancelFlag->load())
					return;
				const size_t index = nextIndex.fetch_add(1);
				if (index >= count)
					return;
				out.targets[index] = ProbeOne(defs[index]);
			}
		});
	}

	for (std::thread& t : workers)
	{
		if (t.joinable())
			t.join();
	}

	int pingSum = 0;
	int pingSamples = 0;
	bool discordAny = false;
	bool youtubeAny = false;

	for (const TargetResult& t : out.targets)
	{
		if (t.kind == TargetKind::Ping)
		{
			if (t.ok)
			{
				++out.pingOk;
				if (t.pingMs >= 0)
				{
					pingSum += t.pingMs;
					++pingSamples;
				}
			}
			else
			{
				++out.pingFail;
			}
			continue;
		}

		if (t.ok)
			++out.httpOk;
		else
			++out.httpErr;

		if (t.group == TargetGroup::Discord && t.ok)
			discordAny = true;
		if (t.group == TargetGroup::YouTube && t.ok)
			youtubeAny = true;
	}

	out.discordOk = discordAny;
	out.youtubeOk = youtubeAny;
	if (!(cancelFlag && cancelFlag->load()))
		out.telegramOk = ZapretConnectivity::ProbeTelegram();
	out.pingMs = pingSamples > 0 ? (pingSum / pingSamples) : -1;
	return out;
}

StrategyTestEntry ToStoreEntry(const FullProbeResult& probe, const StrategyTestEntry& previous)
{
	StrategyTestEntry entry = previous;
	entry.discordOk = probe.discordOk;
	entry.youtubeOk = probe.youtubeOk;
	entry.telegramOk = probe.telegramOk;
	entry.pingMs = probe.pingMs;
	entry.httpOk = probe.httpOk;
	entry.httpErr = probe.httpErr;
	entry.pingOk = probe.pingOk;
	entry.pingFail = probe.pingFail;
	entry.fullTest = true;
	entry.targets.clear();
	entry.targets.reserve(probe.targets.size());
	for (const auto& t : probe.targets)
	{
		StrategyTargetResultView row;
		row.name = t.name;
		row.detail = t.detail;
		row.ok = t.ok;
		row.isPing = t.kind == TargetKind::Ping;
		row.pingMs = t.pingMs;
		entry.targets.push_back(std::move(row));
	}
	return entry;
}

}  // namespace ZapretStrategyProbe
