#pragma once

#include "vpn/vpn_node.h"

#include <string>
#include <vector>

struct VpnImportResult
{
	std::vector<VpnNode> nodes;
	std::vector<std::string> errors;
	int duplicatesSkipped = 0;
};

namespace VpnImport
{
	bool ReadClipboardUtf8(std::string& outText);
	VpnImportResult ImportFromText(const std::string& text, int nextNodeIndex);
	bool ParseShareLink(const std::string& line, VpnNode& outNode, int nodeIndex, std::string& outError);
}
