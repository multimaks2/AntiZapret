#include "discord/discord_invite_codec.h"

#include "app/protocol_handler.h"

#include "miniz.h"

#include <vector>

namespace DiscordInviteCodec
{
	std::string Base64UrlEncode(const void* data, size_t size)
	{
		static const char kTable[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
		const auto* bytes = static_cast<const unsigned char*>(data);
		std::string out;
		out.reserve(((size + 2) / 3) * 4);
		for (size_t i = 0; i < size; i += 3)
		{
			const unsigned int b0 = bytes[i];
			const unsigned int b1 = (i + 1 < size) ? bytes[i + 1] : 0u;
			const unsigned int b2 = (i + 2 < size) ? bytes[i + 2] : 0u;
			const unsigned int triple = (b0 << 16) | (b1 << 8) | b2;
			out.push_back(kTable[(triple >> 18) & 63]);
			out.push_back(kTable[(triple >> 12) & 63]);
			if (i + 1 < size)
				out.push_back(kTable[(triple >> 6) & 63]);
			if (i + 2 < size)
				out.push_back(kTable[triple & 63]);
		}
		return out;
	}

	std::string Base64UrlEncode(const std::string& text)
	{
		return Base64UrlEncode(text.data(), text.size());
	}

	bool Base64UrlDecode(const std::string& text, std::string& outBytes)
	{
		outBytes.clear();
		if (text.empty())
			return false;

		auto decodeChar = [](char c) -> int {
			if (c >= 'A' && c <= 'Z')
				return c - 'A';
			if (c >= 'a' && c <= 'z')
				return c - 'a' + 26;
			if (c >= '0' && c <= '9')
				return c - '0' + 52;
			if (c == '-' || c == '+')
				return 62;
			if (c == '_' || c == '/')
				return 63;
			return -1;
		};

		std::string clean;
		clean.reserve(text.size());
		for (char ch : text)
		{
			if (ch == '=' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t')
				continue;
			if (decodeChar(ch) < 0)
				return false;
			clean.push_back(ch);
		}
		if (clean.empty())
			return false;

		outBytes.reserve((clean.size() * 3) / 4);
		for (size_t i = 0; i < clean.size(); )
		{
			const int c0 = decodeChar(clean[i++]);
			const int c1 = (i < clean.size()) ? decodeChar(clean[i++]) : 0;
			const int c2 = (i < clean.size()) ? decodeChar(clean[i++]) : -1;
			const int c3 = (i < clean.size()) ? decodeChar(clean[i++]) : -1;
			if (c0 < 0 || c1 < 0)
				return false;

			outBytes.push_back(static_cast<char>((c0 << 2) | (c1 >> 4)));
			if (c2 >= 0)
				outBytes.push_back(static_cast<char>(((c1 & 15) << 4) | (c2 >> 2)));
			if (c3 >= 0)
				outBytes.push_back(static_cast<char>(((c2 & 3) << 6) | c3));
		}
		return true;
	}

	std::string ZlibCompress(const std::string& text)
	{
		if (text.empty())
			return {};
		mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(text.size()));
		std::vector<unsigned char> dest(static_cast<size_t>(bound));
		mz_ulong destLen = bound;
		const int rc = mz_compress2(
			dest.data(),
			&destLen,
			reinterpret_cast<const unsigned char*>(text.data()),
			static_cast<mz_ulong>(text.size()),
			MZ_BEST_COMPRESSION);
		if (rc != MZ_OK || destLen == 0)
			return {};
		return std::string(reinterpret_cast<const char*>(dest.data()), static_cast<size_t>(destLen));
	}

	bool ZlibDecompress(const std::string& compressed, std::string& outText)
	{
		outText.clear();
		if (compressed.empty())
			return false;

		mz_ulong destLen = static_cast<mz_ulong>(compressed.size() * 8 + 64);
		for (int attempt = 0; attempt < 6; ++attempt)
		{
			std::vector<unsigned char> dest(static_cast<size_t>(destLen));
			mz_ulong len = destLen;
			const int rc = mz_uncompress(
				dest.data(),
				&len,
				reinterpret_cast<const unsigned char*>(compressed.data()),
				static_cast<mz_ulong>(compressed.size()));
			if (rc == MZ_OK)
			{
				outText.assign(reinterpret_cast<const char*>(dest.data()), static_cast<size_t>(len));
				return true;
			}
			if (rc != MZ_BUF_ERROR)
				return false;
			destLen *= 2;
		}
		return false;
	}

	std::string BuildVpnServerInviteHttps(
		const std::string& openHtmlBaseUrl,
		const std::string& shareUri,
		size_t maxUrlBytes)
	{
		if (openHtmlBaseUrl.empty() || shareUri.empty())
			return {};

		std::vector<std::string> candidates;
		candidates.push_back(openHtmlBaseUrl + "?t=vpn&u=" + ProtocolHandler::UrlEncode(shareUri));
		candidates.push_back(openHtmlBaseUrl + "?t=vpn&b=" + Base64UrlEncode(shareUri));

		const std::string zlib = ZlibCompress(shareUri);
		if (!zlib.empty())
			candidates.push_back(openHtmlBaseUrl + "?t=vpn&z=" + Base64UrlEncode(zlib));

		std::string best;
		for (const std::string& candidate : candidates)
		{
			if (candidate.size() > maxUrlBytes)
				continue;
			if (best.empty() || candidate.size() < best.size())
				best = candidate;
		}
		return best;
	}
}
