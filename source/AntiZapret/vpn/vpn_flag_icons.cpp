#include "vpn/vpn_flag_icons.h"

#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>
#include <wincodec.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
	std::mutex g_mutex;
	std::atomic<bool> g_bulkDownloadStarted { false };

	bool StartsWithIgnoreCase(const std::string& text, const char* prefix)
	{
		if (!prefix)
			return false;
		const size_t n = std::strlen(prefix);
		if (text.size() < n)
			return false;
		for (size_t i = 0; i < n; ++i)
		{
			const unsigned char a = static_cast<unsigned char>(text[i]);
			const unsigned char b = static_cast<unsigned char>(prefix[i]);
			if (std::tolower(a) != std::tolower(b))
				return false;
		}
		return true;
	}

	// ISO 3166-1 alpha-2 (+ XK), same set as flagcdn.com.
	const char* kCountryCodes[] = {
		"AD", "AE", "AF", "AG", "AI", "AL", "AM", "AO", "AQ", "AR", "AS", "AT", "AU", "AW", "AX", "AZ",
		"BA", "BB", "BD", "BE", "BF", "BG", "BH", "BI", "BJ", "BL", "BM", "BN", "BO", "BQ", "BR", "BS", "BT", "BV", "BW", "BY", "BZ",
		"CA", "CC", "CD", "CF", "CG", "CH", "CI", "CK", "CL", "CM", "CN", "CO", "CR", "CU", "CV", "CW", "CX", "CY", "CZ",
		"DE", "DJ", "DK", "DM", "DO", "DZ",
		"EC", "EE", "EG", "EH", "ER", "ES", "ET",
		"FI", "FJ", "FK", "FM", "FO", "FR",
		"GA", "GB", "GD", "GE", "GF", "GG", "GH", "GI", "GL", "GM", "GN", "GP", "GQ", "GR", "GS", "GT", "GU", "GW", "GY",
		"HK", "HM", "HN", "HR", "HT", "HU",
		"ID", "IE", "IL", "IM", "IN", "IO", "IQ", "IR", "IS", "IT",
		"JE", "JM", "JO", "JP",
		"KE", "KG", "KH", "KI", "KM", "KN", "KP", "KR", "KW", "KY", "KZ",
		"LA", "LB", "LC", "LI", "LK", "LR", "LS", "LT", "LU", "LV", "LY",
		"MA", "MC", "MD", "ME", "MF", "MG", "MH", "MK", "ML", "MM", "MN", "MO", "MP", "MQ", "MR", "MS", "MT", "MU", "MV", "MW", "MX", "MY", "MZ",
		"NA", "NC", "NE", "NF", "NG", "NI", "NL", "NO", "NP", "NR", "NU", "NZ",
		"OM",
		"PA", "PE", "PF", "PG", "PH", "PK", "PL", "PM", "PN", "PR", "PS", "PT", "PW", "PY",
		"QA",
		"RE", "RO", "RS", "RU", "RW",
		"SA", "SB", "SC", "SD", "SE", "SG", "SH", "SI", "SJ", "SK", "SL", "SM", "SN", "SO", "SR", "SS", "ST", "SV", "SX", "SY", "SZ",
		"TC", "TD", "TF", "TG", "TH", "TJ", "TK", "TL", "TM", "TN", "TO", "TR", "TT", "TV", "TW", "TZ",
		"UA", "UG", "UM", "US", "UY", "UZ",
		"VA", "VC", "VE", "VG", "VI", "VN", "VU",
		"WF", "WS",
		"XK", "YE", "YT",
		"ZA", "ZM", "ZW",
	};

	std::string NormalizeCountryCode(const std::string& countryCode)
	{
		if (countryCode.size() != 2)
			return {};
		char a = static_cast<char>(std::toupper(static_cast<unsigned char>(countryCode[0])));
		char b = static_cast<char>(std::toupper(static_cast<unsigned char>(countryCode[1])));
		if (a < 'A' || a > 'Z' || b < 'A' || b > 'Z')
			return {};
		return std::string { a, b };
	}

	std::string ToLowerAscii(const std::string& value)
	{
		std::string result = value;
		for (char& ch : result)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return result;
	}

	std::filesystem::path FlagDirectory()
	{
		return std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"flags";
	}

	std::filesystem::path FlagFilePath(const std::string& countryCode)
	{
		return FlagDirectory() / (ToLowerAscii(countryCode) + ".png");
	}

	bool DownloadUrlToFile(const std::string& url, const std::filesystem::path& destination)
	{
		HINTERNET internet = InternetOpenA("AntiZapret", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (!internet)
			return false;

		DWORD timeoutMs = 10000;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeoutMs, sizeof(timeoutMs));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			nullptr,
			0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
			0);
		if (!request)
		{
			InternetCloseHandle(internet);
			return false;
		}

		std::vector<char> bytes;
		char buffer[4096];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			bytes.insert(bytes.end(), buffer, buffer + read);

		InternetCloseHandle(request);
		InternetCloseHandle(internet);

		if (bytes.size() < 64)
			return false;

		std::error_code ec;
		std::filesystem::create_directories(destination.parent_path(), ec);

		std::ofstream output(destination, std::ios::binary | std::ios::trunc);
		if (!output)
			return false;

		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
		return output.good();
	}

	bool CreateSrvFromRgba(
		ID3D11Device* device,
		const std::vector<uint8_t>& rgba,
		UINT width,
		UINT height,
		ID3D11ShaderResourceView** outSrv)
	{
		if (!device || !outSrv || rgba.empty() || width == 0 || height == 0)
			return false;

		D3D11_TEXTURE2D_DESC textureDesc = {};
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.MipLevels = 1;
		textureDesc.ArraySize = 1;
		textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.Usage = D3D11_USAGE_DEFAULT;
		textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initialData = {};
		initialData.pSysMem = rgba.data();
		initialData.SysMemPitch = width * 4;

		ID3D11Texture2D* texture = nullptr;
		if (FAILED(device->CreateTexture2D(&textureDesc, &initialData, &texture)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = textureDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		const HRESULT hr = device->CreateShaderResourceView(texture, &srvDesc, outSrv);
		texture->Release();
		return SUCCEEDED(hr);
	}

	bool LoadImageFile(
		ID3D11Device* device,
		const std::filesystem::path& path,
		ID3D11ShaderResourceView** outSrv,
		UINT* outWidth,
		UINT* outHeight)
	{
		if (!device || !outSrv)
			return false;

		std::ifstream input(path, std::ios::binary);
		if (!input)
			return false;
		input.seekg(0, std::ios::end);
		const std::streamoff fileSize = input.tellg();
		if (fileSize < 32)
			return false;
		input.seekg(0, std::ios::beg);
		std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
		if (!input.read(reinterpret_cast<char*>(fileBytes.data()), fileSize))
			return false;

		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))))
			return false;

		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, fileBytes.size());
		if (!hMem)
		{
			factory->Release();
			return false;
		}
		void* locked = GlobalLock(hMem);
		if (!locked)
		{
			GlobalFree(hMem);
			factory->Release();
			return false;
		}
		std::memcpy(locked, fileBytes.data(), fileBytes.size());
		GlobalUnlock(hMem);

		IStream* stream = nullptr;
		if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream)) || !stream)
		{
			GlobalFree(hMem);
			factory->Release();
			return false;
		}

		IWICBitmapDecoder* decoder = nullptr;
		const HRESULT decodeHr = factory->CreateDecoderFromStream(
			stream,
			nullptr,
			WICDecodeMetadataCacheOnLoad,
			&decoder);
		stream->Release();
		if (FAILED(decodeHr) || !decoder)
		{
			factory->Release();
			return false;
		}

		IWICBitmapFrameDecode* frame = nullptr;
		if (FAILED(decoder->GetFrame(0, &frame)))
		{
			decoder->Release();
			factory->Release();
			return false;
		}

		IWICFormatConverter* converter = nullptr;
		if (FAILED(factory->CreateFormatConverter(&converter)))
		{
			frame->Release();
			decoder->Release();
			factory->Release();
			return false;
		}

		if (FAILED(converter->Initialize(
				frame,
				GUID_WICPixelFormat32bppRGBA,
				WICBitmapDitherTypeNone,
				nullptr,
				0.f,
				WICBitmapPaletteTypeCustom)))
		{
			converter->Release();
			frame->Release();
			decoder->Release();
			factory->Release();
			return false;
		}

		UINT width = 0;
		UINT height = 0;
		converter->GetSize(&width, &height);
		if (width == 0 || height == 0)
		{
			converter->Release();
			frame->Release();
			decoder->Release();
			factory->Release();
			return false;
		}

		std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		const HRESULT copyHr = converter->CopyPixels(
			nullptr,
			width * 4,
			static_cast<UINT>(rgba.size()),
			rgba.data());

		converter->Release();
		frame->Release();
		decoder->Release();
		factory->Release();

		if (FAILED(copyHr))
			return false;

		if (outWidth)
			*outWidth = width;
		if (outHeight)
			*outHeight = height;

		return CreateSrvFromRgba(device, rgba, width, height, outSrv);
	}

	bool ParseUrlParts(const std::string& url, std::string& origin, std::string& host)
	{
		origin.clear();
		host.clear();
		const auto schemePos = url.find("://");
		if (schemePos == std::string::npos)
			return false;
		const size_t hostStart = schemePos + 3;
		size_t hostEnd = url.find_first_of("/?#", hostStart);
		if (hostEnd == std::string::npos)
			hostEnd = url.size();
		if (hostEnd <= hostStart)
			return false;
		host = url.substr(hostStart, hostEnd - hostStart);
		origin = url.substr(0, hostEnd);
		return !host.empty() && !origin.empty();
	}

	std::string GuessBrandFromHost(const std::string& host)
	{
		std::vector<std::string> parts;
		std::string cur;
		for (char ch : host)
		{
			if (ch == '.')
			{
				if (!cur.empty())
					parts.push_back(cur);
				cur.clear();
			}
			else
				cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		}
		if (!cur.empty())
			parts.push_back(cur);
		if (parts.size() < 2)
			return {};

		auto isSkip = [](const std::string& p) {
			return p == "www" || p == "sub" || p == "cdn" || p == "api" || p == "panel" || p == "app"
				|| p == "m" || p == "png" || p == "static" || p == "assets";
		};

		// sub.capybara.support → capybara
		for (size_t i = 0; i + 1 < parts.size(); ++i)
		{
			if (!isSkip(parts[i]) && parts[i].size() >= 3)
				return parts[i];
		}
		return {};
	}

	bool HttpGetText(
		HINTERNET internet,
		const std::string& url,
		const char* extraHeaders,
		std::string& outBody,
		DWORD timeoutMs = 8000)
	{
		outBody.clear();
		if (!internet || url.empty())
			return false;

		DWORD timeout = timeoutMs;
		InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
		InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

		HINTERNET request = InternetOpenUrlA(
			internet,
			url.c_str(),
			extraHeaders,
			extraHeaders ? static_cast<DWORD>(std::strlen(extraHeaders)) : 0,
			INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
			0);
		if (!request)
			return false;

		char buffer[4096];
		DWORD read = 0;
		while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
			outBody.append(buffer, buffer + read);
		InternetCloseHandle(request);
		return !outBody.empty();
	}

	bool HttpGetBinaryOk(HINTERNET internet, const std::string& url, size_t minBytes = 256)
	{
		std::string body;
		if (!HttpGetText(internet, url, "Accept: image/*,*/*\r\n", body, 6000))
			return false;
		if (body.size() < minBytes)
			return false;
		const unsigned char b0 = static_cast<unsigned char>(body[0]);
		const unsigned char b1 = static_cast<unsigned char>(body[1]);
		// PNG / JPEG / GIF / RIFF(webp)
		if (b0 == 0x89 && b1 == 0x50)
			return true;
		if (b0 == 0xFF && b1 == 0xD8)
			return true;
		if (b0 == 'G' && b1 == 'I')
			return true;
		if (b0 == 'R' && b1 == 'I')
			return true;
		if (b0 == '<' && (body.find("<svg") != std::string::npos || body.find("<SVG") != std::string::npos))
			return true;
		return false;
	}

	std::string ExtractJsonStringField(const std::string& json, const char* field)
	{
		if (!field || !*field || json.empty())
			return {};
		const std::string key = std::string("\"") + field + "\"";
		size_t pos = 0;
		while ((pos = json.find(key, pos)) != std::string::npos)
		{
			size_t colon = json.find(':', pos + key.size());
			if (colon == std::string::npos)
				break;
			size_t i = colon + 1;
			while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n'))
				++i;
			if (i >= json.size() || json[i] != '"')
			{
				pos += key.size();
				continue;
			}
			++i;
			std::string value;
			while (i < json.size() && json[i] != '"')
			{
				if (json[i] == '\\' && i + 1 < json.size())
				{
					value.push_back(json[i + 1]);
					i += 2;
					continue;
				}
				value.push_back(json[i++]);
			}
			if (!value.empty() && (value.find("http://") == 0 || value.find("https://") == 0))
				return value;
			pos += key.size();
		}
		return {};
	}

	std::string ResolveSubscriptionPageLogoUrl(const std::string& subscriptionUrl)
	{
		std::string origin;
		std::string host;
		if (!ParseUrlParts(subscriptionUrl, origin, host))
			return {};

		HINTERNET internet = InternetOpenA(
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AntiZapret/1.0",
			INTERNET_OPEN_TYPE_PRECONFIG,
			nullptr,
			nullptr,
			0);
		if (!internet)
			return {};

		// Fast path used by Capybara and similar: https://png.{brand}.press/{brand}.png
		const std::string brand = GuessBrandFromHost(host);
		if (!brand.empty())
		{
			const std::string candidates[] = {
				"https://png." + brand + ".press/" + brand + ".png",
				"https://png." + brand + ".press/logo.png",
				"https://cdn." + brand + ".press/" + brand + ".png",
			};
			for (const std::string& candidate : candidates)
			{
				if (HttpGetBinaryOk(internet, candidate, 1024))
				{
					InternetCloseHandle(internet);
					return candidate;
				}
			}
		}

		// Remnawave sub-page: HTML sets session cookie, then /assets/app-config.json has branding.logoUrl
		const char* htmlHeaders =
			"Accept: text/html,application/xhtml+xml\r\n"
			"Accept-Language: ru-RU,ru;q=0.9\r\n";
		std::string html;
		HttpGetText(internet, subscriptionUrl, htmlHeaders, html, 8000);

		std::string configJson;
		HttpGetText(
			internet,
			origin + "/assets/app-config.json",
			"Accept: application/json\r\n",
			configJson,
			8000);

		std::string logoUrl = ExtractJsonStringField(configJson, "logoUrl");
		if (!logoUrl.empty() && logoUrl.find("docs.rw") == std::string::npos)
		{
			InternetCloseHandle(internet);
			return logoUrl;
		}

		// Absolute image URLs occasionally appear in HTML (SSR / custom themes).
		{
			size_t pos = 0;
			while ((pos = html.find("https://", pos)) != std::string::npos)
			{
				size_t end = pos;
				while (end < html.size())
				{
					const char ch = html[end];
					if (ch == '"' || ch == '\'' || ch == ')' || ch == ' ' || ch == '<' || ch == '>')
						break;
					++end;
				}
				std::string cand = html.substr(pos, end - pos);
				const std::string lower = [&cand]() {
					std::string s = cand;
					for (char& c : s)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					return s;
				}();
				if ((lower.find(".png") != std::string::npos || lower.find(".jpg") != std::string::npos
						|| lower.find(".jpeg") != std::string::npos || lower.find(".webp") != std::string::npos)
					&& lower.find("favicon") == std::string::npos
					&& lower.find("docs.rw") == std::string::npos)
				{
					InternetCloseHandle(internet);
					return cand;
				}
				pos = end;
			}
		}

		InternetCloseHandle(internet);
		return {};
	}
}

