#pragma once

#include "zapret/zapret_store.h"

#include <atomic>
#include <string>
#include <vector>

namespace ZapretStrategyProbe
{

enum class TargetKind
{
	Https,
	Ping
};

enum class TargetGroup
{
	Discord,
	YouTube,
	Other,
	Dns
};

struct TargetDef
{
	const char* name;
	const char* value; // URL or host for PING
	TargetKind kind;
	TargetGroup group;
};

struct TargetResult
{
	std::string name;
	TargetKind kind = TargetKind::Https;
	TargetGroup group = TargetGroup::Other;
	bool ok = false;
	int pingMs = -1;
	std::string detail; // "OK" / "FAIL" / "42 ms" / "Timeout"
};

struct FullProbeResult
{
	bool discordOk = false;
	bool youtubeOk = false;
	bool telegramOk = false;
	int pingMs = -1;
	int httpOk = 0;
	int httpErr = 0;
	int pingOk = 0;
	int pingFail = 0;
	std::vector<TargetResult> targets;
};

const TargetDef* GetDefaultTargets(size_t& outCount);
FullProbeResult RunFullStandardProbe(std::atomic<bool>* cancelFlag = nullptr);

StrategyTestEntry ToStoreEntry(const FullProbeResult& probe, const StrategyTestEntry& previous);

}  // namespace ZapretStrategyProbe
