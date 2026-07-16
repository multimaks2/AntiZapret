#pragma once

#include <string>
#include <vector>

struct VpnNodeHistoryEntry
{
	std::string time;
	std::string value;
};

struct VpnNode
{
	std::string id;
	std::string name;
	std::string scheme;
	std::string server;
	int port = 0;
	bool tls = false;
	std::string group;
	std::string tags;
	std::string country;
	std::string originalUri;
	std::string sourceUrl; // subscription URL when imported from feed
	int pingMs = -1;
	float speedMbps = -1.f;
	int alive = -1;
	std::string lastUsed;
	std::vector<VpnNodeHistoryEntry> pingHistory;
	std::vector<VpnNodeHistoryEntry> speedHistory;
};