VpnFlagIcons& VpnFlagIcons::Instance()
{
	static VpnFlagIcons instance;
	return instance;
}

void VpnFlagIcons::Initialize(ID3D11Device* device)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	m_device = device;
}

void VpnFlagIcons::Shutdown()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	for (auto& entry : m_cache)
	{
		if (entry.second.srv)
			entry.second.srv->Release();
	}
	m_cache.clear();
	for (auto& entry : m_fileCache)
	{
		if (entry.second.srv)
			entry.second.srv->Release();
	}
	m_fileCache.clear();
	m_urlInFlight.clear();
	m_subPageIconUrl.clear();
	m_subPageInFlight.clear();
	m_subPageFailed.clear();
	m_inFlight.clear();
	m_failed.clear();
	m_device = nullptr;
}

void VpnFlagIcons::StartBackgroundDownloadAll()
{
	if (g_bulkDownloadStarted.exchange(true))
		return;

	std::thread([]()
	{
		std::error_code ec;
		std::filesystem::create_directories(FlagDirectory(), ec);

		VpnFlagIcons& icons = VpnFlagIcons::Instance();
		for (const char* code : kCountryCodes)
		{
			const std::string normalized = NormalizeCountryCode(code);
			if (normalized.empty())
				continue;

			const std::filesystem::path path = FlagFilePath(normalized);
			if (std::filesystem::exists(path))
			{
				const auto size = std::filesystem::file_size(path, ec);
				if (!ec && size >= 64)
					continue;
			}

			icons.DownloadFlagFile(normalized);
		}
	}).detach();
}

