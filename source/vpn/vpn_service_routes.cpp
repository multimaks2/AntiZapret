#include "vpn/vpn_service_routes.h"

#include "vpn/vpn_adult_sites.h"
#include "vpn/vpn_service_fallback_domains.h"
#include "zapret/zapret_paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
	std::mutex g_routesMutex;

	void AssignCatalogEntry(const ServiceCatalogEntry& item, ServiceRouteEntry& entry)
	{
		entry.id = item.id ? item.id : "";
		entry.icon = item.icon;
		entry.name = item.name ? item.name : "";
		entry.description = item.description ? item.description : "";
		entry.region = item.region;
		entry.section = item.section;
		entry.enabled = true;
		entry.mode = ServiceRouteMode::None;
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
		return std::filesystem::path(ZapretPaths::GetVpnDirectory()) / L"cache" / L"service-routes.ini";
	}

	const ServiceCatalogEntry kCatalog[] = {
		// --- Зарубежные: утилиты ---
		{ "2ip", 0xE774, "2ip.ru", "2ip.ru, api.2ip.ru", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignTools },

		// --- Зарубежные: соцсети и мессенджеры ---
		{ "youtube", 0xE714, "YouTube", "YouTube, ytimg, ggpht, googleapis", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "discord", 0xE8BD, "Discord", "Discord, discord.media, AyuGram, Vesktop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "telegram", 0xE724, "Telegram", "Telegram, t.me, telegram-cdn, AyuGram, Unigram", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "instagram", 0xE91B, "Instagram", "Instagram, CDN Instagram", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "facebook", 0xE716, "Facebook", "Facebook, fbcdn, fburl", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "twitter", 0xE70F, "Twitter / X", "Twitter, X, t.co, twimg", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "tiktok", 0xE714, "TikTok", "tiktok.com, tiktokcdn.com, musically", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "whatsapp", 0xE8BD, "WhatsApp", "whatsapp.com, web.whatsapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "linkedin", 0xE716, "LinkedIn", "linkedin.com, licdn.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },
		{ "reddit", 0xE8BD, "Reddit", "reddit.com, redditstatic.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSocial },

		// --- Зарубежные: стриминг и музыка ---
		{ "twitch", 0xE714, "Twitch", "Twitch, twitchcdn, jtvnw", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "soundcloud", 0xE8D6, "SoundCloud", "SoundCloud, sndcdn", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "spotify", 0xE8D6, "Spotify", "spotify.com, scdn.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },
		{ "netflix", 0xE714, "Netflix", "netflix.com, nflxvideo.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignStreaming },

		// --- Зарубежные: браузеры ---
		{ "chrome", 0xE774, "Google Chrome", "Chrome, Chromium", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "firefox", 0xE774, "Firefox", "Firefox, Waterfox, LibreWolf", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "edge", 0xE774, "Microsoft Edge", "msedge.exe, Edge WebView", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "opera", 0xE774, "Opera", "opera.exe, opera gx", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },
		{ "brave", 0xE774, "Brave", "brave.exe, Chromium", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignBrowser },

		// --- Зарубежные: AI (ограничены / недоступны в РФ) ---
		{ "chatgpt", 0xE8F1, "ChatGPT / OpenAI", "chatgpt.com, openai.com, api.openai.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "claude", 0xE8F1, "Claude / Anthropic", "claude.ai, anthropic.com, api.anthropic.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "gemini", 0xE8F1, "Google Gemini", "gemini.google.com, bard.google.com, generativelanguage.googleapis.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "copilot", 0xE8F1, "Microsoft Copilot", "copilot.microsoft.com, bing.com/chat", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "grok", 0xE8F1, "Grok AI (xAI / Илон Маск)", "grok.x.com, x.ai, grok.com, console.x.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "perplexity", 0xE8F1, "Perplexity", "perplexity.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "midjourney", 0xE8F1, "Midjourney", "midjourney.com, discord Midjourney", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "character_ai", 0xE8F1, "Character.AI", "character.ai, beta.character.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "groq", 0xE8F1, "Groq", "groq.com, console.groq.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "elevenlabs", 0xE8F1, "ElevenLabs", "elevenlabs.io, api.elevenlabs.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "runway", 0xE8F1, "Runway", "runwayml.com, app.runwayml.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "leonardo", 0xE8F1, "Leonardo.AI", "leonardo.ai, cloud.leonardo.ai", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "huggingface", 0xE8F1, "Hugging Face", "huggingface.co, hf.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "poe", 0xE8F1, "Poe", "poe.com, quora.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },
		{ "notion_ai", 0xE8F1, "Notion AI", "notion.so, notion.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignAI },

		// --- Зарубежные: для разработчиков ---
		{ "github", 0xE943, "GitHub", "github.com, githubusercontent.com, ghcr.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gitlab", 0xE943, "GitLab", "gitlab.com, gitlab.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "bitbucket", 0xE943, "Bitbucket", "bitbucket.org, atlassian.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "stackoverflow", 0xE8F1, "Stack Overflow", "stackoverflow.com, stackexchange.com, sstatic.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "npm", 0xE943, "npm", "npmjs.com, registry.npmjs.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "pypi", 0xE943, "PyPI", "pypi.org, pythonhosted.org, files.pythonhosted.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "crates_io", 0xE943, "crates.io", "crates.io, static.crates.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "golang", 0xE943, "Go / pkg.go.dev", "go.dev, pkg.go.dev, proxy.golang.org", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "nuget", 0xE943, "NuGet", "nuget.org, nuget.azure.cn", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "maven", 0xE943, "Maven Central", "maven.org, repo1.maven.org, central.sonatype.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "dockerhub", 0xE943, "Docker Hub", "docker.com, docker.io, hub.docker.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "hashicorp", 0xE943, "HashiCorp", "hashicorp.com, terraform.io, releases.hashicorp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "aws", 0xE950, "Amazon AWS", "amazonaws.com, aws.amazon.com, cloudfront.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gcp", 0xE950, "Google Cloud", "cloud.google.com, googleapis.com, gcr.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "azure", 0xE950, "Microsoft Azure", "azure.com, azureedge.net, visualstudio.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "cloudflare", 0xE950, "Cloudflare", "cloudflare.com, workers.dev, r2.dev", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "vercel", 0xE950, "Vercel", "vercel.com, vercel.app, now.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "netlify", 0xE950, "Netlify", "netlify.com, netlify.app", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "digitalocean", 0xE950, "DigitalOcean", "digitalocean.com, digitaloceanspaces.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "heroku", 0xE950, "Heroku", "heroku.com, herokuapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "render", 0xE950, "Render", "render.com, onrender.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "railway", 0xE950, "Railway", "railway.app, railway.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "supabase", 0xE950, "Supabase", "supabase.com, supabase.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "firebase", 0xE950, "Firebase", "firebase.google.com, firebaseio.com, firebaseapp.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "mongodb_atlas", 0xE950, "MongoDB Atlas", "mongodb.com, mongodb.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "planetscale", 0xE950, "PlanetScale", "planetscale.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "replit", 0xE943, "Replit", "replit.com, repl.co", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "codepen", 0xE943, "CodePen", "codepen.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "jsfiddle", 0xE943, "JSFiddle", "jsfiddle.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "codesandbox", 0xE943, "CodeSandbox", "codesandbox.io, csb.app", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "stackblitz", 0xE943, "StackBlitz", "stackblitz.com, webcontainer.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "cursor_ide", 0xE943, "Cursor", "cursor.com, cursor.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "jetbrains", 0xE943, "JetBrains", "jetbrains.com, jb.gg", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "vscode", 0xE943, "VS Code Marketplace", "code.visualstudio.com, marketplace.visualstudio.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "sourceforge", 0xE8F1, "SourceForge", "sourceforge.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gist", 0xE943, "GitHub Gist", "gist.github.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "gitkraken", 0xE943, "GitKraken / Axosoft", "gitkraken.com, axosoft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "postman", 0xE943, "Postman", "postman.com, getpostman.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "sentry", 0xE950, "Sentry", "sentry.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "datadog", 0xE950, "Datadog", "datadoghq.com, datadoghq.eu", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "grafana", 0xE950, "Grafana", "grafana.com, grafana.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },
		{ "openai_api", 0xE8F1, "OpenAI API / platform", "platform.openai.com, api.openai.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignDev },

		// --- Зарубежные: игровые лаунчеры ---
		{ "steam", 0xE7FC, "Steam", "steam.exe, steampowered.com, steamcommunity.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "epic", 0xE7FC, "Epic Games", "EpicGamesLauncher.exe, epicgames.com, unrealengine.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "battlenet", 0xE7FC, "Battle.net", "Battle.net.exe, blizzard.com, battle.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "ea_app", 0xE7FC, "EA App", "EADesktop.exe, ea.com, origin.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "ubisoft", 0xE7FC, "Ubisoft Connect", "upc.exe, ubisoft.com, ubi.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "rockstar", 0xE7FC, "Rockstar Launcher", "Launcher.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "gog", 0xE7FC, "GOG Galaxy", "GalaxyClient.exe, gog.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "riot", 0xE7FC, "Riot Client", "RiotClientServices.exe, riotgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "xbox", 0xE7FC, "Xbox / Microsoft Store", "XboxPcApp.exe, xbox.com, xboxlive.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },
		{ "itch", 0xE7FC, "itch.io", "itch.exe, itch.io", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignLaunchers },

		// --- Зарубежные: онлайн / сетевые игры ---
		{ "sigame", 0xE7FC, "SiGame", "SIGame.exe, vladimirkhil.com, своя игра", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "genshin", 0xE7FC, "Genshin Impact", "GenshinImpact.exe, hoyoverse.com, mihoyo.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "honkai", 0xE7FC, "Honkai: Star Rail", "StarRail.exe, hoyoverse.com, mihoyo.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "zzz", 0xE7FC, "Zenless Zone Zero", "ZenlessZoneZero.exe, hoyoverse.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "valorant", 0xE7FC, "Valorant", "VALORANT.exe, riotgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "league", 0xE7FC, "League of Legends", "LeagueClient.exe, leagueoflegends.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fortnite", 0xE7FC, "Fortnite", "FortniteClient, epicgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "roblox", 0xE7FC, "Roblox", "RobloxPlayerBeta.exe, roblox.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "minecraft", 0xE7FC, "Minecraft", "Minecraft.exe, minecraft.net, mojang.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "faceit", 0xE7FC, "FACEIT", "faceit.exe, faceit.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "apex", 0xE7FC, "Apex Legends", "r5apex.exe, ea.com/games/apex", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warzone", 0xE7FC, "Call of Duty / Warzone", "cod.exe, callofduty.com, activision.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wot", 0xE7FC, "World of Tanks / Warships", "WorldOfTanks.exe, wargaming.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "cs2", 0xE7FC, "Counter-Strike 2", "cs2.exe, counter-strike.net, steam", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "dota2", 0xE7FC, "Dota 2", "dota2.exe, dota2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "pubg", 0xE7FC, "PUBG", "TslGame.exe, pubg.com, krafton.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "overwatch", 0xE7FC, "Overwatch 2", "Overwatch.exe, overwatch.blizzard.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "wow", 0xE7FC, "World of Warcraft", "Wow.exe, worldofwarcraft.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "diablo", 0xE7FC, "Diablo IV", "Diablo IV.exe, diablo.blizzard.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "hearthstone", 0xE7FC, "Hearthstone", "Hearthstone.exe, playhearthstone.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "path_of_exile", 0xE7FC, "Path of Exile", "PathOfExile.exe, pathofexile.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "destiny2", 0xE7FC, "Destiny 2", "destiny2.exe, bungie.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "gta5", 0xE7FC, "GTA Online", "GTA5.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rdr2", 0xE7FC, "Red Dead Online", "RDR2.exe, rockstargames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "helldivers2", 0xE7FC, "Helldivers 2", "helldivers2.exe, arrowheadgamestudios.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "palworld", 0xE7FC, "Palworld", "Palworld-Win64-Shipping.exe, palworldgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rust", 0xE7FC, "Rust", "RustClient.exe, rust.facepunch.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "ark", 0xE7FC, "ARK: Survival", "ShooterGame.exe, playark.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "tarkov", 0xE7FC, "Escape from Tarkov", "EscapeFromTarkov.exe, escapefromtarkov.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warthunder", 0xE7FC, "War Thunder", "aces.exe, warthunder.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fc25", 0xE7FC, "EA FC Online / FIFA", "FC25.exe, ea.com/games/ea-sports-fc", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "nba2k", 0xE7FC, "NBA 2K Online", "NBA2K.exe, nba.com, 2k.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "rocket_league", 0xE7FC, "Rocket League", "RocketLeague.exe, rocketleague.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "dbd", 0xE7FC, "Dead by Daylight", "DeadByDaylight.exe, deadbydaylight.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "among_us", 0xE7FC, "Among Us", "Among Us.exe, innersloth.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "ffxiv", 0xE7FC, "Final Fantasy XIV", "ffxiv_dx11.exe, finalfantasyxiv.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "lost_ark", 0xE7FC, "Lost Ark", "LOSTARK.exe, playlostark.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "new_world", 0xE7FC, "New World", "NewWorld.exe, newworld.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "deadlock", 0xE7FC, "Deadlock", "deadlock.exe, playdeadlock.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "tf2", 0xE7FC, "Team Fortress 2", "hl2.exe, teamfortress.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "fall_guys", 0xE7FC, "Fall Guys", "FallGuys_client.exe, fallguys.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "brawlhalla", 0xE7FC, "Brawlhalla", "Brawlhalla.exe, brawlhalla.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "smite", 0xE7FC, "Smite", "Smite.exe, smitegame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "osu", 0xE7FC, "osu!", "osu!.exe, osu.ppy.sh", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "vrchat", 0xE7FC, "VRChat", "VRChat.exe, vrchat.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "phasmophobia", 0xE7FC, "Phasmophobia", "Phasmophobia.exe, kineticgames.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "warface", 0xE7FC, "Warface", "Warface.exe, warface.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "crossout", 0xE7FC, "Crossout", "Crossout.exe, crossout.net", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "albion", 0xE7FC, "Albion Online", "Albion-Online.exe, albiononline.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "eve", 0xE7FC, "EVE Online", "exefile.exe, eveonline.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },
		{ "hunt", 0xE7FC, "Hunt: Showdown", "HuntGame.exe, huntshowdown.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignGames },

		// --- Зарубежные: новинки Steam (онлайн / сетевые) ---
		{ "marvel_rivals", 0xE7FC, "Marvel Rivals", "MarvelRivals.exe, marvelrivals.com, NetEase", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "delta_force", 0xE7FC, "Delta Force", "DeltaForce.exe, deltaforcegame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "peak", 0xE7FC, "PEAK", "PEAK.exe, steam PEAK coop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "repo", 0xE7FC, "R.E.P.O.", "REPO.exe, steam R.E.P.O. coop", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "arc_raiders", 0xE7FC, "ARC Raiders", "ARCRaiders.exe, arcraiders.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "schedule_i", 0xE7FC, "Schedule I", "Schedule I.exe, steam Schedule I", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "poe2", 0xE7FC, "Path of Exile 2", "PathOfExile_x64.exe, pathofexile2.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "throne_liberty", 0xE7FC, "Throne and Liberty", "TL.exe, playthroneandliberty.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "once_human", 0xE7FC, "Once Human", "ONCE_HUMAN.exe, oncehuman.game", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "fragpunk", 0xE7FC, "FragPunk", "FragPunk.exe, fragpunk.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "the_finals", 0xE7FC, "THE FINALS", "Discovery.exe, reachthefinals.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "battlefield", 0xE7FC, "Battlefield", "bf6.exe / bf2042.exe, ea.com/games/battlefield", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "abiotic_factor", 0xE7FC, "Abiotic Factor", "AbioticFactor.exe, abioticfactor.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "content_warning", 0xE7FC, "Content Warning", "Content Warning.exe, landfall.se", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "lethal_company", 0xE7FC, "Lethal Company", "Lethal Company.exe, zeekerss.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "party_animals", 0xE7FC, "Party Animals", "PartyAnimals.exe, partyanimalsgame.com", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "spiritvale", 0xE7FC, "SpiritVale", "SpiritVale.exe, action MMO Steam", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "ragnarok_tnw", 0xE7FC, "Ragnarok: The New World", "RagnarokTNW.exe, Gravity MMO", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "zerospace", 0xE7FC, "ZeroSpace", "ZeroSpace.exe, RTS/RPG multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "soulbound", 0xE7FC, "Soulbound: Online", "Soulbound.exe, SpiderWare MMO", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "carnival_hunt", 0xE7FC, "Carnival Hunt", "CarnivalHunt.exe, asymmetric horror", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "codename_cure2", 0xE7FC, "Codename CURE II", "CodenameCURE2.exe, co-op zombie", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "gun_x_gunner", 0xE7FC, "GUN X GUNNER", "GunXGunner.exe, tactical multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "ouch_cargo", 0xE7FC, "Ouch Cargo", "OuchCargo.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "funnel_runners", 0xE7FC, "Funnel Runners", "FunnelRunners.exe, co-op survival", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "galley_mound", 0xE7FC, "The Mound: Omen of Cthulhu", "TheMound.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "bodycam_onrecord", 0xE7FC, "Bodycam Onrecord", "BodycamOnrecord.exe, PvP / co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "halo_evolved", 0xE7FC, "Halo: Campaign Evolved", "HaloCampaignEvolved.exe, 4-player co-op", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "nightreign", 0xE7FC, "ELDEN RING Nightreign", "Nightreign.exe, co-op FromSoftware", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "monster_hunter_wilds", 0xE7FC, "Monster Hunter Wilds", "MonsterHunterWilds.exe, Capcom online hunt", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "umamusume", 0xE7FC, "Umamusume: Pretty Derby", "umamusume.exe, Cygames online", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "meccha_chameleon", 0xE7FC, "Meccha Chameleon", "MecchaChameleon.exe, party Steam hit", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "difficult", 0xE7FC, "diffiCULT", "diffiCULT.exe, social deduction multiplayer", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },
		{ "warena", 0xE7FC, "Warena", "Warena.exe, real-time card battler", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignSteamNew },

		// --- Зарубежные: прочее ---
		{ "torrents", 0xE896, "Торренты", "qBittorrent, uTorrent, Transmission", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },
		{ "windows", 0xEC05, "Windows", "Системные процессы Windows", ServiceCatalogRegion::Foreign, ServiceCatalogSection::ForeignMisc },

		// --- Российские: браузеры ---
		{ "yandex_browser", 0xE774, "Яндекс Браузер", "browser.exe, yandexbrowser.exe, yandex.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },
		{ "atom_browser", 0xE774, "Atom (VK)", "atom.exe, браузер VK / Mail.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBrowser },

		// --- Российские: экосистемы ---
		{ "yandex", 0xE821, "Яндекс", "Диск, Музыка, Маркет, Карты, Почта, yandex.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "vk", 0xE716, "VK", "VK.exe, vk.com, vk.ru, vkplay.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "mailru", 0xE715, "Mail.ru", "mail.ru, cloud.mail.ru, ICQ New", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "ok", 0xE716, "Одноклассники", "ok.ru, odnoklassniki.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },
		{ "max", 0xE8BD, "MAX", "max.ru, мессенджер VK", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianEco },

		// --- Российские: банки ---
		{ "sberbank", 0xE825, "Сбербанк", "Sberbank.exe, online.sberbank.ru, sber.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "tinkoff", 0xE825, "Т-Банк", "Tinkoff.exe, tbank.ru, tinkoff.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "vtb", 0xE825, "ВТБ", "VTB.exe, vtb.ru, online.vtb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "alfabank", 0xE825, "Альфа-Банк", "Alfa-Bank.exe, alfabank.ru, alfa.me", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "yoomoney", 0xE825, "ЮMoney", "yoomoney.ru, yookassa.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "gazprombank", 0xE825, "Газпромбанк", "gazprombank.ru, gpb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "sovcombank", 0xE825, "Совкомбанк", "sovcombank.ru, halvacard.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "raiffeisen", 0xE825, "Райффайзен", "raiffeisen.ru, online.raiffeisen.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "rosbank", 0xE825, "Росбанк", "rosbank.ru, online.rosbank.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },
		{ "mtsbank", 0xE825, "МТС Банк", "mtsbank.ru, mtsdengi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianBank },

		// --- Российские: госуслуги ---
		{ "gosuslugi", 0xE72E, "Госуслуги", "gosuslugi.ru, esia.gosuslugi.ru, lk.gosuslugi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "nalog", 0xE72E, "ФНС / Налоги", "nalog.gov.ru, lkfl2.nalog.ru, gov.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "mos_ru", 0xE72E, "Москва / mos.ru", "mos.ru, my.mos.ru, uslugi.mos.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },
		{ "pochta_ru", 0xE715, "Почта России", "pochta.ru, tracking.pochta.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianGov },

		// --- Российские: маркетплейсы ---
		{ "wildberries", 0xE719, "Wildberries", "wildberries.ru, wb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "ozon", 0xE719, "Ozon", "ozon.ru, ozon.app, Ozon Bank", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "avito", 0xE719, "Авито", "avito.ru, avito.st", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "megamarket", 0xE719, "СберМегаМаркет", "megamarket.ru, sbermegamarket.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "dns_shop", 0xE719, "DNS", "dns-shop.ru, dns-shop.net", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "mvideo", 0xE719, "М.Видео / Эльдорадо", "mvideo.ru, eldorado.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "citilink", 0xE719, "Ситилинк", "citilink.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "lamoda", 0xE719, "Lamoda", "lamoda.ru, lamoda.co", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "goldapple", 0xE719, "Золотое Яблоко", "goldapple.ru, gacdn.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "vkusvill", 0xE719, "ВкусВилл", "vkusvill.ru, online.vkusvill.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },
		{ "pyaterochka", 0xE719, "Пятёрочка / X5", "5ka.ru, perekrestok.ru, x5.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianShop },

		// --- Российские: доставка ---
		{ "yandex_go", 0xE804, "Яндекс Go", "taxi.yandex.ru, eda.yandex, доставка", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "samokat", 0xE804, "Самокат", "samokat.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "cdek", 0xE804, "СДЭК", "cdek.ru, lk.cdek.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },
		{ "delivery_club", 0xE804, "Delivery Club", "delivery-club.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianDelivery },

		// --- Российские: телеком ---
		{ "mts", 0xE717, "МТС", "mts.ru, mymts.ru, lk.mts.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "megafon", 0xE717, "МегаФон", "megafon.ru, lk.megafon.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "beeline", 0xE717, "Билайн", "beeline.ru, my.beeline.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "tele2", 0xE717, "Tele2", "tele2.ru, t2.ru, my.tele2.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },
		{ "rostelecom", 0xE717, "Ростелеком", "rt.ru, rostelecom.ru, lk.rt.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTelecom },

		// --- Российские: стриминг ---
		{ "rutube", 0xE714, "Rutube", "rutube.ru, static.rutube.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "okko", 0xE714, "Okko", "okko.tv, api.okko.tv", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "ivi", 0xE714, "IVI", "ivi.ru, ivi.tv, api.ivi.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "kion", 0xE714, "KION", "kion.ru, api.kion.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "wink", 0xE714, "Wink", "wink.ru, api.wink.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "start", 0xE714, "START", "start.ru, start.video", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "premier", 0xE714, "Premier", "premier.one, api.premier.one", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },
		{ "zvuk", 0xE8D6, "Звук", "zvuk.com, sberaudio.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianStreaming },

		// --- Российские: транспорт ---
		{ "gis2", 0xE821, "2ГИС", "2gis.ru, 2gis.com, навигация", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "rzd", 0xE806, "РЖД", "rzd.ru, ticket.rzd.ru, pass.rzd.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "aeroflot", 0xE806, "Аэрофлот", "aeroflot.ru, booking.aeroflot.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "s7", 0xE806, "S7 Airlines", "s7.ru, api.s7.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "tutu", 0xE806, "Tutu.ru", "tutu.ru, api.tutu.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },
		{ "aviasales", 0xE806, "Aviasales", "aviasales.ru, aviasales.com", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianTravel },

		// --- Российские: недвижимость и авто ---
		{ "cian", 0xE821, "ЦИАН", "cian.ru, api.cian.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "domclick", 0xE821, "Домклик", "domclick.ru, api.domclick.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "auto_ru", 0xE804, "Auto.ru", "auto.ru, api.auto.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },
		{ "drom", 0xE804, "Drom.ru", "drom.ru, auto.drom.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianProperty },

		// --- Российские: работа, медицина, безопасность ---
		{ "hh", 0xE734, "HeadHunter", "hh.ru, headhunter.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "superjob", 0xE734, "SuperJob", "superjob.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "habr", 0xE734, "Хабр", "habr.com, career.habr.com", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "sberhealth", 0xE72E, "СберЗдоровье", "sberhealth.ru, doctoronline.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "invitro", 0xE72E, "Invitro", "invitro.ru, lk.invitro.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "apteka", 0xE72E, "Apteka.ru", "apteka.ru, eapteka.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "kaspersky", 0xE72E, "Kaspersky", "avp.exe, ksde.exe, kaspersky.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },
		{ "drweb", 0xE72E, "Dr.Web", "drweb.exe, drweb.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianWorkHealth },

		// --- Российские: прочее ---
		{ "rustore", 0xE896, "RuStore", "RuStore.exe, rustore.ru", ServiceCatalogRegion::Russian, ServiceCatalogSection::RussianMisc },
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
	if (VpnAdultSites::IsAdultServiceId(serviceId))
		return true;

	static const char* kFallbackOnly[] = {
		"2ip", "sigame", "genshin", "honkai", "zzz", "faceit", "itch",
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
	case ServiceCatalogSection::ForeignMisc: return "Прочее";
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
	case ServiceCatalogSection::RussianMisc: return "Прочее";
	default: return "Сервисы";
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

	std::unordered_map<std::string, ServiceRouteEntry*> byId;
	for (ServiceRouteEntry& entry : outRoutes)
		byId[entry.id] = &entry;

	std::ifstream input(RoutesFile(), std::ios::binary);
	if (!input)
		return false;

	std::string currentId;
	std::string line;
	while (std::getline(input, line))
	{
		line = Trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;

		if (line.front() == '[' && line.back() == ']')
		{
			currentId = line.substr(1, line.size() - 2);
			continue;
		}

		const size_t eq = line.find('=');
		if (eq == std::string::npos || currentId.empty())
			continue;

		const std::string key = Trim(line.substr(0, eq));
		const std::string value = Trim(line.substr(eq + 1));
		const auto it = byId.find(currentId);
		if (it == byId.end())
			continue;

		if (key == "mode")
		{
			const int mode = ParseInt(value, static_cast<int>(ServiceRouteMode::None));
			if (mode >= static_cast<int>(ServiceRouteMode::Antizapret)
				&& mode <= static_cast<int>(ServiceRouteMode::None))
			{
				it->second->mode = static_cast<ServiceRouteMode>(mode);
			}
		}
		else if (key == "enabled")
		{
			it->second->enabled = ParseBool(value, true);
		}
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
	}
}
