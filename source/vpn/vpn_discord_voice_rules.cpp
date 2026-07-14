#include "vpn/vpn_discord_voice_rules.h"

#include "vpn/vpn_service_routes.h"
#include "zapret/zapret_paths.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

namespace
{
	std::string Trim(std::string value)
	{
		while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		const size_t start = value.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return {};
		const size_t end = value.find_last_not_of(" \t\r\n");
		return value.substr(start, end - start + 1);
	}

	bool LooksLikeCidr(const std::string& value)
	{
		return value.find('/') != std::string::npos;
	}

	void AppendLine(std::string& yaml, const char* action, const std::string& rule)
	{
		yaml += "  - ";
		yaml += rule;
		yaml += ",";
		yaml += action;
		yaml += "\n";
	}

	void AppendIpRulesFromFile(std::string& yaml, const char* action)
	{
		const std::filesystem::path path =
			std::filesystem::path(ZapretPaths::GetListsDirectory()) / L"ipset-discord.txt";

		std::ifstream input(path, std::ios::binary);
		if (!input)
			return;

		std::string line;
		while (std::getline(input, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#')
				continue;
			if (!LooksLikeCidr(line))
				continue;
			AppendLine(yaml, action, "IP-CIDR," + line);
		}
	}

	void AppendFallbackIpRules(std::string& yaml, const char* action)
	{
		static const char* kFallbackCidrs[] = {
			"34.0.48.0/21",
			"35.207.64.0/23",
			"35.213.0.0/24",
			"35.214.128.0/22",
			"35.215.72.0/23",
			"35.217.0.0/22",
			"66.22.196.0/22",
			"162.159.130.0/24",
			"172.65.202.0/24",
		};

		for (const char* cidr : kFallbackCidrs)
			AppendLine(yaml, action, std::string("IP-CIDR,") + cidr);
	}
}

void VpnDiscordVoiceRules::AppendRules(std::string& yaml, const char* action)
{
	AppendLine(yaml, action, "PROCESS-NAME,Discord.exe");
	AppendLine(yaml, action, "AND,((DST-PORT,19294-19344),(NETWORK,UDP))");
	AppendLine(yaml, action, "AND,((DST-PORT,50000-65535),(NETWORK,UDP))");

	const size_t before = yaml.size();
	AppendIpRulesFromFile(yaml, action);
	if (yaml.size() == before)
		AppendFallbackIpRules(yaml, action);
}

void VpnDiscordVoiceRules::AppendDomainAndVoiceRules(std::string& yaml, const char* action)
{
	std::vector<std::string> domains;
	VpnServiceRoutes::CollectFallbackDomains("discord", domains);
	for (const std::string& domain : domains)
		AppendLine(yaml, action, "DOMAIN-SUFFIX," + domain);

	AppendRules(yaml, action);
}