bool VpnFlagIcons::DownloadFlagFile(const std::string& countryCode) const
{
	const std::string normalized = NormalizeCountryCode(countryCode);
	if (normalized.empty())
		return false;

	const std::string url = "https://flagcdn.com/w40/" + ToLowerAscii(normalized) + ".png";
	return DownloadUrlToFile(url, FlagFilePath(normalized));
}

bool VpnFlagIcons::LoadFlagFromDisk(const std::string& countryCode, FlagEntry& outEntry)
{
	if (!m_device)
		return false;

	const std::filesystem::path path = FlagFilePath(countryCode);
	if (!std::filesystem::exists(path))
		return false;

	ID3D11ShaderResourceView* srv = nullptr;
	UINT width = 0;
	UINT height = 0;
		if (!LoadImageFile(m_device, path, &srv, &width, &height) || !srv)
		return false;

	outEntry.srv = srv;
	outEntry.width = static_cast<int>(width);
	outEntry.height = static_cast<int>(height);
	return true;
}

void VpnFlagIcons::RequestFlag(const std::string& countryCode)
{
	const std::string normalized = NormalizeCountryCode(countryCode);
	if (normalized.empty() || !m_device)
		return;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (m_cache.count(normalized) > 0)
			return;
		if (m_inFlight.count(normalized) > 0)
			return;
		if (m_failed.count(normalized) > 0)
			return;
		if (std::filesystem::exists(FlagFilePath(normalized)))
			return;
		m_inFlight.insert(normalized);
	}

	std::thread([this, normalized]()
	{
		const bool ok = DownloadFlagFile(normalized);
		std::lock_guard<std::mutex> lock(g_mutex);
		m_inFlight.erase(normalized);
		if (!ok)
			m_failed.insert(normalized);
	}).detach();
}

