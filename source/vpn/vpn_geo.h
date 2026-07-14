#pragma once

#include <string>

namespace VpnGeo
{
	void EnsureCacheLoaded();
	std::string CountryCodeToFlag(const std::string& countryCode);
	std::string GetCachedCountryCode(const std::string& ip);
	bool LookupCountryCode(const std::string& ip, std::string& outCode);
	void RememberCountryCode(const std::string& ip, const std::string& code);
	bool IsPublicIp(const std::string& ip);
}
