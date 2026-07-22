#include "vpn/vpn_store.h"

#include "app/settings_document.h"
#include "vpn/vpn_import.h"
#include "vpn/vpn_geo.h"
#include "zapret/zapret_paths.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace
{
	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t");
		return value.substr(start, end - start + 1);
	}

	int ParseInt(const std::string& value, int fallback)
	{
		if (value.empty())
			return fallback;
		return std::atoi(value.c_str());
	}

	long long ParseInt64(const std::string& value, long long fallback)
	{
		if (value.empty())
			return fallback;
		return std::atoll(value.c_str());
	}

	float ParseFloat(const std::string& value, float fallback)
	{
		if (value.empty())
			return fallback;
		return static_cast<float>(std::atof(value.c_str()));
	}
}

std::filesystem::path VpnStore::CacheDirectory() const
{
	return std::filesystem::path(ZapretPaths::GetCacheDirectory());
}

std::filesystem::path VpnStore::NodesFile() const
{
	return CacheDirectory() / L"nodes.txt";
}

std::filesystem::path VpnStore::StateFile() const
{
	return CacheDirectory() / L"state.ini";
}

void VpnStore::ParseSettingsLine(const std::string& key, const std::string& value, VpnStoreSettings& settings) const
{
	if (key == "active_uri")
		settings.activeUri = value;
	else if (key == "work_mode")
		settings.workMode = ParseInt(value, 1);
	else if (key == "transport_mode")
		settings.transportMode = ParseInt(value, 1);
	else if (key == "dns_mode")
		settings.dnsMode = ParseInt(value, 1);
	else if (key == "bootstrap_dns")
		settings.bootstrapDns = ParseInt(value, 2);
	else if (key == "bootstrap_type")
		settings.bootstrapType = ParseInt(value, 0);
	else if (key == "proxy_dns")
		settings.proxyDns = ParseInt(value, 0);
	else if (key == "proxy_type")
		settings.proxyType = ParseInt(value, 0);
	else if (key == "routing_revision")
		settings.routingRevision = ParseInt(value, 0);
	else if (key == "fix_discord")
		settings.fixDiscord = value == "1" || value == "true" || value == "yes";
	else if (key == "last_subscription_url")
		settings.lastSubscriptionUrl = value;
	else if (key == "subscription_expire_unix")
		settings.subscriptionExpireUnix = ParseInt64(value, 0);
}

void VpnStore::LoadSettings(VpnStoreSettings& settings) const
{
	settings = VpnStoreSettings {};

	SettingsDocument::Doc doc;
	{
		std::lock_guard<std::mutex> lock(SettingsDocument::Mutex());
		SettingsDocument::Load(doc);
	}

	const SettingsDocument::KeyMap keys = SettingsDocument::GetSection(doc, "vpn");
	for (const auto& kv : keys)
		ParseSettingsLine(kv.first, kv.second, settings);
}

void VpnStore::SaveSettings(const VpnStoreSettings& settings) const
{
	SettingsDocument::KeyMap keys;
	keys["active_uri"] = settings.activeUri;
	keys["work_mode"] = std::to_string(settings.workMode);
	keys["transport_mode"] = std::to_string(settings.transportMode);
	keys["dns_mode"] = std::to_string(settings.dnsMode);
	keys["bootstrap_dns"] = std::to_string(settings.bootstrapDns);
	keys["bootstrap_type"] = std::to_string(settings.bootstrapType);
	keys["proxy_dns"] = std::to_string(settings.proxyDns);
	keys["proxy_type"] = std::to_string(settings.proxyType);
	keys["routing_revision"] = std::to_string(settings.routingRevision);
	keys["fix_discord"] = settings.fixDiscord ? "1" : "0";
	keys["last_subscription_url"] = settings.lastSubscriptionUrl;
	keys["subscription_expire_unix"] = std::to_string(settings.subscriptionExpireUnix);
	SettingsDocument::UpsertSection("vpn", keys);
}

