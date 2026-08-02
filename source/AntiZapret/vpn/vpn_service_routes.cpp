#include "vpn/vpn_service_routes.h"

#include "vpn/vpn_adult_sites.h"
#include "vpn/vpn_service_fallback_domains.h"
#include "zapret/zapret_paths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
	std::mutex g_routesMutex;

	bool DescriptionHasExe(const char* description)
	{
		if (!description || !description[0])
			return false;
		return std::strstr(description, ".exe") != nullptr || std::strstr(description, ".EXE") != nullptr;
	}

	void AssignCatalogEntry(const ServiceCatalogEntry& item, ServiceRouteEntry& entry)
	{
		entry.id = item.id ? item.id : "";
		entry.icon = item.icon;
		entry.brandIcon = item.brandIcon;
		entry.name = item.name ? item.name : "";
		entry.description = item.description ? item.description : "";
		entry.region = item.region;
		entry.section = item.section;
		entry.kind = VpnServiceRoutes::InferKind(item);
		entry.custom = false;
		entry.enabled = true;
		entry.mode = ServiceRouteMode::None;
	}

	void AppendUniqueToken(std::vector<std::string>& out, std::string value)
	{
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
			value.erase(value.begin());
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
			value.pop_back();
		if (value.empty())
			return;
		for (const std::string& existing : out)
		{
			if (existing == value)
				return;
		}
		out.push_back(std::move(value));
	}

	bool LooksLikeDomainToken(const std::string& token)
	{
		if (token.find('.') == std::string::npos)
			return false;
		std::string lower = token;
		for (char& ch : lower)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".exe") == 0)
			return false;
		for (unsigned char ch : token)
		{
			if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_')
				continue;
			return false;
		}
		return true;
	}

	bool LooksLikeProcessToken(const std::string& token)
	{
		if (token.size() < 5)
			return false;
		std::string lower = token;
		for (char& ch : lower)
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return lower.compare(lower.size() - 4, 4, ".exe") == 0;
	}

	void ParseTargetCsv(
		const std::string& csv,
		ServiceCatalogKind kind,
		std::vector<std::string>& outDomains,
		std::vector<std::string>& outProcesses)
	{
		size_t start = 0;
		while (start <= csv.size())
		{
			const size_t comma = csv.find(',', start);
			std::string token = csv.substr(
				start,
				comma == std::string::npos ? std::string::npos : comma - start);
			while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
				token.erase(token.begin());
			while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
				token.pop_back();

			if (!token.empty())
			{
				if (LooksLikeProcessToken(token))
					AppendUniqueToken(outProcesses, token);
				else if (LooksLikeDomainToken(token))
					AppendUniqueToken(outDomains, token);
				else if (kind == ServiceCatalogKind::App)
				{
					std::string exe = token;
					std::string lower = exe;
					for (char& ch : lower)
						ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".exe") != 0)
						exe += ".exe";
					AppendUniqueToken(outProcesses, std::move(exe));
				}
			}

			if (comma == std::string::npos)
				break;
			start = comma + 1;
		}
	}

	std::string SanitizeCustomIdPart(std::string value)
	{
		std::string out;
		out.reserve(value.size());
		for (unsigned char ch : value)
		{
			if (std::isalnum(ch))
				out.push_back(static_cast<char>(std::tolower(ch)));
			else if (ch == ' ' || ch == '-' || ch == '_')
				out.push_back('_');
		}
		while (!out.empty() && out.back() == '_')
			out.pop_back();
		if (out.size() > 24)
			out.resize(24);
		if (out.empty())
			out = "item";
		return out;
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

	int ParseInt(const std::string& value, int fallback)
	{
		if (value.empty())
			return fallback;
		return std::atoi(value.c_str());
	}

	bool ParseBool(const std::string& value, bool fallback)
	{
		if (value.empty())
			return fallback;
		if (value == "1" || value == "true" || value == "True" || value == "yes")
			return true;
		if (value == "0" || value == "false" || value == "False" || value == "no")
			return false;
		return fallback;
	}

	std::filesystem::path RoutesFile()
	{
		return std::filesystem::path(ZapretPaths::GetCacheDirectory()) / L"service-routes.ini";
	}

	const ServiceCatalogEntry kCatalog[] = {
		// --- Зарубежные: соцсети и мессенджеры ---
		{ "youtube", 0xf167, true, "YouTube", "YouTube, ytimg, ggpht, googleapis", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "discord", 0xf392, true, "Discord", "Discord, discord.media, AyuGram, Vesktop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "telegram", 0xf2c6, true, "Telegram", "Telegram, t.me, telegram-cdn, AyuGram, Unigram", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "instagram", 0xf16d, true, "Instagram", "Instagram, CDN Instagram", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "facebook", 0xf09a, true, "Facebook", "Facebook, fbcdn, fburl", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "twitter", 0xe61b, true, "Twitter / X", "Twitter, X, t.co, twimg", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "tiktok", 0xe07b, true, "TikTok", "tiktok.com, tiktokcdn.com, musically", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "whatsapp", 0xf232, true, "WhatsApp", "whatsapp.com, web.whatsapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "linkedin", 0xf08c, true, "LinkedIn", "linkedin.com, licdn.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "reddit", 0xf1a1, true, "Reddit", "reddit.com, redditstatic.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },

		// --- Зарубежные: стриминг и музыка ---
		{ "twitch", 0xf1e8, true, "Twitch", "Twitch, twitchcdn, jtvnw", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "soundcloud", 0xf1be, true, "SoundCloud", "SoundCloud, sndcdn", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "spotify", 0xf1bc, true, "Spotify", "spotify.com, scdn.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "netflix", 0xE714, false, "Netflix", "netflix.com, nflxvideo.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },

		// --- Зарубежные: браузеры ---
		{ "chrome", 0xf268, true, "Google Chrome", "chrome.exe, chromium.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "firefox", 0xe007, true, "Firefox", "firefox.exe, Waterfox, LibreWolf", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "edge", 0xf282, true, "Microsoft Edge", "msedge.exe, msedgewebview2.exe, identity_helper.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "opera", 0xf26a, true, "Opera", "opera.exe, opera_browser.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "opera_gx", 0xf26a, true, "Opera GX", "opera.exe, operagx.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "brave", 0xe63c, true, "Brave", "brave.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "vivaldi", 0xf27d, true, "Vivaldi", "vivaldi.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "tor_browser", 0xf519, true, "Tor Browser", "firefox.exe, tor.exe, torbrowser", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "chromium", 0xf268, true, "Chromium", "chrome.exe, chromium.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "librewolf", 0xe007, true, "LibreWolf", "librewolf.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "waterfox", 0xe007, true, "Waterfox", "waterfox.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "floorp", 0xe007, true, "Floorp", "floorp.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "zen_browser", 0xe007, true, "Zen Browser", "zen.exe, zen-browser", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "thorium", 0xf268, true, "Thorium", "thorium.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "ungoogled", 0xf268, true, "Ungoogled Chromium", "chrome.exe, ungoogled", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "mullvad_browser", 0xf519, true, "Mullvad Browser", "mullvadbrowser.exe, firefox.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "arc", 0xf268, true, "Arc", "Arc.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "duckduckgo", 0xf519, true, "DuckDuckGo Browser", "duckduckgo.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "pale_moon", 0xe007, true, "Pale Moon", "palemoon.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "maxthon", 0xf268, true, "Maxthon", "Maxthon.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "centbrowser", 0xf268, true, "Cent Browser", "chrome.exe, centbrowser", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "iron", 0xf268, true, "SRWare Iron", "chrome.exe, iron.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "comodo", 0xf268, true, "Comodo Dragon", "dragon.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "avg_browser", 0xf268, true, "AVG Secure Browser", "AVGBrowser.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "avast_browser", 0xf268, true, "Avast Secure Browser", "AvastBrowser.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "sidekick", 0xf268, true, "Sidekick", "sidekick.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "wavebox", 0xf268, true, "Wavebox", "Wavebox.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "slimjet", 0xf268, true, "Slimjet", "slimjet.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },

		// --- Зарубежные: AI (ограничены / недоступны в РФ) ---
		{ "chatgpt", 0xE8F1, false, "ChatGPT / OpenAI", "chatgpt.com, openai.com, api.openai.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "claude", 0xE8F1, false, "Claude / Anthropic", "claude.ai, anthropic.com, api.anthropic.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "gemini", 0xf1a0, true, "Google Gemini", "gemini.google.com, bard.google.com, generativelanguage.googleapis.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "copilot", 0xf3ca, true, "Microsoft Copilot", "copilot.microsoft.com, bing.com/chat", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "grok", 0xE8F1, false, "Grok AI (xAI / Илон Маск)", "grok.x.com, x.ai, grok.com, console.x.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "perplexity", 0xE8F1, false, "Perplexity", "perplexity.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "midjourney", 0xE8F1, false, "Midjourney", "midjourney.com, discord Midjourney", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "character_ai", 0xE8F1, false, "Character.AI", "character.ai, beta.character.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "groq", 0xE8F1, false, "Groq", "groq.com, console.groq.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "elevenlabs", 0xE8F1, false, "ElevenLabs", "elevenlabs.io, api.elevenlabs.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "runway", 0xE8F1, false, "Runway", "runwayml.com, app.runwayml.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "leonardo", 0xE8F1, false, "Leonardo.AI", "leonardo.ai, cloud.leonardo.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "huggingface", 0xE8F1, false, "Hugging Face", "huggingface.co, hf.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "poe", 0xf2c4, true, "Poe", "poe.com, quora.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "notion_ai", 0xE8F1, false, "Notion AI", "notion.so, notion.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },

		// --- Зарубежные: для разработчиков ---
		{ "github", 0xf09b, true, "GitHub", "github.com, githubusercontent.com, ghcr.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gitlab", 0xf296, true, "GitLab", "gitlab.com, gitlab.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "bitbucket", 0xf171, true, "Bitbucket", "bitbucket.org, atlassian.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "stackoverflow", 0xf16c, true, "Stack Overflow", "stackoverflow.com, stackexchange.com, sstatic.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "npm", 0xf3d4, true, "npm", "npmjs.com, registry.npmjs.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "pypi", 0xf3e2, true, "PyPI", "pypi.org, pythonhosted.org, files.pythonhosted.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "crates_io", 0xe07a, true, "crates.io", "crates.io, static.crates.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "golang", 0xe40f, true, "Go / pkg.go.dev", "go.dev, pkg.go.dev, proxy.golang.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "nuget", 0xf3ca, true, "NuGet", "nuget.org, nuget.azure.cn", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "maven", 0xE943, false, "Maven Central", "maven.org, repo1.maven.org, central.sonatype.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "dockerhub", 0xf395, true, "Docker Hub", "docker.com, docker.io, hub.docker.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "hashicorp", 0xE943, false, "HashiCorp", "hashicorp.com, terraform.io, releases.hashicorp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "aws", 0xf375, true, "Amazon AWS", "amazonaws.com, aws.amazon.com, cloudfront.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gcp", 0xf1a0, true, "Google Cloud", "cloud.google.com, googleapis.com, gcr.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "azure", 0xf3ca, true, "Microsoft Azure", "azure.com, azureedge.net, visualstudio.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "cloudflare", 0xe07d, true, "Cloudflare", "cloudflare.com, workers.dev, r2.dev", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "vercel", 0xE950, false, "Vercel", "vercel.com, vercel.app, now.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "netlify", 0xE950, false, "Netlify", "netlify.com, netlify.app", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "digitalocean", 0xf391, true, "DigitalOcean", "digitalocean.com, digitaloceanspaces.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "heroku", 0xE950, false, "Heroku", "heroku.com, herokuapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "render", 0xE950, false, "Render", "render.com, onrender.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "railway", 0xE950, false, "Railway", "railway.app, railway.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "supabase", 0xE950, false, "Supabase", "supabase.com, supabase.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "firebase", 0xf1a0, true, "Firebase", "firebase.google.com, firebaseio.com, firebaseapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "mongodb_atlas", 0xE950, false, "MongoDB Atlas", "mongodb.com, mongodb.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "planetscale", 0xE950, false, "PlanetScale", "planetscale.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "replit", 0xE943, false, "Replit", "replit.com, repl.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "codepen", 0xf1cb, true, "CodePen", "codepen.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "jsfiddle", 0xf1cc, true, "JSFiddle", "jsfiddle.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "codesandbox", 0xE943, false, "CodeSandbox", "codesandbox.io, csb.app", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "stackblitz", 0xE943, false, "StackBlitz", "stackblitz.com, webcontainer.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "cursor_ide", 0xE943, false, "Cursor", "cursor.com, cursor.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "jetbrains", 0xE943, false, "JetBrains", "jetbrains.com, jb.gg", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "vscode", 0xf3ca, true, "VS Code Marketplace", "code.visualstudio.com, marketplace.visualstudio.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "sourceforge", 0xE8F1, false, "SourceForge", "sourceforge.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gist", 0xf09b, true, "GitHub Gist", "gist.github.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gitkraken", 0xf3a6, true, "GitKraken / Axosoft", "gitkraken.com, axosoft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "postman", 0xE943, false, "Postman", "postman.com, getpostman.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "sentry", 0xE950, false, "Sentry", "sentry.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "datadog", 0xE950, false, "Datadog", "datadoghq.com, datadoghq.eu", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "grafana", 0xE950, false, "Grafana", "grafana.com, grafana.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "openai_api", 0xE8F1, false, "OpenAI API / platform", "platform.openai.com, api.openai.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },

		// --- Зарубежные: игровые лаунчеры ---
		{ "steam", 0xf1b6, true, "Steam", "steam.exe, steampowered.com, steamcommunity.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "epic", 0xE7FC, false, "Epic Games", "EpicGamesLauncher.exe, epicgames.com, unrealengine.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "battlenet", 0xf835, true, "Battle.net", "Battle.net.exe, blizzard.com, battle.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "ea_app", 0xE7FC, false, "EA App", "EADesktop.exe, ea.com, origin.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "ubisoft", 0xE7FC, false, "Ubisoft Connect", "upc.exe, ubisoft.com, ubi.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "rockstar", 0xE7FC, false, "Rockstar Launcher", "Launcher.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "gog", 0xE7FC, false, "GOG Galaxy", "GalaxyClient.exe, gog.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "riot", 0xE7FC, false, "Riot Client", "RiotClientServices.exe, riotgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "xbox", 0xf412, true, "Xbox / Microsoft Store", "XboxPcApp.exe, xbox.com, xboxlive.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "itch", 0xf83a, true, "itch.io", "itch.exe, itch.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },

		// --- Зарубежные: онлайн / сетевые игры ---
		{ "sigame", 0xE7FC, false, "SiGame", "SIGame.exe, vladimirkhil.com, своя игра", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "genshin", 0xE7FC, false, "Genshin Impact", "GenshinImpact.exe, hoyoverse.com, mihoyo.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "honkai", 0xE7FC, false, "Honkai: Star Rail", "StarRail.exe, hoyoverse.com, mihoyo.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "zzz", 0xE7FC, false, "Zenless Zone Zero", "ZenlessZoneZero.exe, hoyoverse.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "valorant", 0xE7FC, false, "Valorant", "VALORANT.exe, riotgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "league", 0xE7FC, false, "League of Legends", "LeagueClient.exe, leagueoflegends.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fortnite", 0xE7FC, false, "Fortnite", "FortniteClient, epicgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "roblox", 0xE7FC, false, "Roblox", "RobloxPlayerBeta.exe, roblox.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "minecraft", 0xE7FC, false, "Minecraft", "Minecraft.exe, minecraft.net, mojang.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "faceit", 0xE7FC, false, "FACEIT", "faceit.exe, faceit.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "apex", 0xE7FC, false, "Apex Legends", "r5apex.exe, ea.com/games/apex", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warzone", 0xE7FC, false, "Call of Duty / Warzone", "cod.exe, callofduty.com, activision.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wot", 0xE7FC, false, "World of Tanks / Warships", "WorldOfTanks.exe, wargaming.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "cs2", 0xE7FC, false, "Counter-Strike 2", "cs2.exe, counter-strike.net, steam", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "dota2", 0xE7FC, false, "Dota 2", "dota2.exe, dota2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "pubg", 0xE7FC, false, "PUBG", "TslGame.exe, pubg.com, krafton.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "overwatch", 0xE7FC, false, "Overwatch 2", "Overwatch.exe, overwatch.blizzard.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wow", 0xE7FC, false, "World of Warcraft", "Wow.exe, worldofwarcraft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "diablo", 0xE7FC, false, "Diablo IV", "Diablo IV.exe, diablo.blizzard.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "hearthstone", 0xE7FC, false, "Hearthstone", "Hearthstone.exe, playhearthstone.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "path_of_exile", 0xE7FC, false, "Path of Exile", "PathOfExile.exe, pathofexile.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "destiny2", 0xE7FC, false, "Destiny 2", "destiny2.exe, bungie.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "gta5", 0xE7FC, false, "GTA Online", "GTA5.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rdr2", 0xE7FC, false, "Red Dead Online", "RDR2.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "helldivers2", 0xE7FC, false, "HELLDIVERS™ 2", "helldivers2.exe, arrowheadgamestudios.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "palworld", 0xE7FC, false, "Palworld", "Palworld-Win64-Shipping.exe, palworldgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rust", 0xE7FC, false, "Rust", "RustClient.exe, rust.facepunch.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "ark", 0xE7FC, false, "ARK: Survival", "ShooterGame.exe, playark.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "tarkov", 0xE7FC, false, "Escape from Tarkov", "EscapeFromTarkov.exe, escapefromtarkov.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warthunder", 0xE7FC, false, "War Thunder", "aces.exe, warthunder.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wot_heat", 0xE7FC, false, "World of Tanks: HEAT", "WotHEAT.exe, WorldOfTanksHEAT.exe, wargaming.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warships", 0xE7FC, false, "World of Warships", "WorldOfWarships.exe, worldofwarships.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warframe", 0xE7FC, false, "Warframe", "Warframe.x64.exe, warframe.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "r6siege", 0xE7FC, false, "Rainbow Six Siege", "RainbowSix.exe, ubisoft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "sea_of_thieves", 0xE7FC, false, "Sea of Thieves", "SoTGame.exe, seaofthieves.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "valheim", 0xE7FC, false, "Valheim", "valheim.exe, valheimgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "deep_rock", 0xE7FC, false, "Deep Rock Galactic", "FSD-Win64-Shipping.exe, deeprockgalactic.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "dayz", 0xE7FC, false, "DayZ", "DayZ_x64.exe, dayz.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "arma_reforger", 0xE7FC, false, "Arma Reforger", "ArmaReforgerSteam.exe, bohemia.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "hell_let_loose", 0xE7FC, false, "Hell Let Loose", "HLL-Win64-Shipping.exe, hellletloose.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "ready_or_not", 0xE7FC, false, "Ready or Not", "ReadyOrNotSteam.exe, readyornotgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "forza_horizon5", 0xE7FC, false, "Forza Horizon 5", "ForzaHorizon5.exe, forza.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "forza_horizon6", 0xE7FC, false, "Forza Horizon 6", "ForzaHorizon6.exe, forza.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "forza_motorsport", 0xE7FC, false, "Forza Motorsport", "ForzaMotorsport.exe, forza.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "euro_truck", 0xE7FC, false, "Euro Truck Simulator 2", "eurotrucks2.exe, scssoft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "american_truck", 0xE7FC, false, "American Truck Simulator", "amtrucks.exe, scssoft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "beamng", 0xE7FC, false, "BeamNG.drive", "BeamNG.drive.exe, beamng.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "gmod", 0xE7FC, false, "Garry's Mod", "gmod.exe, garrysmod.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "l4d2", 0xE7FC, false, "Left 4 Dead 2", "left4dead2.exe, valvesoftware.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "elden_ring", 0xE7FC, false, "ELDEN RING", "eldenring.exe, bandainamcoent.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "bg3", 0xE7FC, false, "Baldur's Gate 3", "bg3.exe, bg3_dx11.exe, larian.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wuthering_waves", 0xE7FC, false, "Wuthering Waves", "Client-Win64-Shipping.exe, wutheringwaves.kurogame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "black_desert", 0xE7FC, false, "Black Desert", "BlackDesert64.exe, blackdesertfoundry.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "guild_wars2", 0xE7FC, false, "Guild Wars 2", "Gw2-64.exe, guildwars2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "no_mans_sky", 0xE7FC, false, "No Man's Sky", "NMS.exe, nomanssky.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "satisfactory", 0xE7FC, false, "Satisfactory", "FactoryGameSteam.exe, satisfactorygame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "factorio", 0xE7FC, false, "Factorio", "factorio.exe, factorio.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "project_zomboid", 0xE7FC, false, "Project Zomboid", "ProjectZomboid64.exe, projectzomboid.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "seven_days", 0xE7FC, false, "7 Days to Die", "7DaysToDie.exe, 7daystodie.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "scum", 0xE7FC, false, "SCUM", "SCUM.exe, scumgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "vrising", 0xE7FC, false, "V Rising", "VRising.exe, playvrising.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "enshrouded", 0xE7FC, false, "Enshrouded", "enshrouded.exe, enshrouded.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "gray_zone", 0xE7FC, false, "Gray Zone Warfare", "GZWClientSteam.exe, grayzonewarfare.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "arena_breakout", 0xE7FC, false, "Arena Breakout: Infinite", "ABIGame.exe, arenabreakoutinfinite.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "first_descendant", 0xE7FC, false, "The First Descendant", "TFD.exe, nexon.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "naraka", 0xE7FC, false, "NARAKA: BLADEPOINT", "NarakaBladepoint.exe, narakathegame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "starcraft2", 0xE7FC, false, "StarCraft II", "SC2_x64.exe, starcraft2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "aoe4", 0xE7FC, false, "Age of Empires IV", "RelicCardinal.exe, ageofempires.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wallpaper_engine", 0xE7FC, false, "Wallpaper Engine", "wallpaper64.exe, wallpaperengine.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fc25", 0xE7FC, false, "EA FC Online / FIFA", "FC25.exe, FC26.exe, ea.com/games/ea-sports-fc", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "nba2k", 0xE7FC, false, "NBA 2K Online", "NBA2K.exe, nba.com, 2k.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rocket_league", 0xE7FC, false, "Rocket League", "RocketLeague.exe, rocketleague.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "dbd", 0xE7FC, false, "Dead by Daylight", "DeadByDaylight.exe, deadbydaylight.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "among_us", 0xE7FC, false, "Among Us", "Among Us.exe, innersloth.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "ffxiv", 0xE7FC, false, "Final Fantasy XIV", "ffxiv_dx11.exe, finalfantasyxiv.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "lost_ark", 0xE7FC, false, "Lost Ark", "LOSTARK.exe, playlostark.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "new_world", 0xE7FC, false, "New World", "NewWorld.exe, newworld.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "deadlock", 0xE7FC, false, "Deadlock", "deadlock.exe, playdeadlock.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "tf2", 0xE7FC, false, "Team Fortress 2", "hl2.exe, teamfortress.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fall_guys", 0xE7FC, false, "Fall Guys", "FallGuys_client.exe, fallguys.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "brawlhalla", 0xE7FC, false, "Brawlhalla", "Brawlhalla.exe, brawlhalla.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "smite", 0xE7FC, false, "Smite", "Smite.exe, smitegame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "osu", 0xE7FC, false, "osu!", "osu!.exe, osu.ppy.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "vrchat", 0xE7FC, false, "VRChat", "VRChat.exe, vrchat.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "phasmophobia", 0xE7FC, false, "Phasmophobia", "Phasmophobia.exe, kineticgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warface", 0xE7FC, false, "Warface", "Warface.exe, warface.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "crossout", 0xE7FC, false, "Crossout", "Crossout.exe, crossout.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "albion", 0xE7FC, false, "Albion Online", "Albion-Online.exe, albiononline.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "eve", 0xE7FC, false, "EVE Online", "exefile.exe, eveonline.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "hunt", 0xE7FC, false, "Hunt: Showdown", "HuntGame.exe, huntshowdown.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },

		// --- Зарубежные: новинки Steam (онлайн / сетевые) ---
		{ "marvel_rivals", 0xE7FC, false, "Marvel Rivals", "MarvelRivals.exe, marvelrivals.com, NetEase", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "delta_force", 0xE7FC, false, "Delta Force", "DeltaForce.exe, deltaforcegame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "peak", 0xE7FC, false, "PEAK", "PEAK.exe, steam PEAK coop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "repo", 0xE7FC, false, "R.E.P.O.", "REPO.exe, steam R.E.P.O. coop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "arc_raiders", 0xE7FC, false, "ARC Raiders", "ARCRaiders.exe, arcraiders.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "schedule_i", 0xE7FC, false, "Schedule I", "Schedule I.exe, steam Schedule I", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "poe2", 0xE7FC, false, "Path of Exile 2", "PathOfExile_x64.exe, pathofexile2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "throne_liberty", 0xE7FC, false, "Throne and Liberty", "TL.exe, playthroneandliberty.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "once_human", 0xE7FC, false, "Once Human", "ONCE_HUMAN.exe, oncehuman.game", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "fragpunk", 0xE7FC, false, "FragPunk", "FragPunk.exe, fragpunk.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "the_finals", 0xE7FC, false, "THE FINALS", "Discovery.exe, reachthefinals.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "battlefield", 0xE7FC, false, "Battlefield", "bf6.exe / bf2042.exe, ea.com/games/battlefield", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "abiotic_factor", 0xE7FC, false, "Abiotic Factor", "AbioticFactor.exe, abioticfactor.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "content_warning", 0xE7FC, false, "Content Warning", "Content Warning.exe, landfall.se", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "lethal_company", 0xE7FC, false, "Lethal Company", "Lethal Company.exe, zeekerss.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "party_animals", 0xE7FC, false, "Party Animals", "PartyAnimals.exe, partyanimalsgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "spiritvale", 0xE7FC, false, "SpiritVale", "SpiritVale.exe, action MMO Steam", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "ragnarok_tnw", 0xE7FC, false, "Ragnarok: The New World", "RagnarokTNW.exe, Gravity MMO", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "zerospace", 0xE7FC, false, "ZeroSpace", "ZeroSpace.exe, RTS/RPG multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "soulbound", 0xE7FC, false, "Soulbound: Online", "Soulbound.exe, SpiderWare MMO", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "carnival_hunt", 0xE7FC, false, "Carnival Hunt", "CarnivalHunt.exe, asymmetric horror", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "codename_cure2", 0xE7FC, false, "Codename CURE II", "CodenameCURE2.exe, co-op zombie", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "gun_x_gunner", 0xE7FC, false, "GUN X GUNNER", "GunXGunner.exe, tactical multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "ouch_cargo", 0xE7FC, false, "Ouch Cargo", "OuchCargo.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "funnel_runners", 0xE7FC, false, "Funnel Runners", "FunnelRunners.exe, co-op survival", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "galley_mound", 0xE7FC, false, "The Mound: Omen of Cthulhu", "TheMound.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "bodycam_onrecord", 0xE7FC, false, "Bodycam Onrecord", "BodycamOnrecord.exe, PvP / co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "halo_evolved", 0xE7FC, false, "Halo: Campaign Evolved", "HaloCampaignEvolved.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "nightreign", 0xE7FC, false, "ELDEN RING Nightreign", "Nightreign.exe, co-op FromSoftware", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "monster_hunter_wilds", 0xE7FC, false, "Monster Hunter Wilds", "MonsterHunterWilds.exe, Capcom online hunt", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "umamusume", 0xE7FC, false, "Umamusume: Pretty Derby", "umamusume.exe, Cygames online", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "meccha_chameleon", 0xE7FC, false, "Meccha Chameleon", "MecchaChameleon.exe, party Steam hit", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "difficult", 0xE7FC, false, "diffiCULT", "diffiCULT.exe, social deduction multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "warena", 0xE7FC, false, "Warena", "Warena.exe, real-time card battler", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },

		// --- Зарубежные: торрент клиенты ---
		{ "qbittorrent", 0xE896, false, "qBittorrent", "qbittorrent.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "utorrent", 0xE896, false, "µTorrent / uTorrent", "uTorrent.exe, utorrent.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "bittorrent", 0xE896, false, "BitTorrent", "BitTorrent.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "transmission", 0xE896, false, "Transmission", "transmission-qt.exe, transmission-gtk.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "deluge", 0xE896, false, "Deluge", "deluge.exe, deluge-gtk.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "tixati", 0xE896, false, "Tixati", "tixati.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "biglybt", 0xE896, false, "BiglyBT", "BiglyBT.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "vuze", 0xE896, false, "Vuze / Azureus", "Azureus.exe, vuze.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "picotorrent", 0xE896, false, "PicoTorrent", "PicoTorrent.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "frostwire", 0xE896, false, "FrostWire", "FrostWire.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "motrix", 0xE896, false, "Motrix", "Motrix.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "fdm", 0xE896, false, "Free Download Manager", "fdm.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "aria2", 0xE896, false, "aria2", "aria2c.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "bitcomet", 0xE896, false, "BitComet", "BitComet.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "bitlord", 0xE896, false, "BitLord", "BitLord.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "halite", 0xE896, false, "Halite", "Halite.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "tribler", 0xE896, false, "Tribler", "tribler.exe", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },

		// --- Вне категорий (конец списка приложений) ---
		{ "windows", 0xf17a, true, "Windows", "Системные процессы Windows, svchost.exe, System", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStandalone },

		// --- Российские: браузеры ---
		{ "yandex_browser", 0xf413, true, "Яндекс Браузер", "browser.exe, yandexbrowser.exe, yandex.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },
		{ "atom_browser", 0xE774, false, "Atom (VK)", "atom.exe, браузер VK / Mail.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },
		{ "sputnik_browser", 0xE774, false, "Спутник", "sputnik.exe, browser.sputnik.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },
		{ "nichrome", 0xE774, false, "Nichrome", "nichrome.exe", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },

		// --- Российские: экосистемы ---
		{ "yandex", 0xf413, true, "Яндекс", "Диск, Маркет, Карты, Почта, yandex.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "vk", 0xf189, true, "VK", "VK.exe, vk.com, vk.ru, vkplay.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "mailru", 0xE715, false, "Mail.ru", "mail.ru, cloud.mail.ru, ICQ New", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "ok", 0xf263, true, "Одноклассники", "ok.ru, odnoklassniki.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "max", 0xE8BD, false, "MAX", "max.ru, мессенджер VK", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },

		// --- Российские: банки ---
		{ "sberbank", 0xE825, false, "Сбербанк", "Sberbank.exe, online.sberbank.ru, sber.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "tinkoff", 0xE825, false, "Т-Банк", "Tinkoff.exe, tbank.ru, tinkoff.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "vtb", 0xE825, false, "ВТБ", "VTB.exe, vtb.ru, online.vtb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "alfabank", 0xE825, false, "Альфа-Банк", "Alfa-Bank.exe, alfabank.ru, alfa.me", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "yoomoney", 0xE825, false, "ЮMoney", "yoomoney.ru, yookassa.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "gazprombank", 0xE825, false, "Газпромбанк", "gazprombank.ru, gpb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "sovcombank", 0xE825, false, "Совкомбанк", "sovcombank.ru, halvacard.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "raiffeisen", 0xE825, false, "Райффайзен", "raiffeisen.ru, online.raiffeisen.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "rosbank", 0xE825, false, "Росбанк", "rosbank.ru, online.rosbank.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "mtsbank", 0xE825, false, "МТС Банк", "mtsbank.ru, mtsdengi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },

		// --- Российские: госуслуги ---
		{ "gosuslugi", 0xE72E, false, "Госуслуги", "gosuslugi.ru, esia.gosuslugi.ru, lk.gosuslugi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "nalog", 0xE72E, false, "ФНС / Налоги", "nalog.gov.ru, lkfl2.nalog.ru, gov.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "mos_ru", 0xE72E, false, "Москва / mos.ru", "mos.ru, my.mos.ru, uslugi.mos.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "pochta_ru", 0xE715, false, "Почта России", "pochta.ru, tracking.pochta.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },

		// --- Российские: маркетплейсы ---
		{ "wildberries", 0xE719, false, "Wildberries", "wildberries.ru, wb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "ozon", 0xE719, false, "Ozon", "ozon.ru, ozon.app, Ozon Bank", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "avito", 0xE719, false, "Авито", "avito.ru, avito.st", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "megamarket", 0xE719, false, "СберМегаМаркет", "megamarket.ru, sbermegamarket.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "dns_shop", 0xE719, false, "DNS", "dns-shop.ru, dns-shop.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "mvideo", 0xE719, false, "М.Видео / Эльдорадо", "mvideo.ru, eldorado.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "citilink", 0xE719, false, "Ситилинк", "citilink.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "lamoda", 0xE719, false, "Lamoda", "lamoda.ru, lamoda.co", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "goldapple", 0xE719, false, "Золотое Яблоко", "goldapple.ru, gacdn.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "vkusvill", 0xE719, false, "ВкусВилл", "vkusvill.ru, online.vkusvill.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "pyaterochka", 0xE719, false, "Пятёрочка / X5", "5ka.ru, perekrestok.ru, x5.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },

		// --- Российские: доставка ---
		{ "yandex_go", 0xf413, true, "Яндекс Go", "taxi.yandex.ru, eda.yandex, доставка", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "samokat", 0xE804, false, "Самокат", "samokat.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "cdek", 0xE804, false, "СДЭК", "cdek.ru, lk.cdek.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "delivery_club", 0xE804, false, "Delivery Club", "delivery-club.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },

		// --- Российские: телеком ---
		{ "mts", 0xE717, false, "МТС", "mts.ru, mymts.ru, lk.mts.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "megafon", 0xE717, false, "МегаФон", "megafon.ru, lk.megafon.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "beeline", 0xE717, false, "Билайн", "beeline.ru, my.beeline.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "tele2", 0xE717, false, "Tele2", "tele2.ru, t2.ru, my.tele2.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "rostelecom", 0xE717, false, "Ростелеком", "rt.ru, rostelecom.ru, lk.rt.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },

		// --- Российские: стриминг ---
		{ "rutube", 0xE714, false, "Rutube", "rutube.ru, static.rutube.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "okko", 0xE714, false, "Okko", "okko.tv, api.okko.tv", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "ivi", 0xE714, false, "IVI", "ivi.ru, ivi.tv, api.ivi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "kion", 0xE714, false, "KION", "kion.ru, api.kion.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "wink", 0xE714, false, "Wink", "wink.ru, api.wink.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "start", 0xE714, false, "START", "start.ru, start.video", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "premier", 0xE714, false, "Premier", "premier.one, api.premier.one", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "zvuk", 0xE8D6, false, "Звук", "zvuk.com, sberaudio.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "yandex_music", 0xf001, true, "Яндекс Музыка", "Яндекс Музыка.exe, music.yandex.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "yandex_music_store", 0xf001, true, "Яндекс Музыка (Store)", "Y.Music.exe, music.yandex.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },

		// --- Российские: транспорт ---
		{ "gis2", 0xE821, false, "2ГИС", "2gis.ru, 2gis.com, навигация", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "rzd", 0xE806, false, "РЖД", "rzd.ru, ticket.rzd.ru, pass.rzd.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "aeroflot", 0xE806, false, "Аэрофлот", "aeroflot.ru, booking.aeroflot.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "s7", 0xE806, false, "S7 Airlines", "s7.ru, api.s7.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "tutu", 0xE806, false, "Tutu.ru", "tutu.ru, api.tutu.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "aviasales", 0xE806, false, "Aviasales", "aviasales.ru, aviasales.com", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },

		// --- Российские: недвижимость и авто ---
		{ "cian", 0xE821, false, "ЦИАН", "cian.ru, api.cian.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "domclick", 0xE821, false, "Домклик", "domclick.ru, api.domclick.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "auto_ru", 0xE804, false, "Auto.ru", "auto.ru, api.auto.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "drom", 0xE804, false, "Drom.ru", "drom.ru, auto.drom.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },

		// --- Российские: работа, медицина, безопасность ---
		{ "hh", 0xE734, false, "HeadHunter", "hh.ru, headhunter.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "superjob", 0xE734, false, "SuperJob", "superjob.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "habr", 0xE734, false, "Хабр", "habr.com, career.habr.com", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "sberhealth", 0xE72E, false, "СберЗдоровье", "sberhealth.ru, doctoronline.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "invitro", 0xE72E, false, "Invitro", "invitro.ru, lk.invitro.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "apteka", 0xE72E, false, "Apteka.ru", "apteka.ru, eapteka.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "kaspersky", 0xE72E, false, "Kaspersky", "avp.exe, ksde.exe, kaspersky.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "drweb", 0xE72E, false, "Dr.Web", "drweb.exe, drweb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },

		// --- Российские: прочее ---
		{ "2ip", 0xE774, false, "2ip.ru", "2ip.ru, www.2ip.ru, api.2ip.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianMisc },
		{ "whoer", 0xE774, false, "Whoer", "whoer.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianMisc },
		{ "ipleak", 0xE774, false, "IPLeak", "ipleak.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianMisc },
	};
}

std::string VpnServiceRoutes::GeositeNameForService(const std::string& serviceId)
{
	static const std::unordered_map<std::string, const char*> kOverrides = {
		{ "youtube", "google" },
		{ "chrome", "google" },
		{ "brave", "google" },
		{ "edge", "microsoft" },
		{ "twitter", "x" },
		{ "chatgpt", "openai" },
		{ "claude", "anthropic" },
		{ "gemini", "google" },
		{ "copilot", "microsoft" },
		{ "grok", "x" },
		{ "notion_ai", "notion" },
		{ "character_ai", "character" },
		{ "ea_app", "ea" },
		{ "battlenet", "blizzard" },
		{ "league", "riot" },
		{ "valorant", "riot" },
		{ "fortnite", "epicgames" },
		{ "epic", "epicgames" },
		{ "genshin", "mihoyo" },
		{ "honkai", "mihoyo" },
		{ "zzz", "mihoyo" },
		{ "xbox", "microsoft" },
		{ "minecraft", "microsoft" },
		{ "warzone", "activision" },
		{ "wot", "wargaming" },
		{ "sigame", "vladimirkhil" },
		{ "yandex_browser", "yandex" },
		{ "mos_ru", "mos" },
		{ "pochta_ru", "pochta" },
		{ "dns_shop", "dns-shop" },
		{ "auto_ru", "auto" },
		{ "gis2", "2gis" },
		{ "delivery_club", "delivery-club" },
		{ "megamarket", "sbermegamarket" },
		{ "yandex_go", "yandex-go" },
		{ "sberhealth", "sberhealth" },
	};

	const auto it = kOverrides.find(serviceId);
	if (it != kOverrides.end())
		return it->second;

	return serviceId;
}

void VpnServiceRoutes::CollectRequiredGeosites(
	const std::vector<ServiceRouteEntry>& routes,
	std::vector<std::string>& outNames)
{
	std::unordered_set<std::string> unique;
	for (const ServiceRouteEntry& route : routes)
	{
		if (!route.enabled
			|| route.mode == ServiceRouteMode::None
			|| route.mode == ServiceRouteMode::Antizapret)
			continue;

		if (PreferFallbackOnly(route.id))
			continue;

		const std::string geositeName = GeositeNameForService(route.id);
		if (geositeName.empty())
			continue;

		if (unique.insert(geositeName).second)
			outNames.push_back(geositeName);
	}
}

void VpnServiceRoutes::CollectFallbackDomains(const std::string& serviceId, std::vector<std::string>& outDomains)
{
	if (VpnAdultSites::IsAdultServiceId(serviceId))
	{
		VpnAdultSites::CollectFallbackDomains(serviceId, outDomains);
		return;
	}
	VpnServiceFallbackDomains::Collect(serviceId, outDomains);
}

void VpnServiceRoutes::CollectRouteTargets(
	const ServiceRouteEntry& service,
	std::vector<std::string>& outDomains,
	std::vector<std::string>& outProcesses)
{
	outDomains.clear();
	outProcesses.clear();

	if (service.custom)
	{
		ParseTargetCsv(service.description, service.kind, outDomains, outProcesses);
		return;
	}

	CollectFallbackDomains(service.id, outDomains);

	// Каталожные приложения: PROCESS-NAME из description (*.exe), плюс домены из fallback.
	// Kind::Site — чтобы путь вроде ea.com/games/... не превратился в фейковый .exe.
	if (service.kind == ServiceCatalogKind::App)
	{
		std::vector<std::string> unusedDomains;
		ParseTargetCsv(service.description, ServiceCatalogKind::Site, unusedDomains, outProcesses);
	}
}

bool VpnServiceRoutes::IsAdultSection(ServiceCatalogSection section)
{
	return section == ServiceCatalogSection::ForeignAdult;
}

bool VpnServiceRoutes::HasFallbackDomains(const std::string& serviceId)
{
	std::vector<std::string> domains;
	CollectFallbackDomains(serviceId, domains);
	return !domains.empty();
}

bool VpnServiceRoutes::PreferFallbackOnly(const std::string& serviceId)
{
	if (serviceId.rfind("custom_", 0) == 0)
		return true;
	if (VpnAdultSites::IsAdultServiceId(serviceId))
		return true;

	static const char* kFallbackOnly[] = {
		"2ip", "whoer", "ipleak", "sigame", "genshin", "honkai", "zzz", "faceit", "itch",
		"midjourney", "character_ai", "leonardo", "runway", "elevenlabs",
		"groq", "poe", "notion_ai", "perplexity", "grok",
		"github", "gitlab", "bitbucket", "stackoverflow", "npm", "pypi", "crates_io",
		"golang", "nuget", "maven", "dockerhub", "hashicorp",
		"aws", "gcp", "azure", "cloudflare", "vercel", "netlify", "digitalocean",
		"heroku", "render", "railway", "supabase", "firebase", "mongodb_atlas", "planetscale",
		"replit", "codepen", "jsfiddle", "codesandbox", "stackblitz",
		"cursor_ide", "jetbrains", "vscode", "sourceforge", "gist", "gitkraken",
		"postman", "sentry", "datadog", "grafana", "openai_api",
		"cs2", "dota2", "pubg", "overwatch", "wow", "diablo", "hearthstone",
		"path_of_exile", "destiny2", "gta5", "rdr2",
		"helldivers2", "palworld", "rust", "ark", "tarkov", "warthunder",
		"wot_heat", "warships", "warframe", "r6siege", "sea_of_thieves", "valheim",
		"deep_rock", "dayz", "arma_reforger", "hell_let_loose", "ready_or_not",
		"forza_horizon5", "forza_horizon6", "forza_motorsport",
		"euro_truck", "american_truck", "beamng", "gmod", "l4d2",
		"elden_ring", "bg3", "wuthering_waves", "black_desert", "guild_wars2",
		"no_mans_sky", "satisfactory", "factorio", "project_zomboid", "seven_days",
		"scum", "vrising", "enshrouded", "gray_zone", "arena_breakout",
		"first_descendant", "naraka", "starcraft2", "aoe4", "wallpaper_engine",
		"fc25", "nba2k", "rocket_league", "dbd", "among_us",
		"ffxiv", "lost_ark", "new_world", "deadlock", "tf2",
		"fall_guys", "brawlhalla", "smite", "osu", "vrchat", "phasmophobia",
		"warface", "crossout", "albion", "eve", "hunt",
		"valorant", "league", "fortnite", "roblox", "minecraft", "apex", "warzone", "wot",
		"marvel_rivals", "delta_force", "peak", "repo", "arc_raiders", "schedule_i", "poe2",
		"throne_liberty", "once_human", "fragpunk", "the_finals", "battlefield", "abiotic_factor",
		"content_warning", "lethal_company", "party_animals", "spiritvale", "ragnarok_tnw",
		"zerospace", "soulbound", "carnival_hunt", "codename_cure2", "gun_x_gunner", "ouch_cargo",
		"funnel_runners", "galley_mound", "bodycam_onrecord", "halo_evolved", "nightreign",
		"monster_hunter_wilds", "umamusume", "meccha_chameleon", "difficult", "warena",
		"yandex_music", "yandex_music_store",
		"qbittorrent", "utorrent", "bittorrent", "transmission", "deluge", "tixati",
		"biglybt", "vuze", "picotorrent", "frostwire", "motrix", "fdm", "aria2",
		"bitcomet", "bitlord", "halite", "tribler", "windows",
	};
	for (const char* id : kFallbackOnly)
	{
		if (serviceId == id)
			return true;
	}
	return false;
}

bool VpnServiceRoutes::NeedsVoiceRouting(const std::string& serviceId)
{
	return serviceId == "discord";
}

const char* VpnServiceRoutes::ModeLabel(ServiceRouteMode mode)
{
	switch (mode)
	{
	case ServiceRouteMode::Antizapret: return "Напрямую";
	case ServiceRouteMode::VpnTunnel:
	case ServiceRouteMode::VpnProxy: return "VPN";
	case ServiceRouteMode::None:
	default: return "Напрямую";
	}
}

ServiceCatalogKind VpnServiceRoutes::InferKind(const ServiceCatalogEntry& item)
{
	ServiceRouteEntry tmp;
	tmp.id = item.id ? item.id : "";
	tmp.description = item.description ? item.description : "";
	tmp.section = item.section;
	tmp.custom = false;
	return InferKind(tmp);
}

ServiceCatalogKind VpnServiceRoutes::InferKind(const ServiceRouteEntry& entry)
{
	if (entry.custom)
		return entry.kind;

	const bool hasExe = entry.description.find(".exe") != std::string::npos
		|| entry.description.find(".EXE") != std::string::npos;

	switch (entry.section)
	{
	case ServiceCatalogSection::ForeignBrowser:
	case ServiceCatalogSection::ForeignLaunchers:
	case ServiceCatalogSection::ForeignGames:
	case ServiceCatalogSection::ForeignSteamNew:
	case ServiceCatalogSection::ForeignMisc:
	case ServiceCatalogSection::ForeignStandalone:
	case ServiceCatalogSection::RussianBrowser:
	case ServiceCatalogSection::CustomApps:
		return ServiceCatalogKind::App;
	case ServiceCatalogSection::CustomSites:
	case ServiceCatalogSection::ForeignAdult:
		return ServiceCatalogKind::Site;
	case ServiceCatalogSection::ForeignSocial:
		if (entry.id == "discord" || entry.id == "telegram" || entry.id == "whatsapp")
			return ServiceCatalogKind::App;
		return ServiceCatalogKind::Site;
	case ServiceCatalogSection::RussianEco:
		if (entry.id == "vk" || entry.id == "max")
			return ServiceCatalogKind::App;
		return ServiceCatalogKind::Site;
	case ServiceCatalogSection::RussianStreaming:
		if (entry.id == "yandex_music" || entry.id == "yandex_music_store" || hasExe)
			return ServiceCatalogKind::App;
		return ServiceCatalogKind::Site;
	case ServiceCatalogSection::RussianMisc:
		if (hasExe)
			return ServiceCatalogKind::App;
		return ServiceCatalogKind::Site;
	case ServiceCatalogSection::RussianWorkHealth:
		if (entry.id == "kaspersky" || entry.id == "drweb" || hasExe)
			return ServiceCatalogKind::App;
		return ServiceCatalogKind::Site;
	default:
		break;
	}

	if (hasExe)
		return ServiceCatalogKind::App;
	return ServiceCatalogKind::Site;
}

const char* VpnServiceRoutes::SectionLabel(ServiceCatalogSection section)
{
	switch (section)
	{
	case ServiceCatalogSection::ForeignTools: return "Утилиты";
	case ServiceCatalogSection::ForeignSocial: return "Соцсети и мессенджеры";
	case ServiceCatalogSection::ForeignStreaming: return "Стриминг и музыка";
	case ServiceCatalogSection::ForeignBrowser: return "Браузеры";
	case ServiceCatalogSection::ForeignAI: return "AI (ограничены в РФ)";
	case ServiceCatalogSection::ForeignDev: return "Для разработчиков";
	case ServiceCatalogSection::ForeignLaunchers: return "Игровые лаунчеры";
	case ServiceCatalogSection::ForeignGames: return "Онлайн / сетевые игры";
	case ServiceCatalogSection::ForeignSteamNew: return "Новинки Steam";
	case ServiceCatalogSection::ForeignAdult: return "18+ сайты";
	case ServiceCatalogSection::ForeignMisc: return "Торрент клиенты";
	case ServiceCatalogSection::ForeignStandalone: return "";
	case ServiceCatalogSection::RussianBrowser: return "Браузеры";
	case ServiceCatalogSection::RussianEco: return "Экосистемы и мессенджеры";
	case ServiceCatalogSection::RussianBank: return "Банки и платежи";
	case ServiceCatalogSection::RussianGov: return "Госуслуги";
	case ServiceCatalogSection::RussianShop: return "Маркетплейсы и магазины";
	case ServiceCatalogSection::RussianDelivery: return "Доставка и такси";
	case ServiceCatalogSection::RussianTelecom: return "Телеком";
	case ServiceCatalogSection::RussianStreaming: return "Стриминг и видео";
	case ServiceCatalogSection::RussianTravel: return "Транспорт и путешествия";
	case ServiceCatalogSection::RussianProperty: return "Недвижимость и авто";
	case ServiceCatalogSection::RussianWorkHealth: return "Работа, медицина, безопасность";
	case ServiceCatalogSection::RussianMisc: return "Проверка IP";
	case ServiceCatalogSection::CustomApps: return "Мною добавленное";
	case ServiceCatalogSection::CustomSites: return "Мною добавленное";
	default: return "Сервисы";
	}
}

uint32_t VpnServiceRoutes::SectionIcon(ServiceCatalogSection section)
{
	// Segoe MDL2 Assets
	switch (section)
	{
	case ServiceCatalogSection::ForeignTools: return 0xE90F; // Repair
	case ServiceCatalogSection::ForeignSocial: return 0xE8BD; // Contact
	case ServiceCatalogSection::ForeignStreaming: return 0xE714; // Video
	case ServiceCatalogSection::ForeignBrowser: return 0xE774; // Globe
	case ServiceCatalogSection::ForeignAI: return 0xE945; // Processing / AI-ish
	case ServiceCatalogSection::ForeignDev: return 0xE943; // Code
	case ServiceCatalogSection::ForeignLaunchers: return 0xE7FC; // Game
	case ServiceCatalogSection::ForeignGames: return 0xE7FC;
	case ServiceCatalogSection::ForeignSteamNew: return 0xE7FC;
	case ServiceCatalogSection::ForeignAdult: return 0xE8A5; // FavoriteStar / adult marker fallback
	case ServiceCatalogSection::ForeignMisc: return 0xE896; // Download
	case ServiceCatalogSection::ForeignStandalone: return 0xf17a;
	case ServiceCatalogSection::RussianBrowser: return 0xE774;
	case ServiceCatalogSection::RussianEco: return 0xE8BD;
	case ServiceCatalogSection::RussianBank: return 0xE825; // Money
	case ServiceCatalogSection::RussianGov: return 0xE730; // CityNext / building
	case ServiceCatalogSection::RussianShop: return 0xE719; // Shop
	case ServiceCatalogSection::RussianDelivery: return 0xE7EC; // DeliveryTruck-ish / MapPin
	case ServiceCatalogSection::RussianTelecom: return 0xE704; // CellPhone
	case ServiceCatalogSection::RussianStreaming: return 0xE714;
	case ServiceCatalogSection::RussianTravel: return 0xE709; // Airplane
	case ServiceCatalogSection::RussianProperty: return 0xE80F; // Home
	case ServiceCatalogSection::RussianWorkHealth: return 0xE734; // Work
	case ServiceCatalogSection::RussianMisc: return 0xE774;
	case ServiceCatalogSection::CustomApps: return 0xE71D; // AppIcon
	case ServiceCatalogSection::CustomSites: return 0xE774;
	default: return 0xE8A5;
	}
}

const std::vector<ServiceCatalogEntry>& VpnServiceRoutes::Catalog()
{
	static const std::vector<ServiceCatalogEntry> catalog = []
	{
		std::vector<ServiceCatalogEntry> merged;
		const auto& adult = VpnAdultSites::Catalog();
		merged.reserve((std::end(kCatalog) - std::begin(kCatalog)) + adult.size());
		bool adultInserted = false;
		for (const ServiceCatalogEntry& item : kCatalog)
		{
			if (!adultInserted && item.section == ServiceCatalogSection::ForeignMisc)
			{
				merged.insert(merged.end(), adult.begin(), adult.end());
				adultInserted = true;
			}
			merged.push_back(item);
		}
		if (!adultInserted)
			merged.insert(merged.end(), adult.begin(), adult.end());
		return merged;
	}();
	return catalog;
}

void VpnServiceRoutes::BuildDefaultRoutes(std::vector<ServiceRouteEntry>& outRoutes)
{
	const std::vector<ServiceCatalogEntry>& catalog = Catalog();
	outRoutes.clear();
	outRoutes.reserve(catalog.size());
	for (const ServiceCatalogEntry& item : catalog)
	{
		ServiceRouteEntry entry;
		AssignCatalogEntry(item, entry);
		outRoutes.push_back(entry);
	}
}

bool VpnServiceRoutes::Load(std::vector<ServiceRouteEntry>& outRoutes)
{
	std::lock_guard<std::mutex> lock(g_routesMutex);
	BuildDefaultRoutes(outRoutes);

	std::unordered_map<std::string, size_t> byId;
	for (size_t i = 0; i < outRoutes.size(); ++i)
		byId[outRoutes[i].id] = i;

	std::ifstream input(RoutesFile(), std::ios::binary);
	if (!input)
		return false;

	struct PendingSection
	{
		std::string id;
		std::unordered_map<std::string, std::string> kv;
	};
	std::vector<PendingSection> sections;
	PendingSection* current = nullptr;

	std::string line;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			PendingSection section;
			section.id = line.substr(1, line.size() - 2);
			sections.push_back(std::move(section));
			current = &sections.back();
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos || current == nullptr)
			continue;

		const std::string key = Trim(line.substr(0, eq));
		const std::string value = Trim(line.substr(eq + 1));
		current->kv[key] = value;
	}

	for (const PendingSection& section : sections)
	{
		const auto existing = byId.find(section.id);
		const auto customIt = section.kv.find("custom");
		const bool isCustom = (customIt != section.kv.end() && ParseBool(customIt->second, false))
			|| section.id.rfind("custom_", 0) == 0;

		if (existing != byId.end())
		{
			ServiceRouteEntry& entry = outRoutes[existing->second];
			const auto modeIt = section.kv.find("mode");
			if (modeIt != section.kv.end())
			{
				const int mode = ParseInt(modeIt->second, static_cast<int>(ServiceRouteMode::None));
				if (mode >= static_cast<int>(ServiceRouteMode::Antizapret)
					&& mode <= static_cast<int>(ServiceRouteMode::None))
				{
					entry.mode = static_cast<ServiceRouteMode>(mode);
				}
			}
			const auto enabledIt = section.kv.find("enabled");
			if (enabledIt != section.kv.end())
				entry.enabled = ParseBool(enabledIt->second, true);
			continue;
		}

		if (!isCustom)
			continue;

		ServiceRouteEntry entry;
		entry.id = section.id;
		entry.custom = true;
		entry.enabled = true;
		entry.mode = ServiceRouteMode::None;
		entry.kind = ServiceCatalogKind::Site;
		entry.region = ServiceCatalogRegion::Foreign;
		entry.section = ServiceCatalogSection::CustomSites;
		entry.icon = 0xE774;
		entry.brandIcon = false;

		auto get = [&](const char* key) -> std::string {
			const auto it = section.kv.find(key);
			return it == section.kv.end() ? std::string() : it->second;
		};

		const std::string name = get("name");
		entry.name = name.empty() ? section.id : name;
		entry.description = get("description");

		const int kind = ParseInt(get("kind"), static_cast<int>(ServiceCatalogKind::Site));
		entry.kind = kind == static_cast<int>(ServiceCatalogKind::App)
			? ServiceCatalogKind::App
			: ServiceCatalogKind::Site;
		entry.section = entry.kind == ServiceCatalogKind::App
			? ServiceCatalogSection::CustomApps
			: ServiceCatalogSection::CustomSites;
		entry.icon = entry.kind == ServiceCatalogKind::App ? 0xE71D : 0xE774;

		const int region = ParseInt(get("region"), static_cast<int>(ServiceCatalogRegion::Foreign));
		if (region == static_cast<int>(ServiceCatalogRegion::Russian))
			entry.region = ServiceCatalogRegion::Russian;

		const int mode = ParseInt(get("mode"), static_cast<int>(ServiceRouteMode::None));
		if (mode >= static_cast<int>(ServiceRouteMode::Antizapret)
			&& mode <= static_cast<int>(ServiceRouteMode::None))
		{
			entry.mode = static_cast<ServiceRouteMode>(mode);
		}
		entry.enabled = ParseBool(get("enabled"), true);

		byId[entry.id] = outRoutes.size();
		outRoutes.push_back(std::move(entry));
	}

	return true;
}

void VpnServiceRoutes::Save(const std::vector<ServiceRouteEntry>& routes)
{
	std::lock_guard<std::mutex> lock(g_routesMutex);
	const std::filesystem::path path = RoutesFile();
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output)
		return;

	output << "; AntiZapret service routing\r\n";
	for (const ServiceRouteEntry& entry : routes)
	{
		output << "[" << entry.id << "]\r\n";
		output << "mode=" << static_cast<int>(entry.mode) << "\r\n";
		output << "enabled=" << (entry.enabled ? 1 : 0) << "\r\n";
		if (entry.custom)
		{
			output << "custom=1\r\n";
			output << "kind=" << static_cast<int>(entry.kind) << "\r\n";
			output << "name=" << entry.name << "\r\n";
			output << "description=" << entry.description << "\r\n";
			output << "region=" << static_cast<int>(entry.region) << "\r\n";
			output << "section=" << static_cast<int>(entry.section) << "\r\n";
			output << "icon=" << entry.icon << "\r\n";
			output << "brandIcon=" << (entry.brandIcon ? 1 : 0) << "\r\n";
		}
	}
}

ServiceRouteEntry VpnServiceRoutes::MakeCustomEntry(
	ServiceCatalogKind kind,
	const std::string& name,
	const std::string& targets)
{
	ServiceRouteEntry entry;
	entry.custom = true;
	entry.kind = kind;
	entry.name = name.empty() ? (kind == ServiceCatalogKind::App ? "Приложение" : "Сайт") : name;
	entry.description = targets;
	entry.region = ServiceCatalogRegion::Foreign;
	entry.section = kind == ServiceCatalogKind::App
		? ServiceCatalogSection::CustomApps
		: ServiceCatalogSection::CustomSites;
	entry.icon = kind == ServiceCatalogKind::App ? 0xE71D : 0xE774;
	entry.brandIcon = false;
	entry.enabled = true;
	entry.mode = ServiceRouteMode::None;

	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	entry.id = std::string("custom_")
		+ (kind == ServiceCatalogKind::App ? "app_" : "site_")
		+ SanitizeCustomIdPart(entry.name)
		+ "_"
		+ std::to_string(static_cast<unsigned long long>(stamp % 100000000ULL));
	return entry;
}

bool VpnServiceRoutes::IsFixDiscordEffective(const ServiceRouteEntry& entry)
{
	if (entry.id != "discord" || !entry.enabled)
		return false;
	return entry.mode == ServiceRouteMode::VpnTunnel
		|| entry.mode == ServiceRouteMode::VpnProxy;
}

bool VpnServiceRoutes::ApplyFixDiscordToEntry(ServiceRouteEntry& entry, bool fixDiscord)
{
	if (entry.id != "discord")
		return false;

	bool changed = false;
	if (fixDiscord)
	{
		if (!entry.enabled)
		{
			entry.enabled = true;
			changed = true;
		}
		if (entry.mode != ServiceRouteMode::VpnTunnel
			&& entry.mode != ServiceRouteMode::VpnProxy)
		{
			entry.mode = ServiceRouteMode::VpnTunnel;
			changed = true;
		}
	}
	else if (entry.mode == ServiceRouteMode::VpnTunnel
		|| entry.mode == ServiceRouteMode::VpnProxy)
	{
		entry.mode = ServiceRouteMode::Antizapret;
		changed = true;
	}
	return changed;
}

bool VpnServiceRoutes::ApplyFixDiscordToRoutes(std::vector<ServiceRouteEntry>& routes, bool fixDiscord)
{
	bool changed = false;
	for (ServiceRouteEntry& entry : routes)
	{
		if (ApplyFixDiscordToEntry(entry, fixDiscord))
			changed = true;
	}
	return changed;
}
