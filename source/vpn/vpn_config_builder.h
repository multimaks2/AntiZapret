#pragma once

#include "vpn/vpn_node.h"
#include "vpn/vpn_routing.h"
#include "vpn/vpn_transport_settings.h"

#include <string>

namespace VpnConfigBuilder
{
	bool WriteRuntimeConfig(
		const VpnNode& node,
		VpnRoutingPreset preset,
		const VpnStoreSettings& transport,
		const std::wstring& vpnDirectory,
		std::string& outError,
		bool fetchGeosites = true);
}
