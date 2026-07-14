#include "vpn/vpn_domain_routes.h"



#include "zapret/zapret_paths.h"



#include <cctype>

#include <filesystem>

#include <fstream>



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



	int ParseInt(const std::string& value, int fallback)

	{

		if (value.empty())

			return fallback;

		return std::atoi(value.c_str());

	}



	std::filesystem::path RulesFile()

	{

		return std::filesystem::path(ZapretPaths::GetVpnDirectory()) / L"cache" / L"domain-rules.ini";

	}

}



bool VpnDomainRoutes::Load(std::vector<VpnDomainRule>& outRules)

{

	outRules.clear();



	std::ifstream input(RulesFile(), std::ios::binary);

	if (!input)

		return false;



	std::string line;

	VpnDomainRule current;

	bool hasRule = false;



	while (std::getline(input, line))

	{

		line = Trim(line);

		if (line.empty() || line[0] == ';' || line[0] == '#')

			continue;



		if (line.front() == '[' && line.back() == ']')

		{

			if (hasRule && !current.address.empty())

				outRules.push_back(current);

			current = VpnDomainRule {};

			hasRule = true;

			continue;

		}



		const size_t eq = line.find('=');

		if (eq == std::string::npos || !hasRule)

			continue;



		const std::string key = Trim(line.substr(0, eq));

		const std::string value = Trim(line.substr(eq + 1));

		if (key == "address")

			current.address = value;

		else if (key == "action")

		{

			const int action = ParseInt(value, static_cast<int>(VpnDomainRuleAction::Proxy));

			if (action >= static_cast<int>(VpnDomainRuleAction::Direct)

				&& action <= static_cast<int>(VpnDomainRuleAction::Reject))

			{

				current.action = static_cast<VpnDomainRuleAction>(action);

			}

		}

	}



	if (hasRule && !current.address.empty())

		outRules.push_back(current);



	return true;

}



void VpnDomainRoutes::Save(const std::vector<VpnDomainRule>& rules)

{

	const std::filesystem::path path = RulesFile();

	std::error_code ec;

	std::filesystem::create_directories(path.parent_path(), ec);



	std::ofstream output(path, std::ios::binary | std::ios::trunc);

	if (!output)

		return;



	output << "; AntiZapret domain routing\r\n";

	int index = 0;

	for (const VpnDomainRule& rule : rules)

	{

		if (rule.address.empty())

			continue;



		output << "[rule_" << index++ << "]\r\n";

		output << "address=" << rule.address << "\r\n";

		output << "action=" << static_cast<int>(rule.action) << "\r\n";

	}

}


