#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_transport_settings.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

class VpnStore
{
public:
	using SubscriptionCards = std::map<std::string, VpnStoreSettings>;
	void Load(std::vector<VpnNode>& nodes, VpnStoreSettings* settings = nullptr);
	void Save(const std::vector<VpnNode>& nodes, const VpnStoreSettings* settings = nullptr, bool saveSubscriptionCard = false) const;

	void LoadSettings(VpnStoreSettings& settings, SubscriptionCards* cards = nullptr) const;
	void SaveSettings(const VpnStoreSettings& settings, bool saveSubscriptionCard = false) const;

private:
	std::filesystem::path CacheDirectory() const;
	std::filesystem::path NodesFile() const;
	std::filesystem::path StateFile() const;
	void ParseSettingsLine(const std::string& key, const std::string& value, VpnStoreSettings& settings) const;
};