ImTextureID VpnFlagIcons::GetFlagTexture(const std::string& countryCode)
{
	const std::string normalized = NormalizeCountryCode(countryCode);
	if (normalized.empty() || !m_device)
		return 0;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		const auto it = m_cache.find(normalized);
		if (it != m_cache.end() && it->second.srv)
			return reinterpret_cast<ImTextureID>(it->second.srv);
	}

	FlagEntry entry;
	if (!LoadFlagFromDisk(normalized, entry))
	{
		RequestFlag(normalized);
		return 0;
	}

	std::lock_guard<std::mutex> lock(g_mutex);
	m_cache[normalized] = entry;
	return reinterpret_cast<ImTextureID>(entry.srv);
}

ImVec2 VpnFlagIcons::GetFlagDrawSize(const std::string& countryCode, float maxHeight) const
{
	const std::string normalized = NormalizeCountryCode(countryCode);
	if (normalized.empty() || maxHeight <= 0.f)
		return {};

	std::lock_guard<std::mutex> lock(g_mutex);
	const auto it = m_cache.find(normalized);
	if (it == m_cache.end() || !it->second.srv || it->second.width <= 0 || it->second.height <= 0)
		return {};

	const float aspect = static_cast<float>(it->second.width) / static_cast<float>(it->second.height);
	return ImVec2(maxHeight * aspect, maxHeight);
}

