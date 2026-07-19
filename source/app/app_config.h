#pragma once

namespace AppConfig
{

inline constexpr const char* kZapretVersionUrl =
	"https://raw.githubusercontent.com/Flowseal/zapret-discord-youtube/main/.service/version.txt";

inline constexpr const char* kZapretUpdateArchiveUrl =
	"https://github.com/Flowseal/zapret-discord-youtube/archive/refs/heads/main.zip";

inline constexpr const char* kZapretRepoExtractedFolderName = "zapret-discord-youtube-main";

inline constexpr const char* kTgProxyReleasesLatestApiUrl =
	"https://api.github.com/repos/Flowseal/tg-ws-proxy/releases/latest";

// Archive for a release tag, e.g. v1.8.1 → .../tags/v1.8.1.zip → folder tg-ws-proxy-1.8.1
inline constexpr const char* kTgProxyTagArchiveUrlPrefix =
	"https://github.com/Flowseal/tg-ws-proxy/archive/refs/tags/";

inline constexpr const char* kTgProxyMainArchiveUrl =
	"https://github.com/Flowseal/tg-ws-proxy/archive/refs/heads/main.zip";

inline constexpr const char* kTgProxyMainExtractedFolderName = "tg-ws-proxy-main";

// VPN modules (mihomo like v2rayN; wintun from official site)
inline constexpr const char* kMihomoReleasesLatestApiUrl =
	"https://api.github.com/repos/MetaCubeX/mihomo/releases/latest";

// {0} = tag e.g. v1.19.0 → mihomo-windows-amd64-v1-v1.19.0.zip
inline constexpr const char* kMihomoWin64ZipUrlTemplate =
	"https://github.com/MetaCubeX/mihomo/releases/download/%s/mihomo-windows-amd64-v1-%s.zip";

inline constexpr const char* kWintunHomeUrl = "https://www.wintun.net/";

// {0} = version e.g. 0.14.1
inline constexpr const char* kWintunZipUrlTemplate =
	"https://www.wintun.net/builds/wintun-%s.zip";

}  // namespace AppConfig
