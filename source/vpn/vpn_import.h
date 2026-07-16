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
};

namespace VpnImport
{
	bool ReadClipboardUtf8(std::string& outText);
	VpnImportResult ImportFromText(const std::string& text, int nextNodeIndex);
	bool ParseShareLink(const std::string& line, VpnNode& outNode, int nodeIndex, std::string& outError);
	// Clean name glyphs, guess country from host, fix Capybara grouping.
	void NormalizeNodeDisplay(VpnNode& node);
}