ImTextureID VpnFlagIcons::GetFileTexture(const std::filesystem::path& path)
{
	if (!m_device || path.empty())
		return 0;

	std::error_code ec;
	const std::filesystem::path absolute = std::filesystem::weakly_canonical(path, ec);
	const std::filesystem::path& keyPath = absolute.empty() ? path : absolute;
	const std::string key = keyPath.string();
	if (key.empty())
		return 0;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		const auto it = m_fileCache.find(key);
		if (it != m_fileCache.end() && it->second.srv)
			return reinterpret_cast<ImTextureID>(it->second.srv);
	}

	FlagEntry entry;
	UINT width = 0;
	UINT height = 0;
	if (!LoadImageFile(m_device, absolute.empty() ? path : absolute, &entry.srv, &width, &height) || !entry.srv)
		return 0;
	entry.width = static_cast<int>(width);
	entry.height = static_cast<int>(height);

	std::lock_guard<std::mutex> lock(g_mutex);
	m_fileCache[key] = entry;
	return reinterpret_cast<ImTextureID>(entry.srv);
}

ImTextureID VpnFlagIcons::GetSubscriptionPageIcon(const std::string& subscriptionUrl)
{
	if (!m_device || subscriptionUrl.empty())
		return 0;
	if (!(StartsWithIgnoreCase(subscriptionUrl, "http://") || StartsWithIgnoreCase(subscriptionUrl, "https://")))
		return 0;

	std::string iconUrl;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (m_subPageFailed.count(subscriptionUrl) > 0)
			return 0;
		const auto it = m_subPageIconUrl.find(subscriptionUrl);
		if (it != m_subPageIconUrl.end())
			iconUrl = it->second;
	}

	if (!iconUrl.empty())
		return GetUrlTexture(iconUrl);

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (m_subPageInFlight.count(subscriptionUrl) > 0)
			return 0;
		m_subPageInFlight.insert(subscriptionUrl);
	}

	std::thread([this, subscriptionUrl]()
	{
		const std::string resolved = ResolveSubscriptionPageLogoUrl(subscriptionUrl);
		std::lock_guard<std::mutex> lock(g_mutex);
		m_subPageInFlight.erase(subscriptionUrl);
		if (resolved.empty())
			m_subPageFailed.insert(subscriptionUrl);
		else
			m_subPageIconUrl[subscriptionUrl] = resolved;
	}).detach();

	return 0;
}

