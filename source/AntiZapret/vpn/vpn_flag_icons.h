#pragma once

#include "imgui.h"

#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct ID3D11ShaderResourceView;

class VpnFlagIcons
{
public:
	static VpnFlagIcons& Instance();

	void StartBackgroundDownloadAll();

	void Initialize(ID3D11Device* device);
	void Shutdown();
	void RequestFlag(const std::string& countryCode);
	ImTextureID GetFlagTexture(const std::string& countryCode);
	ImVec2 GetFlagDrawSize(const std::string& countryCode, float maxHeight) const;

private:
	struct FlagEntry
	{
		ID3D11ShaderResourceView* srv = nullptr;
		int width = 0;
		int height = 0;
	};

	VpnFlagIcons() = default;

	bool LoadFlagFromDisk(const std::string& countryCode, FlagEntry& outEntry);
	bool DownloadFlagFile(const std::string& countryCode) const;

	ID3D11Device* m_device = nullptr;
	std::unordered_map<std::string, FlagEntry> m_cache;
	std::unordered_set<std::string> m_inFlight;
	std::unordered_set<std::string> m_failed;
};