void VpnStore::Load(std::vector<VpnNode>& nodes, VpnStoreSettings* settings)
{
	nodes.clear();

	const std::filesystem::path nodesFile = NodesFile();
	std::ifstream input(nodesFile, std::ios::binary);
	if (!input)
		return;

	std::vector<std::string> lines;
	std::string line;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (!line.empty() && line[0] != ';' && line[0] != '#')
			lines.push_back(line);
	}

	std::unordered_map<std::string, VpnNode> stateByUri;
	{
		std::ifstream stateInput(StateFile(), std::ios::binary);
		if (stateInput)
		{
			VpnNode current;
			while (std::getline(stateInput, line))
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#')
					continue;

				if (line.front() == '[' && line.back() == ']')
				{
					if (!current.originalUri.empty())
						stateByUri[current.originalUri] = current;
					current = VpnNode {};
					continue;
				}

				const size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;

				const std::string key = Trim(line.substr(0, eq));
				const std::string value = Trim(line.substr(eq + 1));
				if (key == "uri")
					current.originalUri = value;
				else if (key == "ping_ms")
					current.pingMs = ParseInt(value, -1);
				else if (key == "speed_mbps")
					current.speedMbps = ParseFloat(value, -1.f);
				else if (key == "alive")
					current.alive = ParseInt(value, -1);
				else if (key == "last_used")
					current.lastUsed = value;
				else if (key == "country")
					current.country = value;
				else if (key == "name")
					current.name = value;
				else if (key == "group")
					current.group = value;
				else if (key == "source_url")
					current.sourceUrl = value;
			}

			if (!current.originalUri.empty())
				stateByUri[current.originalUri] = current;
		}
	}

	int nodeIndex = 1;
	for (const std::string& uri : lines)
	{
		VpnNode node;
		std::string error;
		if (!VpnImport::ParseShareLink(uri, node, nodeIndex, error))
			continue;

		const auto it = stateByUri.find(uri);
		if (it != stateByUri.end())
		{
			node.pingMs = it->second.pingMs;
			node.speedMbps = it->second.speedMbps;
			node.alive = it->second.alive;
			node.lastUsed = it->second.lastUsed;
			if (!it->second.country.empty())
				node.country = it->second.country;
			if (!it->second.name.empty())
				node.name = it->second.name;
			if (!it->second.group.empty())
				node.group = it->second.group;
			if (!it->second.sourceUrl.empty())
				node.sourceUrl = it->second.sourceUrl;
		}

		VpnImport::NormalizeNodeDisplay(node);

		if (node.country.empty())
			node.country = VpnGeo::GetCachedCountryCode(node.server);

		nodes.push_back(std::move(node));
		++nodeIndex;
	}

	if (settings)
	{
		LoadSettings(*settings);
	}
}

void VpnStore::Save(const std::vector<VpnNode>& nodes, const VpnStoreSettings* settings) const
{
	const std::filesystem::path cacheDir = CacheDirectory();
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);

	{
		std::ofstream output(NodesFile(), std::ios::binary | std::ios::trunc);
		if (output)
		{
			for (const VpnNode& node : nodes)
			{
				if (!node.originalUri.empty())
					output << node.originalUri << "\r\n";
			}
		}
	}

	{
		std::ofstream output(StateFile(), std::ios::binary | std::ios::trunc);
		if (!output)
			return;

		output << "; AntiZapret VPN cache state\r\n";
		for (const VpnNode& node : nodes)
		{
			if (node.originalUri.empty())
				continue;

			output << "[node]\r\n";
			output << "uri=" << node.originalUri << "\r\n";
			output << "ping_ms=" << node.pingMs << "\r\n";
			output << "speed_mbps=" << node.speedMbps << "\r\n";
			output << "alive=" << node.alive << "\r\n";
			output << "last_used=" << node.lastUsed << "\r\n";
			output << "country=" << node.country << "\r\n";
			output << "name=" << node.name << "\r\n";
			output << "group=" << node.group << "\r\n";
			output << "source_url=" << node.sourceUrl << "\r\n";
		}
	}

	if (settings)
		SaveSettings(*settings);
}
