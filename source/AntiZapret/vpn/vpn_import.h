#pragma once

#include "vpn/vpn_node.h"

#include <string>
#include <vector>

struct VpnImportResult
{
	std::vector<VpnNode> nodes;
	std::vector<std::string> errors;
	int duplicatesSkipped = 0;
	// From subscription-userinfo expire= (Unix seconds). 0 if absent.
	long long subscriptionExpireUnix = 0;
	long long subscriptionUploadBytes = 0;
	long long subscriptionDownloadBytes = 0;
	long long subscriptionTotalBytes = 0;
	std::string subscriptionSupportUrl;
	std::string subscriptionProfileTitle;
	std::string subscriptionAnnounce;
	std::string subscriptionProviderId;
	std::string subscriptionUserId;
	std::string subscriptionIconUrl;
	// True when HTTP response had subscription card headers (not a plain URI list / GitHub file).
	bool hasSubscriptionCard = false;
};

namespace VpnImport
{
	bool ReadClipboardUtf8(std::string& outText);
	VpnImportResult ImportFromText(const std::string& text, int nextNodeIndex);
	bool ParseShareLink(const std::string& line, VpnNode& outNode, int nodeIndex, std::string& outError);
	// Clean name glyphs, guess country from host, fix Capybara grouping.
	void NormalizeNodeDisplay(VpnNode& node);

	// MachineGuid-based HWID used for subscription x-hwid when custom override is empty.
	std::string GetSystemHwid();
}
