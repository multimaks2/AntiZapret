#pragma once



#include <string>

#include <vector>



enum class VpnDomainRuleAction : int

{

	Direct = 0,

	Proxy = 1,

	Reject = 2,

};



struct VpnDomainRule

{

	std::string address;

	VpnDomainRuleAction action = VpnDomainRuleAction::Proxy;

};



namespace VpnDomainRoutes

{

	bool Load(std::vector<VpnDomainRule>& outRules);

	void Save(const std::vector<VpnDomainRule>& rules);

}