ImTextureID VpnFlagIcons::GetUrlTexture(const std::string& url)
{
	if (!m_device || url.empty())
		return 0;
	if (!(StartsWithIgnoreCase(url, "http://") || StartsWithIgnoreCase(url, "https://")))
		return 0;

	const std::filesystem::path cacheDir =
		std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"sub_icons";
	std::error_code ec;
	std::filesystem::create_directories(cacheDir, ec);

	unsigned long hash = 2166136261u;
	for (unsigned char ch : url)
	{
		hash ^= ch;
		hash *= 16777619u;
	}
	wchar_t fileName[64] = {};
	swprintf_s(fileName, L"%08lx.img", hash);
	const std::filesystem::path cachePath = cacheDir / fileName;

	if (std::filesystem::is_regular_file(cachePath, ec))
	{
		const ImTextureID existing = GetFileTexture(cachePath);
		if (existing != 0)
			return existing;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (m_urlInFlight.count(url) > 0)
			return 0;
		m_urlInFlight.insert(url);
	}

	std::thread([this, url, cachePath]()
	{
		bool ok = false;
		HINTERNET internet = InternetOpenA("AntiZapret/1.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
		if (internet)
		{
			HINTERNET request = InternetOpenUrlA(
				internet,
				url.c_str(),
				nullptr,
				0,
				INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE,
				0);
			if (request)
			{
				std::vector<char> bytes;
				char buffer[4096];
				DWORD read = 0;
				while (InternetReadFile(request, buffer, sizeof(buffer), &read) && read > 0)
					bytes.insert(bytes.end(), buffer, buffer + read);
				InternetCloseHandle(request);
				if (bytes.size() > 64)
				{
					std::ofstream output(cachePath, std::ios::binary | std::ios::trunc);
					if (output)
					{
						output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
						ok = output.good();
					}
				}
			}
			InternetCloseHandle(internet);
		}

		std::lock_guard<std::mutex> lock(g_mutex);
		m_urlInFlight.erase(url);
		(void)ok;
	}).detach();

	return 0;
}
