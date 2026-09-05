#pragma once

#include <string>
#include <vector>

namespace DnsManager
{

struct AdapterInfo
{
	std::string name;
	std::wstring nameWide;
	unsigned long ifIndex = 0;
	bool isUp = false;
	bool isLoopback = false;
	bool isTunnel = false;
	std::string primaryDns;
	std::string alternateDns;
	std::string description;
};

struct PublicServer
{
	const char* name = nullptr;
	const char* ipv4 = nullptr;
	const char* provider = nullptr;
	const char* tags = nullptr; // comma-separated search tags
};

const PublicServer* GetPublicServers(size_t& outCount);

bool RefreshAdapters(std::vector<AdapterInfo>& outAdapters);
int FindPreferredAdapterIndex(const std::vector<AdapterInfo>& adapters);

// ICMP RTT; returns -1 on timeout/error.
int MeasureServerLatencyMs(const char* ipv4, int timeoutMs = 1500);

bool ApplyStaticDns(
	const std::wstring& adapterName,
	const char* primaryIpv4,
	const char* alternateIpv4,
	std::string& errorOut);

bool ApplyDhcpDns(const std::wstring& adapterName, std::string& errorOut);

}  // namespace DnsManager
