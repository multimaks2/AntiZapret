#include "vpn/vpn_import.h"

#include "app/app_log.h"
#include "app/app_settings.h"

#include <Windows.h>
#include <WinInet.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
	void LogImport(const std::string& message)
	{
		if (!message.empty())
			AppLog::Instance().Append(LogSource::VpnRouting, message);
	}

	void LogImportf(const char* fmt, ...)
	{
		char buffer[1024] = {};
		va_list args;
		va_start(args, fmt);
		vsnprintf(buffer, sizeof buffer, fmt, args);
		va_end(args);
		LogImport(buffer);
	}

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

	std::string ToLower(std::string value)
	{
		for (char& ch : value)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return value;
	}

	bool StartsWithIgnoreCase(const std::string& value, const char* prefix)
	{
		const std::string lower = ToLower(value);
		const std::string prefixLower = ToLower(std::string(prefix));
		return lower.rfind(prefixLower, 0) == 0;
	}

	bool Base64Decode(const std::string& input, std::string& out)
	{
		std::string normalized;
		normalized.reserve(input.size());
		for (char ch : input)
		{
			if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
				continue;
			if (ch == '-')
				normalized.push_back('+');
			else if (ch == '_')
				normalized.push_back('/');
			else
				normalized.push_back(ch);
		}
		while (normalized.size() % 4 != 0)
			normalized.push_back('=');

		DWORD required = 0;
		if (!CryptStringToBinaryA(
				normalized.c_str(),
				static_cast<DWORD>(normalized.size()),
				CRYPT_STRING_BASE64,
				nullptr,
				&required,
				nullptr,
				nullptr))
			return false;

		std::vector<BYTE> buffer(required);
		if (!CryptStringToBinaryA(
				normalized.c_str(),
				static_cast<DWORD>(normalized.size()),
				CRYPT_STRING_BASE64,
				buffer.data(),
				&required,
				nullptr,
				nullptr))
			return false;

		out.assign(reinterpret_cast<const char*>(buffer.data()), required);
		return true;
	}

	size_t SkipJsonWhitespace(const std::string& json, size_t index)
	{
		while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])))
			++index;
		return index;
	}

	std::string UrlDecode(const std::string& value)
	{
		std::string result;
		result.reserve(value.size());
		for (size_t i = 0; i < value.size(); ++i)
		{
			if (value[i] == '%' && i + 2 < value.size())
			{
				const char hex[] = { value[i + 1], value[i + 2], '\0' };
				result.push_back(static_cast<char>(strtoul(hex, nullptr, 16)));
				i += 2;
			}
			else if (value[i] == '+')
				result.push_back(' ');
			else
				result.push_back(value[i]);
		}
		return result;
	}

	std::string JsonStringField(const std::string& json, const char* key)
	{
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return {};

		size_t start = SkipJsonWhitespace(json, pos + pattern.size());
		if (start >= json.size() || json[start] != '"')
			return {};
		++start;

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

	int JsonIntField(const std::string& json, const char* key, int fallback = 0)
	{
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t pos = json.find(pattern);
		if (pos == std::string::npos)
			return fallback;

		size_t start = SkipJsonWhitespace(json, pos + pattern.size());
		if (start < json.size() && json[start] == '"')
		{
			++start;
			return std::atoi(json.c_str() + start);
		}

		return std::atoi(json.c_str() + start);
	}

	std::string SanitizeDisplayName(const std::string& value, std::string* outCountryCode = nullptr);
	std::string GuessCountryFromHost(const std::string& server);
	void AssignNodeIdentity(VpnNode& node, int nodeIndex);

	bool DecodeUtf8Codepoint(const std::string& text, size_t& index, uint32_t& codepoint)
	{
		if (index >= text.size())
			return false;
		const unsigned char lead = static_cast<unsigned char>(text[index]);
		if (lead < 0x80)
		{
			codepoint = lead;
			++index;
			return true;
		}
		size_t need = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			need = 1;
			codepoint = lead & 0x1F;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			need = 2;
			codepoint = lead & 0x0F;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			need = 3;
			codepoint = lead & 0x07;
		}
		else
		{
			codepoint = lead;
			++index;
			return true;
		}
		if (index + need >= text.size())
			return false;
		++index;
		for (size_t n = 0; n < need; ++n)
		{
			const unsigned char ch = static_cast<unsigned char>(text[index++]);
			if ((ch & 0xC0) != 0x80)
				return false;
			codepoint = (codepoint << 6) | (ch & 0x3F);
		}
		return true;
	}

	bool IsRegionalIndicator(uint32_t codepoint)
	{
		return codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF;
	}

	bool IsAllowedDisplayCodepoint(uint32_t codepoint)
	{
		if (codepoint == 0xFFFD) // replacement char �
			return false;
		if (codepoint < 0x20 || codepoint == 0x7F)
			return false;
		if (codepoint <= 0x7E)
			return true; // ASCII printable
		if (codepoint >= 0x00A0 && codepoint <= 0x00FF)
			return true; // Latin-1
		if (codepoint >= 0x0400 && codepoint <= 0x04FF)
			return true; // Cyrillic
		if (codepoint >= 0x2010 && codepoint <= 0x2027)
			return true; // dashes / quotes
		if (codepoint == 0x2116) // №
			return true;
		return false;
	}

	std::string SanitizeDisplayName(const std::string& value, std::string* outCountryCode)
	{
		std::string out;
		out.reserve(value.size());
		std::string flagLetters;
		size_t index = 0;
		while (index < value.size())
		{
			const size_t start = index;
			uint32_t codepoint = 0;
			if (!DecodeUtf8Codepoint(value, index, codepoint))
				break;

			if (IsRegionalIndicator(codepoint))
			{
				flagLetters.push_back(static_cast<char>('A' + (codepoint - 0x1F1E6)));
				continue;
			}
			if (codepoint == 0xFE0F || codepoint == 0x200D || codepoint == 0xFE0E)
				continue;
			if (!IsAllowedDisplayCodepoint(codepoint))
				continue;

			out.append(value, start, index - start);
		}

		out = Trim(out);
		while (!out.empty() && (out.front() == ' ' || out.front() == '\t' || out.front() == '-' || out.front() == '|'))
			out.erase(out.begin());
		while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '-' || out.back() == '|'))
			out.pop_back();
		// Collapse leftover double spaces after glyph removal.
		std::string collapsed;
		collapsed.reserve(out.size());
		bool prevSpace = false;
		for (char ch : out)
		{
			const bool isSpace = ch == ' ' || ch == '\t';
			if (isSpace && prevSpace)
				continue;
			collapsed.push_back(isSpace ? ' ' : ch);
			prevSpace = isSpace;
		}
		out = Trim(collapsed);

		if (outCountryCode && flagLetters.size() >= 2)
		{
			outCountryCode->clear();
			outCountryCode->push_back(flagLetters[0]);
			outCountryCode->push_back(flagLetters[1]);
		}
		return out;
	}

	bool HostEndsWith(const std::string& host, const char* suffix)
	{
		const size_t suffixLen = std::strlen(suffix);
		return host.size() >= suffixLen
			&& host.compare(host.size() - suffixLen, suffixLen, suffix) == 0;
	}

	bool IsRussianInfraHost(const std::string& hostLower)
	{
		return hostLower.find("capycore.") != std::string::npos
			|| hostLower.find("ngenix.") != std::string::npos
			|| hostLower.find("capybara.") != std::string::npos
			|| HostEndsWith(hostLower, ".ru")
			|| HostEndsWith(hostLower, ".su");
	}

	std::string GuessCountryFromHost(const std::string& server)
	{
		std::string host = ToLower(server);
		const size_t slash = host.find('/');
		if (slash != std::string::npos)
			host = host.substr(0, slash);
		const size_t colon = host.rfind(':');
		if (colon != std::string::npos && host.find(':') == colon)
			host = host.substr(0, colon);

		// Russian CDN / bypass hosts (194.capycore.ru, ngenix, …) — always RU.
		if (IsRussianInfraHost(host))
			return "RU";

		std::string label = host;
		const size_t dot = host.find('.');
		if (dot != std::string::npos)
			label = host.substr(0, dot);
		while (!label.empty() && std::isdigit(static_cast<unsigned char>(label.back())))
			label.pop_back();

		if (label == "spb" || label == "msk" || label == "data")
			return "RU";

		// Country subdomain: de.capynode.com / lt2.capynode.com → DE / LT
		if (label.size() == 2
			&& std::isalpha(static_cast<unsigned char>(label[0]))
			&& std::isalpha(static_cast<unsigned char>(label[1]))
			&& host.find("capynode.") != std::string::npos)
		{
			for (char& ch : label)
				ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			return label;
		}

		if (host.size() >= 3 && host[2] == '.'
			&& std::isalpha(static_cast<unsigned char>(host[0]))
			&& std::isalpha(static_cast<unsigned char>(host[1]))
			&& host.find("capynode.") != std::string::npos)
		{
			char code[3] = {
				static_cast<char>(std::toupper(static_cast<unsigned char>(host[0]))),
				static_cast<char>(std::toupper(static_cast<unsigned char>(host[1]))),
				'\0'};
			return code;
		}
		return {};
	}

	bool LooksLikeCapybaraNode(const VpnNode& node)
	{
		const std::string nameLower = ToLower(node.name);
		const std::string serverLower = ToLower(node.server);
		const std::string groupLower = ToLower(node.group);
		if (groupLower.find("capybara") != std::string::npos || groupLower.find("copybara") != std::string::npos)
			return true;
		if (serverLower.find("capynode.") != std::string::npos
			|| serverLower.find("capycore.") != std::string::npos
			|| serverLower.find("capybara") != std::string::npos)
			return true;
		// UTF-8 "обход" lowercased roughly — check raw Cyrillic too.
		if (nameLower.find("obhod") != std::string::npos)
			return true;
		if (node.name.find("ОБХОД") != std::string::npos
			|| node.name.find("Обход") != std::string::npos
			|| node.name.find("обход") != std::string::npos)
			return true;
		return false;
	}

	void NormalizeNodeDisplayFields(VpnNode& node)
	{
		std::string countryFromEmoji;
		node.name = SanitizeDisplayName(node.name, &countryFromEmoji);
		if (node.country.empty() && !countryFromEmoji.empty())
			node.country = countryFromEmoji;

		const std::string guessed = GuessCountryFromHost(node.server);
		if (!guessed.empty())
		{
			const std::string hostLower = ToLower(node.server);
			// Domain-based RU (capycore/ngenix/.ru) must override stale wrong codes like FM.
			if (node.country.empty() || IsRussianInfraHost(hostLower))
				node.country = guessed;
		}

		if (LooksLikeCapybaraNode(node)
			&& (node.group.empty() || node.group == "Imported"
				|| ToLower(node.group).find("copybara") != std::string::npos))
		{
			node.group = "Capybara VPN";
		}
	}

	std::string ProviderNameFromUrl(const std::string& url)
	{
		std::string host;
		const size_t scheme = url.find("://");
		size_t start = scheme == std::string::npos ? 0 : scheme + 3;
		const size_t end = url.find_first_of("/?#", start);
		host = end == std::string::npos ? url.substr(start) : url.substr(start, end - start);
		host = ToLower(host);
		if (host.rfind("www.", 0) == 0)
			host = host.substr(4);

		if (host.find("capybara") != std::string::npos)
			return "Capybara VPN";

		const size_t dot = host.find('.');
		std::string label = dot == std::string::npos ? host : host.substr(0, dot);
		if (label.empty())
			return "Subscription";
		label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
		return label;
	}

	std::string DecodeProfileTitleHeader(const std::string& raw)
	{
		std::string value = Trim(raw);
		if (StartsWithIgnoreCase(value, "base64:"))
		{
			std::string decoded;
			if (Base64Decode(value.substr(7), decoded))
				return Trim(decoded);
		}
		return value;
	}

	std::string QueryHttpHeader(HINTERNET request, const char* headerName)
	{
		char buffer[512] = {};
		const size_t nameLen = std::strlen(headerName);
		if (nameLen + 1 >= sizeof(buffer))
			return {};
		std::memcpy(buffer, headerName, nameLen + 1);
		DWORD bufferSize = sizeof(buffer);
		DWORD index = 0;
		if (!HttpQueryInfoA(request, HTTP_QUERY_CUSTOM, buffer, &bufferSize, &index))
			return {};
		buffer[sizeof(buffer) - 1] = '\0';
		return Trim(std::string(buffer));
	}

	long long ParseSubscriptionExpireUnix(const std::string& userInfo)
	{
		if (userInfo.empty())
			return 0;

		std::string lower = userInfo;
		for (char& ch : lower)
		{
			if (ch >= 'A' && ch <= 'Z')
				ch = static_cast<char>(ch - 'A' + 'a');
		}

		size_t pos = lower.find("expire=");
		if (pos == std::string::npos)
			pos = lower.find("expire:");
		if (pos == std::string::npos)
			return 0;

		while (pos < lower.size() && lower[pos] != '=' && lower[pos] != ':')
			++pos;
		if (pos >= lower.size())
			return 0;
		++pos;
		while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '\t'))
			++pos;

		long long value = std::atoll(userInfo.c_str() + pos);
		// Some panels send milliseconds.
		if (value > 100000000000LL)
			value /= 1000;
		if (value < 1000000000LL) // before ~2001 — ignore junk
			return 0;
		return value;
	}

	void AssignNodeIdentity(VpnNode& node, int nodeIndex)
	{
		if (node.id.empty())
		{
			char buffer[32] = {};
			snprintf(buffer, sizeof buffer, "node-%d", nodeIndex);
			node.id = buffer;
		}
		if (node.group.empty())
			node.group = "Imported";
		if (node.name.empty())
			node.name = node.server.empty() ? node.id : node.server;

		NormalizeNodeDisplayFields(node);
	}

	bool ParseHostPort(const std::string& hostPort, std::string& host, int& port, int defaultPort)
	{
		if (hostPort.empty())
			return false;

		if (hostPort.front() == '[')
		{
			const size_t end = hostPort.find(']');
			if (end == std::string::npos)
				return false;
			host = hostPort.substr(1, end - 1);
			if (end + 1 < hostPort.size() && hostPort[end + 1] == ':')
				port = std::atoi(hostPort.c_str() + end + 2);
			else
				port = defaultPort;
			return true;
		}

		const size_t colon = hostPort.rfind(':');
		if (colon == std::string::npos)
		{
			host = hostPort;
			port = defaultPort;
			return true;
		}

		host = hostPort.substr(0, colon);
		port = std::atoi(hostPort.c_str() + colon + 1);
		return !host.empty() && port > 0;
	}

	bool QueryContainsTls(const std::string& query)
	{
		const std::string lower = ToLower(query);
		return lower.find("security=tls") != std::string::npos
			|| lower.find("security=reality") != std::string::npos
			|| lower.find("security=xtls") != std::string::npos
			|| lower.find("allowinsecure=0") != std::string::npos;
	}

	bool ParseStandardUri(const std::string& scheme, const std::string& rest, VpnNode& node, int nodeIndex, std::string& error)
	{
		std::string body = rest;
		std::string fragment;
		const size_t hashPos = body.find('#');
		if (hashPos != std::string::npos)
		{
			fragment = UrlDecode(body.substr(hashPos + 1));
			body = body.substr(0, hashPos);
		}

		std::string query;
		const size_t queryPos = body.find('?');
		if (queryPos != std::string::npos)
		{
			query = body.substr(queryPos + 1);
			body = body.substr(0, queryPos);
		}

		const size_t atPos = body.rfind('@');
		std::string hostPort = atPos == std::string::npos ? body : body.substr(atPos + 1);
		if (!ParseHostPort(hostPort, node.server, node.port, node.port > 0 ? node.port : 443))
		{
			error = "Не удалось разобрать host:port.";
			return false;
		}

		node.scheme = scheme;
		node.tls = QueryContainsTls(query) || node.port == 443 || node.port == 8443;
		if (!fragment.empty())
			node.name = fragment;
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool ParseVmessJson(const std::string& json, VpnNode& node, int nodeIndex, std::string& error)
	{
		node.scheme = "vmess";
		node.name = JsonStringField(json, "ps");
		node.server = JsonStringField(json, "add");
		node.port = JsonIntField(json, "port", 443);
		const std::string net = JsonStringField(json, "net");
		const std::string tls = JsonStringField(json, "tls");
		node.tls = tls == "tls" || tls == "xtls" || net == "ws" || net == "grpc";
		if (node.server.empty() || node.port <= 0)
		{
			error = "Некорректный vmess JSON.";
			return false;
		}
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool ParseShadowsocksUri(const std::string& rest, VpnNode& node, int nodeIndex, std::string& error)
	{
		node.scheme = "ss";
		std::string body = rest;
		std::string fragment;
		const size_t hashPos = body.find('#');
		if (hashPos != std::string::npos)
		{
			fragment = UrlDecode(body.substr(hashPos + 1));
			body = body.substr(0, hashPos);
		}

		std::string userPart;
		std::string hostPort = body;
		const size_t atPos = body.rfind('@');
		if (atPos != std::string::npos)
		{
			userPart = body.substr(0, atPos);
			hostPort = body.substr(atPos + 1);
		}
		else
		{
			std::string decoded;
			if (Base64Decode(body, decoded))
			{
				const size_t decodedAt = decoded.rfind('@');
				if (decodedAt != std::string::npos)
				{
					userPart = decoded.substr(0, decodedAt);
					hostPort = decoded.substr(decodedAt + 1);
				}
			}
		}

		if (!ParseHostPort(hostPort, node.server, node.port, 8388))
		{
			error = "Некорректный ss:// URI.";
			return false;
		}

		node.tls = false;
		if (!fragment.empty())
			node.name = fragment;
		else if (!userPart.empty())
			node.name = userPart;
		AssignNodeIdentity(node, nodeIndex);
		return true;
	}

	bool LooksLikeHtml(const std::string& text)
	{
		const std::string trimmed = Trim(text);
		return StartsWithIgnoreCase(trimmed, "<!doctype")
			|| StartsWithIgnoreCase(trimmed, "<html")
			|| StartsWithIgnoreCase(trimmed, "<head")
			|| trimmed.find("<!doctype html") != std::string::npos;
	}

	bool LooksLikeUnsupportedStub(const std::string& text)
	{
		const std::string lower = ToLower(text);
		return lower.find("не поддерживается") != std::string::npos
			|| lower.find("%d0%bd%d0%b5%20%d0%bf%d0%be%d0%b4%d0%b4%d0%b5%d1%80%d0%b6%d0%b8%d0%b2%d0%b0%d0%b5%d1%82%d1%81%d1%8f") != std::string::npos
			|| lower.find("not supported") != std::string::npos;
	}

	std::string SanitizeHwid(std::string value)
	{
		std::string out;
		out.reserve(value.size());
		for (char ch : value)
		{
			const unsigned char c = static_cast<unsigned char>(ch);
			if ((c >= 'a' && c <= 'z')
				|| (c >= 'A' && c <= 'Z')
				|| (c >= '0' && c <= '9')
				|| c == '='
				|| c == '-')
			{
				out.push_back(static_cast<char>(c));
			}
		}
		if (out.size() > 64)
			out.resize(64);
		return out;
	}

	std::string PreviewText(const std::string& text, size_t maxChars = 96)
	{
		std::string preview = Trim(text);
		for (char& ch : preview)
		{
			if (ch == '\r' || ch == '\n' || ch == '\t')
				ch = ' ';
		}
		if (preview.size() > maxChars)
			preview = preview.substr(0, maxChars) + "...";
		return preview;
	}

	const char* DetectFeedKind(const std::string& text)
	{
		const std::string trimmed = Trim(text);
		if (trimmed.empty())
			return "empty";
		if (LooksLikeHtml(trimmed))
			return "html";
		if (trimmed.front() == '[' || trimmed.front() == '{')
			return "json";
		if (trimmed.find("proxies:") != std::string::npos
			|| trimmed.find("proxy-groups:") != std::string::npos
			|| StartsWithIgnoreCase(trimmed, "mixed-port:")
			|| StartsWithIgnoreCase(trimmed, "port:"))
			return "clash-yaml";
		if (trimmed.find("://") != std::string::npos)
			return "share-links";

		std::string decoded;
		if (Base64Decode(trimmed, decoded))
		{
			const std::string decodedTrim = Trim(decoded);
			if (decodedTrim.find("://") != std::string::npos)
				return "base64-share-links";
			if (!decodedTrim.empty() && (decodedTrim.front() == '{' || decodedTrim.front() == '['))
				return "base64-json";
		}
		return "unknown";
	}

	int CountUriSchemeLines(const std::string& text)
	{
		int count = 0;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			line = Trim(line);
			if (line.find("://") != std::string::npos)
				++count;
		}
		return count;
	}

	std::string MaskHwid(const std::string& hwid)
	{
		if (hwid.size() <= 12)
			return hwid;
		return hwid.substr(0, 8) + "..." + hwid.substr(hwid.size() - 4);
	}

	std::string GetMachineGuid()
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(
				HKEY_LOCAL_MACHINE,
				L"SOFTWARE\\Microsoft\\Cryptography",
				0,
				KEY_READ | KEY_WOW64_64KEY,
				&key) != ERROR_SUCCESS)
		{
			return {};
		}

		wchar_t buffer[128] = {};
		DWORD bufferBytes = sizeof(buffer);
		DWORD type = 0;
		const LONG status = RegQueryValueExW(
			key,
			L"MachineGuid",
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(buffer),
			&bufferBytes);
		RegCloseKey(key);
		if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
			return {};

		char utf8[256] = {};
		WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
		return SanitizeHwid(utf8);
	}

	std::string BuildSystemHwid()
	{
		std::string hwid = GetMachineGuid();
		if (hwid.size() >= 10)
			return hwid;

		wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
		DWORD computerNameLen = MAX_COMPUTERNAME_LENGTH + 1;
		GetComputerNameW(computerName, &computerNameLen);

		DWORD volumeSerial = 0;
		GetVolumeInformationW(L"C:\\", nullptr, 0, &volumeSerial, nullptr, nullptr, nullptr, 0);

		char fallback[96] = {};
		snprintf(
			fallback,
			sizeof fallback,
			"az-%08X-%04X",
			volumeSerial,
			static_cast<unsigned>(computerNameLen * 7919u + 0xA5A5u));
		hwid = SanitizeHwid(fallback);
		while (hwid.size() < 10)
			hwid.push_back('0');
		return hwid;
	}

	std::string BuildSubscriptionHwid()
	{
		{
			AppSettings settings;
			settings.Load();
			const std::string custom = SanitizeHwid(settings.GetCustomHwid());
			// Empty / too short custom -> always fall back to system HWID.
			if (!custom.empty() && custom.size() >= 8)
				return custom;
		}
		return BuildSystemHwid();
	}

	std::string BuildSubscriptionRequestHeaders(std::string& outHwid)
	{
		outHwid = BuildSubscriptionHwid();
		char headers[512] = {};
		snprintf(
			headers,
			sizeof headers,
			// Capybara/Remnawave: text/plain first -> often 1 base64 share-link;
			// application/json + INCY UA -> full Xray JSON array (as in INCY app).
			"Accept: application/json, text/plain, text/yaml, */*\r\n"
			"Accept-Language: en-US,en;q=0.9\r\n"
			"x-hwid: %s\r\n"
			"x-device-os: windows\r\n"
			"x-ver-os: 10.0\r\n"
			"x-device-model: AntiZapret\r\n",
			outHwid.c_str());
		return headers;
	}

	std::string UrlEncode(const std::string& value)
	{
		std::string out;
		out.reserve(value.size() * 3);
		for (unsigned char ch : value)
		{
			if ((ch >= 'a' && ch <= 'z')
				|| (ch >= 'A' && ch <= 'Z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '-' || ch == '_' || ch == '.' || ch == '~')
			{
				out.push_back(static_cast<char>(ch));
			}
			else
			{
				char hex[8] = {};
				snprintf(hex, sizeof hex, "%%%02X", ch);
				out += hex;
			}
		}
		return out;
	}

	bool ExtractBalancedJsonValue(const std::string& text, size_t openIndex, std::string& out, size_t& endIndex)
	{
		if (openIndex >= text.size())
			return false;
		const char open = text[openIndex];
		const char close = open == '{' ? '}' : (open == '[' ? ']' : '\0');
		if (!close)
			return false;

		int depth = 0;
		bool inString = false;
		bool escape = false;
		for (size_t i = openIndex; i < text.size(); ++i)
		{
			const char ch = text[i];
			if (inString)
			{
				if (escape)
					escape = false;
				else if (ch == '\\')
					escape = true;
				else if (ch == '"')
					inString = false;
				continue;
			}
			if (ch == '"')
			{
				inString = true;
				continue;
			}
			if (ch == open)
				++depth;
			else if (ch == close)
			{
				--depth;
				if (depth == 0)
				{
					out = text.substr(openIndex, i - openIndex + 1);
					endIndex = i + 1;
					return true;
				}
			}
		}
		return false;
	}

	std::vector<std::string> ExtractJsonObjectList(const std::string& text)
	{
		std::vector<std::string> objects;
		const size_t arrayStart = text.find('[');
		if (arrayStart == std::string::npos)
			return objects;

		size_t index = arrayStart + 1;
		while (index < text.size())
		{
			index = SkipJsonWhitespace(text, index);
			if (index >= text.size() || text[index] == ']')
				break;
			if (text[index] != '{')
			{
				++index;
				continue;
			}

			std::string object;
			size_t endIndex = 0;
			if (!ExtractBalancedJsonValue(text, index, object, endIndex))
				break;
			objects.push_back(std::move(object));
			index = endIndex;
		}
		return objects;
	}

	std::vector<std::string> ExtractNamedJsonObjectArray(const std::string& json, const char* key)
	{
		std::vector<std::string> objects;
		const std::string pattern = std::string("\"") + key + "\":";
		const size_t keyPos = json.find(pattern);
		if (keyPos == std::string::npos)
			return objects;

		size_t index = SkipJsonWhitespace(json, keyPos + pattern.size());
		if (index >= json.size() || json[index] != '[')
			return objects;

		std::string arrayText;
		size_t endIndex = 0;
		if (!ExtractBalancedJsonValue(json, index, arrayText, endIndex))
			return objects;

		return ExtractJsonObjectList(arrayText);
	}

	bool IsIgnorableXrayOutbound(const std::string& protocol)
	{
		const std::string lower = ToLower(protocol);
		return lower.empty()
			|| lower == "freedom"
			|| lower == "blackhole"
			|| lower == "dns"
			|| lower == "loopback"
			|| lower == "dokodemo-door";
	}

	bool IsProxyXrayProtocol(const std::string& protocol)
	{
		const std::string lower = ToLower(protocol);
		return lower == "vless"
			|| lower == "vmess"
			|| lower == "trojan"
			|| lower == "shadowsocks"
			|| lower == "hysteria"
			|| lower == "hysteria2"
			|| lower == "wireguard";
	}

	std::string BuildVlessShareUri(const std::string& outboundJson, const std::string& remark)
	{
		const std::string uuid = JsonStringField(outboundJson, "id");
		const std::string address = JsonStringField(outboundJson, "address");
		const int port = JsonIntField(outboundJson, "port", 443);
		if (uuid.empty() || address.empty() || port <= 0)
			return {};

		const std::string encryption = JsonStringField(outboundJson, "encryption");
		const std::string flow = JsonStringField(outboundJson, "flow");
		const std::string network = JsonStringField(outboundJson, "network");
		const std::string security = JsonStringField(outboundJson, "security");
		const std::string sni = JsonStringField(outboundJson, "serverName");
		const std::string fp = JsonStringField(outboundJson, "fingerprint");
		const std::string pbk = JsonStringField(outboundJson, "publicKey");
		const std::string sid = JsonStringField(outboundJson, "shortId");
		const std::string spx = JsonStringField(outboundJson, "spiderX");

		std::ostringstream uri;
		uri << "vless://" << uuid << "@" << address << ":" << port
			<< "?encryption=" << UrlEncode(encryption.empty() ? "none" : encryption);
		if (!flow.empty())
			uri << "&flow=" << UrlEncode(flow);
		uri << "&type=" << UrlEncode(network.empty() ? "tcp" : network);
		if (!security.empty())
			uri << "&security=" << UrlEncode(security);
		if (!sni.empty())
			uri << "&sni=" << UrlEncode(sni);
		if (!fp.empty())
			uri << "&fp=" << UrlEncode(fp);
		if (!pbk.empty())
			uri << "&pbk=" << UrlEncode(pbk);
		if (!sid.empty())
			uri << "&sid=" << UrlEncode(sid);
		if (!spx.empty())
			uri << "&spx=" << UrlEncode(spx);
		if (!remark.empty())
			uri << "#" << UrlEncode(remark);
		return uri.str();
	}

	std::string BuildHysteria2ShareUri(const std::string& outboundJson, const std::string& remark)
	{
		const std::string address = JsonStringField(outboundJson, "address");
		const int port = JsonIntField(outboundJson, "port", 443);
		std::string auth = JsonStringField(outboundJson, "auth");
		if (auth.empty())
			auth = JsonStringField(outboundJson, "id");
		if (address.empty() || port <= 0 || auth.empty())
			return {};

		const std::string sni = JsonStringField(outboundJson, "serverName");
		const std::string fp = JsonStringField(outboundJson, "fingerprint");

		std::ostringstream uri;
		uri << "hysteria2://" << UrlEncode(auth) << "@" << address << ":" << port << "?";
		bool first = true;
		auto append = [&](const char* key, const std::string& value)
		{
			if (value.empty())
				return;
			if (!first)
				uri << "&";
			first = false;
			uri << key << "=" << UrlEncode(value);
		};
		append("sni", sni.empty() ? address : sni);
		append("fp", fp);
		if (!remark.empty())
			uri << "#" << UrlEncode(remark);
		return uri.str();
	}

	std::string BuildTrojanShareUri(const std::string& outboundJson, const std::string& remark)
	{
		const std::string password = JsonStringField(outboundJson, "password");
		const std::string address = JsonStringField(outboundJson, "address");
		const int port = JsonIntField(outboundJson, "port", 443);
		if (password.empty() || address.empty() || port <= 0)
			return {};

		const std::string sni = JsonStringField(outboundJson, "serverName");
		const std::string security = JsonStringField(outboundJson, "security");
		std::ostringstream uri;
		uri << "trojan://" << UrlEncode(password) << "@" << address << ":" << port << "?";
		uri << "security=" << UrlEncode(security.empty() ? "tls" : security);
		if (!sni.empty())
			uri << "&sni=" << UrlEncode(sni);
		if (!remark.empty())
			uri << "#" << UrlEncode(remark);
		return uri.str();
	}

	std::string OutboundJsonToShareUri(const std::string& outboundJson, const std::string& remark)
	{
		const std::string protocol = ToLower(JsonStringField(outboundJson, "protocol"));
		if (protocol == "vless")
			return BuildVlessShareUri(outboundJson, remark);
		if (protocol == "hysteria" || protocol == "hysteria2")
			return BuildHysteria2ShareUri(outboundJson, remark);
		if (protocol == "trojan")
			return BuildTrojanShareUri(outboundJson, remark);
		return {};
	}

	std::vector<std::string> ExpandXrayJsonFeed(const std::string& text)
	{
		std::vector<std::string> uris;
		const std::vector<std::string> configs = ExtractJsonObjectList(text);
		LogImportf("Xray JSON: конфигов верхнего уровня: %zu", configs.size());

		for (size_t configIndex = 0; configIndex < configs.size(); ++configIndex)
		{
			const std::string& config = configs[configIndex];
			const std::string remarks = JsonStringField(config, "remarks");
			const std::vector<std::string> outbounds = ExtractNamedJsonObjectArray(config, "outbounds");

			std::vector<std::string> proxyOutbounds;
			std::vector<std::string> taggedProxy;
			for (const std::string& outbound : outbounds)
			{
				const std::string protocol = JsonStringField(outbound, "protocol");
				if (IsIgnorableXrayOutbound(protocol) || !IsProxyXrayProtocol(protocol))
					continue;
				proxyOutbounds.push_back(outbound);
				if (ToLower(JsonStringField(outbound, "tag")) == "proxy")
					taggedProxy.push_back(outbound);
			}

			std::vector<std::string> selected;
			if (!taggedProxy.empty())
				selected = taggedProxy;
			else if (proxyOutbounds.size() == 1)
				selected = proxyOutbounds;

			if (selected.empty())
			{
				if (proxyOutbounds.size() > 1)
				{
					LogImportf(
						"  json[%zu] \"%s\": пропуск balancer (%zu outbounds без tag=proxy)",
						configIndex + 1,
						remarks.c_str(),
						proxyOutbounds.size());
				}
				continue;
			}

			for (const std::string& outbound : selected)
			{
				std::string name = remarks;
				const std::string tag = JsonStringField(outbound, "tag");
				if (name.empty())
					name = tag.empty() ? JsonStringField(outbound, "address") : tag;
				name = SanitizeDisplayName(name);

				const std::string uri = OutboundJsonToShareUri(outbound, name);
				if (uri.empty())
				{
					LogImportf(
						"  json[%zu] \"%s\": не удалось собрать URI (proto=%s)",
						configIndex + 1,
						name.c_str(),
						JsonStringField(outbound, "protocol").c_str());
					continue;
				}
				LogImportf(
					"  json[%zu] -> %s | %s",
					configIndex + 1,
					PreviewText(name, 48).c_str(),
					PreviewText(uri, 100).c_str());
				uris.push_back(uri);
			}
		}

		LogImportf("Xray JSON: собрано share-URI: %zu", uris.size());
		return uris;
	}

	bool FetchSubscriptionText(
		const std::string& url,
		std::string& outText,
		std::string& outError,
		std::string& outProfileTitle,
		long long& outExpireUnix)
	{
		outProfileTitle.clear();
		outExpireUnix = 0;
		// Panels like Capybara/Remnawave:
		// - unknown UA -> HTML install page
		// - no x-hwid -> stub node "Данное приложение не поддерживается"
		// - Happ UA -> often truncated single JSON
		// - INCY UA + Accept application/json -> full multi-server Xray JSON (as in INCY)
		static constexpr const char* kUserAgent = "INCY/2.4.3";
		LogImport("=== Импорт подписки: начало скачивания ===");
		LogImportf("URL: %s", url.c_str());
		LogImportf("User-Agent: %s", kUserAgent);

		HINTERNET internet = InternetOpenA(kUserAgent, INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
		{
			outError = "WinInet недоступен.";
			LogImportf("Ошибка: %s", outError.c_str());
			return false;
		}

		DWORD timeoutMs = 20000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		std::string hwid;
		const std::string headers = BuildSubscriptionRequestHeaders(hwid);
		LogImportf("x-hwid: %s (len=%zu)", MaskHwid(hwid).c_str(), hwid.size());
		LogImport("Заголовки: Accept=application/json,text/plain,text/yaml,*/*; x-device-os=windows; x-device-model=AntiZapret");

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			headers.c_str(),
			static_cast<DWORD>(headers.size()),
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS,
			0);

		if (!request)
		{
			const DWORD err = GetLastError();
			InternetCloseHandle(internet);
			outError = "Не удалось скачать подписку.";
			LogImportf("Ошибка InternetOpenUrl: Win32=%lu — %s", err, outError.c_str());
			return false;
		}

		DWORD statusCode = 0;
		DWORD statusCodeSize = sizeof(statusCode);
		if (HttpQueryInfoA(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &statusCodeSize, nullptr))
			LogImportf("HTTP status: %lu", statusCode);
		else
			LogImport("HTTP status: (не удалось прочитать)");

		char contentType[256] = {};
		DWORD contentTypeSize = sizeof(contentType);
		if (HttpQueryInfoA(request, HTTP_QUERY_CONTENT_TYPE, contentType, &contentTypeSize, nullptr))
			LogImportf("Content-Type: %s", contentType);
		else
			LogImport("Content-Type: (нет)");

		std::string profileTitle = DecodeProfileTitleHeader(QueryHttpHeader(request, "Profile-Title"));
		if (profileTitle.empty())
			profileTitle = DecodeProfileTitleHeader(QueryHttpHeader(request, "profile-title"));
		if (profileTitle.empty())
			profileTitle = ProviderNameFromUrl(url);
		outProfileTitle = profileTitle;
		LogImportf("Profile-Title / провайдер: %s", outProfileTitle.c_str());

		std::string userInfo = QueryHttpHeader(request, "subscription-userinfo");
		if (userInfo.empty())
			userInfo = QueryHttpHeader(request, "Subscription-Userinfo");
		outExpireUnix = ParseSubscriptionExpireUnix(userInfo);
		if (outExpireUnix > 0)
			LogImportf("subscription-userinfo expire: %lld", outExpireUnix);
		else if (!userInfo.empty())
			LogImportf("subscription-userinfo: %s (expire не найден)", PreviewText(userInfo, 120).c_str());

		char buffer[4096];
		DWORD read = 0;
		outText.clear();
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			outText.append(buffer, buffer + read);

		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		LogImportf("Тело ответа: %zu байт", outText.size());
		LogImportf("Превью ответа: %s", PreviewText(outText, 120).c_str());
		LogImportf("Формат ответа: %s", DetectFeedKind(outText));

		if (outText.empty())
		{
			outError = "Подписка пустая.";
			LogImportf("Ошибка: %s", outError.c_str());
			return false;
		}

		if (LooksLikeHtml(outText))
		{
			outError =
				"Сервер вернул HTML-страницу установки вместо списка серверов. "
				"Скопируйте сырую ссылку подписки (иконка «Получить ссылку» на сайте), "
				"а не страницу Happ/INCY.";
			LogImportf("Ошибка: HTML-лендинг вместо feed (%s)", outError.c_str());
			return false;
		}

		std::string decoded;
		const std::string trimmedBody = Trim(outText);
		const char* feedKind = DetectFeedKind(outText);
		if (std::strcmp(feedKind, "json") == 0 || std::strcmp(feedKind, "clash-yaml") == 0)
		{
			LogImportf("Сырой multi-server feed (%s), base64 не применяем.", feedKind);
		}
		else if (Base64Decode(trimmedBody, decoded) && decoded.find("://") != std::string::npos)
		{
			LogImportf(
				"Base64 декодирован: %zu -> %zu байт, URI-строк: %d",
				trimmedBody.size(),
				decoded.size(),
				CountUriSchemeLines(decoded));
			LogImportf("Превью после base64: %s", PreviewText(decoded, 120).c_str());
			outText = decoded;
		}
		else
		{
			LogImportf(
				"Base64 не применён (сырой feed). URI-строк в теле: %d",
				CountUriSchemeLines(outText));
		}

		if (LooksLikeUnsupportedStub(outText))
		{
			outError =
				"Провайдер отклонил клиент (нужен HWID). "
				"Обновите AntiZapret и повторите импорт; "
				"если снова увидите «не поддерживается» — лимит устройств на стороне подписки.";
			LogImportf("Ошибка: stub «не поддерживается» — %s", outError.c_str());
			return false;
		}

		LogImportf(
			"Скачивание OK. Итоговый формат: %s, размер=%zu, URI-строк: %d",
			DetectFeedKind(outText),
			outText.size(),
			CountUriSchemeLines(outText));
		return true;
	}

	std::vector<std::string> SplitLines(const std::string& text)
	{
		std::vector<std::string> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			line = Trim(line);
			if (!line.empty())
				lines.push_back(line);
		}
		return lines;
	}

	bool IsDuplicate(const std::vector<VpnNode>& existing, const VpnNode& candidate)
	{
		for (const VpnNode& node : existing)
		{
			if (!candidate.originalUri.empty() && candidate.originalUri == node.originalUri)
				return true;
			if (candidate.scheme == node.scheme
				&& candidate.server == node.server
				&& candidate.port == node.port
				&& candidate.name == node.name)
			{
				return true;
			}
		}
		return false;
	}
}

