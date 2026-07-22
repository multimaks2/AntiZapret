#include "vpn/vpn_flag_icons.h"

#include "zapret/zapret_paths.h"

#include <Windows.h>
#include <WinInet.h>
#include <wincodec.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
	std::mutex g_mutex;
	std::atomic<bool> g_bulkDownloadStarted { false };

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

	bool LoadPngFile(
		ID3D11Device* device,
		const std::filesystem::path& path,
		ID3D11ShaderResourceView** outSrv,
		UINT* outWidth,
		UINT* outHeight)
	{
		if (!device || !outSrv)
			return false;

		IWICImagingFactory* factory = nullptr;
		if (FAILED(CoCreateInstance(
				CLSID_WICImagingFactory,
				nullptr,
				CLSCTX_INPROC_SERVER,
				IID_PPV_ARGS(&factory))))
			return false;

		IWICBitmapDecoder* decoder = nullptr;
		const HRESULT decodeHr = factory->CreateDecoderFromFilename(
			path.c_str(),
			nullptr,
			GENERIC_READ,
			WICDecodeMetadataCacheOnLoad,
			&decoder);
		if (FAILED(decodeHr))
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
	if (!LoadPngFile(m_device, path, &srv, &width, &height) || !srv)
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
