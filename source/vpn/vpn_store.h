#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_transport_settings.h"

#include <filesystem>
#include <string>
#include <vector>

class VpnStore
{
public:
	void Load(std::vector<VpnNode>& nodes, VpnStoreSettings* settings = nullptr);
	void Save(const std::vector<VpnNode>& nodes, const VpnStoreSettings* settings = nullptr) const;

	void LoadSettings(VpnStoreSettings& settings) const;
	void SaveSettings(const VpnStoreSettings& settings) const;

private:
	std::filesystem::path CacheDirectory() const;
	std::filesystem::path NodesFile() const;
	std::filesystem::path StateFile() const;
	void ParseSettingsLine(const std::string& key, const std::string& value, VpnStoreSettings& settings) const;
};
