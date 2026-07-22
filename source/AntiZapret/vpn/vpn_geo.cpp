#include "vpn/vpn_geo.h"

#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#pragma comment(lib, "wininet.lib")

namespace
{
	std::mutex g_cacheMutex;
	std::unordered_map<std::string, std::string> g_ipToCountry;
	bool g_cacheLoaded = false;

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

	std::string ToUpperAscii(std::string value)
	{
		for (char& ch : value)
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		return value;
	}

	void AppendUtf8(std::string& out, uint32_t codepoint)
	{
		if (codepoint <= 0x7F)
		{
			out.push_back(static_cast<char>(codepoint));
			return;
		}
		if (codepoint <= 0x7FF)
		{
			out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			return;
		}
		if (codepoint <= 0xFFFF)
		{
			out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
			return;
		}

		out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
		out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
	}

	std::filesystem::path GeoCacheFile()
	{
		return std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"geoip.ini";
	}

	std::string JsonStringField(const std::string& json, const char* key)
	{
		const std::string pattern = std::string("\"") + key + "\":\"";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return {};

		size_t start = pos + pattern.size();
		std::string result;
		while (start < json.size())
		{
			const char ch = json[start++];
			if (ch == '\\' && start < json.size())
			{
				result.push_back(json[start++]);
				continue;
			}
			if (ch == '"')
				break;
			result.push_back(ch);
		}
		return result;
	}

	bool FetchCountryCodeFromApi(const std::string& ip, std::string& outCode)
	{
		const std::string url = "http://ip-api.com/json/" + ip + "?fields=status,countryCode";
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
			return false;

		DWORD timeoutMs = 8000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
			0);

		if (!request)
		{
			InternetCloseHandle(internet);
			return false;
		}

		std::string response;
		char buffer[1024];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			response.append(buffer, buffer + read);

		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (JsonStringField(response, "status") != "success")
			return false;

		outCode = ToUpperAscii(JsonStringField(response, "countryCode"));
		return outCode.size() == 2;
	}

	bool ParseIpv4Octet(const std::string& ip, size_t& index, int& outOctet)
	{
		if (index >= ip.size() || !std::isdigit(static_cast<unsigned char>(ip[index])))
			return false;

		outOctet = 0;
		while (index < ip.size() && std::isdigit(static_cast<unsigned char>(ip[index])))
		{
			outOctet = outOctet * 10 + (ip[index] - '0');
			if (outOctet > 255)
				return false;
			++index;
		}
		return true;
	}
}

void VpnGeo::EnsureCacheLoaded()
{
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	if (g_cacheLoaded)
		return;

	g_cacheLoaded = true;
	std::ifstream input(GeoCacheFile(), std::ios::binary);
	if (!input)
		return;

	std::string line;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		const std::string ip = Trim(line.substr(0, eq));
		const std::string code = ToUpperAscii(Trim(line.substr(eq + 1)));
		if (ip.empty() || code.size() != 2)
			continue;

		g_ipToCountry[ip] = code;
	}
}

std::string VpnGeo::CountryCodeToFlag(const std::string& countryCode)
{
	if (countryCode.size() != 2)
		return {};

	const char a = static_cast<char>(std::toupper(static_cast<unsigned char>(countryCode[0])));
	const char b = static_cast<char>(std::toupper(static_cast<unsigned char>(countryCode[1])));
	if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z')
		return {};

	std::string flag;
	AppendUtf8(flag, 0x1F1E6u + static_cast<unsigned>(a - 'A'));
	AppendUtf8(flag, 0x1F1E6u + static_cast<unsigned>(b - 'A'));
	return flag;
}

