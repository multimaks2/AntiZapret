#pragma once

#include <string>

namespace VpnDiscordVoiceRules
{
	void AppendRules(std::string& yaml, const char* action);
	void AppendDomainAndVoiceRules(std::string& yaml, const char* action);
}