void VpnImport::NormalizeNodeDisplay(VpnNode& node)
{
	NormalizeNodeDisplayFields(node);
}

bool VpnImport::ReadClipboardUtf8(std::string& outText)
{
	outText.clear();
	if (!OpenClipboard(nullptr))
		return false;

	HANDLE memory = GetClipboardData(CF_UNICODETEXT);
	if (!memory)
	{
		CloseClipboard();
		return false;
	}

	const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(memory));
	if (!text)
	{
		CloseClipboard();
		return false;
	}

	const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
	if (length > 1)
	{
		outText.resize(static_cast<size_t>(length - 1));
		WideCharToMultiByte(CP_UTF8, 0, text, -1, outText.data(), length, nullptr, nullptr);
	}

	GlobalUnlock(memory);
	CloseClipboard();
	return !outText.empty();
}

bool VpnImport::ParseShareLink(const std::string& line, VpnNode& outNode, int nodeIndex, std::string& outError)
{
	const std::string trimmed = Trim(line);
	if (trimmed.empty())
	{
		outError = "Пустая строка.";
		return false;
	}

	outNode = VpnNode {};
	outNode.originalUri = trimmed;
	outNode.pingMs = -1;
	outNode.speedMbps = -1.f;
	outNode.alive = -1;

	if (StartsWithIgnoreCase(trimmed, "vless://"))
		return ParseStandardUri("vless", trimmed.substr(8), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "vmess://"))
	{
		const std::string payload = trimmed.substr(8);
		if (!payload.empty() && payload.front() == '{')
			return ParseVmessJson(payload, outNode, nodeIndex, outError);

		std::string decoded;
		if (!Base64Decode(payload, decoded))
		{
			outError = "Не удалось декодировать vmess://";
			return false;
		}
		return ParseVmessJson(decoded, outNode, nodeIndex, outError);
	}
	if (StartsWithIgnoreCase(trimmed, "trojan://"))
		return ParseStandardUri("trojan", trimmed.substr(9), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "ss://"))
		return ParseShadowsocksUri(trimmed.substr(5), outNode, nodeIndex, outError);
	if (StartsWithIgnoreCase(trimmed, "hysteria2://") || StartsWithIgnoreCase(trimmed, "hy2://"))
	{
		const size_t prefixLen = StartsWithIgnoreCase(trimmed, "hysteria2://") ? 12 : 5;
		if (!ParseStandardUri("hysteria2", trimmed.substr(prefixLen), outNode, nodeIndex, outError))
			return false;
		outNode.tls = true;
		return true;
	}

	outError = "Неподдерживаемый формат ссылки.";
	return false;
}