std::string VpnGeo::CountryCodeToName(const std::string& countryCode)
{
	if (countryCode.size() != 2)
		return {};

	const std::string code = ToUpperAscii(countryCode);
	static const std::unordered_map<std::string, const char*> kNames = {
		{ "AD", "Андорра" },
		{ "AE", "ОАЭ" },
		{ "AL", "Албания" },
		{ "AM", "Армения" },
		{ "AR", "Аргентина" },
		{ "AT", "Австрия" },
		{ "AU", "Австралия" },
		{ "AZ", "Азербайджан" },
		{ "BA", "Босния и Герцеговина" },
		{ "BE", "Бельгия" },
		{ "BG", "Болгария" },
		{ "BR", "Бразилия" },
		{ "BY", "Беларусь" },
		{ "CA", "Канада" },
		{ "CH", "Швейцария" },
		{ "CL", "Чили" },
		{ "CN", "Китай" },
		{ "CY", "Кипр" },
		{ "CZ", "Чехия" },
		{ "DE", "Германия" },
		{ "DK", "Дания" },
		{ "EE", "Эстония" },
		{ "EG", "Египет" },
		{ "ES", "Испания" },
		{ "FI", "Финляндия" },
		{ "FR", "Франция" },
		{ "GB", "Великобритания" },
		{ "GE", "Грузия" },
		{ "GR", "Греция" },
		{ "HK", "Гонконг" },
		{ "HR", "Хорватия" },
		{ "HU", "Венгрия" },
		{ "ID", "Индонезия" },
		{ "IE", "Ирландия" },
		{ "IL", "Израиль" },
		{ "IN", "Индия" },
		{ "IS", "Исландия" },
		{ "IT", "Италия" },
		{ "JP", "Япония" },
		{ "KG", "Кыргызстан" },
		{ "KR", "Южная Корея" },
		{ "KZ", "Казахстан" },
		{ "LT", "Литва" },
		{ "LU", "Люксембург" },
		{ "LV", "Латвия" },
		{ "MD", "Молдова" },
		{ "MK", "Северная Македония" },
		{ "MX", "Мексика" },
		{ "MY", "Малайзия" },
		{ "NL", "Нидерланды" },
		{ "NO", "Норвегия" },
		{ "NZ", "Новая Зеландия" },
		{ "PL", "Польша" },
		{ "PT", "Португалия" },
		{ "RO", "Румыния" },
		{ "RS", "Сербия" },
		{ "RU", "Россия" },
		{ "SE", "Швеция" },
		{ "SG", "Сингапур" },
		{ "SI", "Словения" },
		{ "SK", "Словакия" },
		{ "TH", "Таиланд" },
		{ "TR", "Турция" },
		{ "TW", "Тайвань" },
		{ "UA", "Украина" },
		{ "UK", "Великобритания" },
		{ "US", "США" },
		{ "UZ", "Узбекистан" },
		{ "VN", "Вьетнам" },
		{ "XK", "Косово" },
	};

	const auto it = kNames.find(code);
	if (it != kNames.end())
		return it->second;
	return code;
}

std::string VpnGeo::GetCachedCountryCode(const std::string& ip)
{
	EnsureCacheLoaded();
	std::lock_guard<std::mutex> lock(g_cacheMutex);
	const auto it = g_ipToCountry.find(ip);
	return it != g_ipToCountry.end() ? it->second : std::string {};
}

bool VpnGeo::IsPublicIp(const std::string& ip)
{
	size_t index = 0;
	int octets[4] = {};
	for (int i = 0; i < 4; ++i)
	{
		if (!ParseIpv4Octet(ip, index, octets[i]))
			return false;
		if (i < 3)
		{
			if (index >= ip.size() || ip[index] != '.')
				return false;
			++index;
		}
	}
	if (index != ip.size())
		return false;

	if (octets[0] == 10)
		return false;
	if (octets[0] == 127)
		return false;
	if (octets[0] == 192 && octets[1] == 168)
		return false;
	if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31)
		return false;
	if (octets[0] == 169 && octets[1] == 254)
		return false;
	return true;
}

bool VpnGeo::LookupCountryCode(const std::string& ip, std::string& outCode)
{
	outCode.clear();
	if (ip.empty() || !IsPublicIp(ip))
		return false;

	const std::string cached = GetCachedCountryCode(ip);
	if (!cached.empty())
	{
		outCode = cached;
		return true;
	}

	if (!FetchCountryCodeFromApi(ip, outCode))
		return false;

	RememberCountryCode(ip, outCode);
	return true;
}

void VpnGeo::RememberCountryCode(const std::string& ip, const std::string& code)
{
	const std::string normalized = ToUpperAscii(code);
	if (ip.empty() || normalized.size() != 2)
		return;

	EnsureCacheLoaded();
	{
		std::lock_guard<std::mutex> lock(g_cacheMutex);
		g_ipToCountry[ip] = normalized;
	}

	const std::filesystem::path cacheFile = GeoCacheFile();
	std::error_code ec;
	std::filesystem::create_directories(cacheFile.parent_path(), ec);

	std::ofstream output(cacheFile, std::ios::binary | std::ios::app);
	if (!output)
		return;

	output << ip << "=" << normalized << "\r\n";
}
