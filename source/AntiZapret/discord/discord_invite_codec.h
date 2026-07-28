#pragma once

#include <string>

namespace DiscordInviteCodec
{
	// URL-safe base64 without padding.
	std::string Base64UrlEncode(const void* data, size_t size);
	std::string Base64UrlEncode(const std::string& text);
	bool Base64UrlDecode(const std::string& text, std::string& outBytes);

	// zlib-wrapped deflate (RFC 1950), for DecompressionStream('deflate') in open.html.
	std::string ZlibCompress(const std::string& text);
	bool ZlibDecompress(const std::string& compressed, std::string& outText);

	// Build Discord-safe HTTPS invite that carries the VPN share-URI (server config).
	// Picks the shortest form that fits in maxUrlBytes (Discord button limit is 512).
	// Returns empty if nothing fits.
	std::string BuildVpnServerInviteHttps(
		const std::string& openHtmlBaseUrl,
		const std::string& shareUri,
		size_t maxUrlBytes = 512);
}