VpnImportResult VpnImport::ImportFromText(const std::string& text, int nextNodeIndex)
{
	VpnImportResult result;
	std::vector<std::string> pendingLines = SplitLines(text);

	struct ExpandedItem
	{
		std::string line;
		std::string group;
		std::string sourceUrl;
	};
	std::vector<ExpandedItem> expanded;

	LogImport("=== Импорт VPN: разбор буфера ===");
	LogImportf("Входной текст: %zu байт, непустых строк: %zu", text.size(), pendingLines.size());
	if (!pendingLines.empty())
		LogImportf("Первая строка: %s", PreviewText(pendingLines.front(), 160).c_str());

	for (size_t i = 0; i < pendingLines.size(); ++i)
	{
		const std::string& line = pendingLines[i];
		if (StartsWithIgnoreCase(line, "http://") || StartsWithIgnoreCase(line, "https://"))
		{
			LogImportf("[%zu] URL подписки — скачиваю...", i + 1);
			std::string subscriptionText;
			std::string error;
			std::string profileTitle;
			long long expireUnix = 0;
			if (!FetchSubscriptionText(line, subscriptionText, error, profileTitle, expireUnix))
			{
				LogImportf("[%zu] Скачивание не удалось: %s", i + 1, error.c_str());
				result.errors.push_back(error);
				continue;
			}
			if (expireUnix > 0)
				result.subscriptionExpireUnix = expireUnix;
			if (profileTitle.empty())
				profileTitle = ProviderNameFromUrl(line);
			LogImportf("[%zu] Группа провайдера: %s", i + 1, profileTitle.c_str());

			const std::string trimmedFeed = Trim(subscriptionText);
			if (!trimmedFeed.empty() && trimmedFeed.front() == '[')
			{
				LogImportf("[%zu] Обнаружен Xray JSON feed — конвертация в share-URI...", i + 1);
				const std::vector<std::string> jsonUris = ExpandXrayJsonFeed(subscriptionText);
				if (jsonUris.empty())
				{
					result.errors.push_back("JSON-подписка не содержит поддерживаемых outbound-прокси.");
					LogImportf("[%zu] JSON не дал URI.", i + 1);
					continue;
				}
				for (const std::string& uri : jsonUris)
					expanded.push_back({ uri, profileTitle, line });
				continue;
			}

			const std::vector<std::string> subLines = SplitLines(subscriptionText);
			LogImportf("[%zu] Из подписки строк для разбора: %zu", i + 1, subLines.size());
			for (size_t j = 0; j < subLines.size(); ++j)
			{
				const std::string& subLine = subLines[j];
				if (subLine.find("://") != std::string::npos)
					LogImportf("  feed[%zu]: %s", j + 1, PreviewText(subLine, 140).c_str());
				else
					LogImportf("  feed[%zu]: (не URI) %s", j + 1, PreviewText(subLine, 80).c_str());
				expanded.push_back({ subLine, profileTitle, line });
			}
			continue;
		}

		LogImportf("[%zu] Локальная share-ссылка: %s", i + 1, PreviewText(line, 140).c_str());
		expanded.push_back({ line, {}, {} });
	}

	LogImportf("К разбору share-ссылок: %zu строк", expanded.size());

	int nodeIndex = nextNodeIndex;
	int parseFailed = 0;
	for (size_t i = 0; i < expanded.size(); ++i)
	{
		const ExpandedItem& item = expanded[i];
		VpnNode node;
		std::string error;
		if (!ParseShareLink(item.line, node, nodeIndex, error))
		{
			++parseFailed;
			if (!error.empty())
			{
				LogImportf("Строка %zu: пропуск — %s | %s", i + 1, error.c_str(), PreviewText(item.line, 64).c_str());
				result.errors.push_back(error + " (" + item.line.substr(0, std::min<size_t>(item.line.size(), 48)) + ")");
			}
			continue;
		}

		if (!item.group.empty())
			node.group = item.group;
		if (!item.sourceUrl.empty())
			node.sourceUrl = item.sourceUrl;

		if (IsDuplicate(result.nodes, node))
		{
			++result.duplicatesSkipped;
			LogImportf(
				"Строка %zu: дубликат %s://%s:%d (%s)",
				i + 1,
				node.scheme.c_str(),
				node.server.c_str(),
				node.port,
				node.name.c_str());
			continue;
		}

		LogImportf(
			"Строка %zu: OK → %s://%s:%d tls=%s name=\"%s\" group=\"%s\"",
			i + 1,
			node.scheme.c_str(),
			node.server.c_str(),
			node.port,
			node.tls ? "yes" : "no",
			node.name.c_str(),
			node.group.c_str());
		result.nodes.push_back(std::move(node));
		++nodeIndex;
	}

	LogImportf(
		"=== Импорт VPN: итог — нод=%zu, дубликатов=%d, ошибок_разбора=%d, сообщений=%zu ===",
		result.nodes.size(),
		result.duplicatesSkipped,
		parseFailed,
		result.errors.size());
	if (result.nodes.size() <= 1 && parseFailed == 0 && result.errors.empty())
	{
		LogImport(
			"Замечание: получен только 1 сервер. "
			"У Capybara полный список (как в INCY) приходит в Xray JSON с User-Agent INCY; "
			"формат Happ/v2rayN text/plain часто отдаёт одну share-ссылку.");
	}

	return result;
}

std::string VpnImport::GetSystemHwid()
{
	return BuildSystemHwid();
}
