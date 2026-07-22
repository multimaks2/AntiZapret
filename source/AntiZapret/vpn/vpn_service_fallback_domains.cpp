#include "vpn/vpn_service_fallback_domains.h"

#include <unordered_map>

namespace
{
	struct DomainEntry
	{
		const char* const* domains = nullptr;
		size_t count = 0;
	};

#define AZ_DOMAINS(var, ...) \
	static const char* const k##var[] = { __VA_ARGS__ }

	AZ_DOMAINS(Domains2ip, "2ip.ru", "www.2ip.ru", "api.2ip.ru");
	AZ_DOMAINS(DomainsYoutube,
		"youtube.com", "youtu.be", "googlevideo.com", "ytimg.com", "ggpht.com",
		"youtube-nocookie.com", "youtubei.googleapis.com", "yt3.ggpht.com");
	AZ_DOMAINS(DomainsDiscord,
		"discord.com", "discord.gg", "discordapp.com", "discordapp.net", "discord.media",
		"discord.co", "discordstatus.com", "discordcdn.com", "discord.app", "discord.dev",
		"discord.new", "discord.gift", "discord.gifts", "discord.store", "discord.design",
		"discord.tools", "discordactivities.com", "discordpartygames.com", "discordsays.com",
		"discordsez.com", "discord-activities.com", "discordmerch.com", "discordapp.io",
		"gateway.discord.gg", "cdn.discordapp.com", "media.discordapp.net",
		"images-ext-1.discordapp.net", "stable.dl2.discordapp.net",
		"www.discord.com", "www.discord.app",
		"discord-attachments-uploads-prd.storage.googleapis.com");
	AZ_DOMAINS(DomainsTelegram,
		"telegram.org", "t.me", "telegra.ph", "telegram-cdn.org", "telesco.pe", "tg.dev");
	AZ_DOMAINS(DomainsInstagram, "instagram.com", "cdninstagram.com", "ig.me", "igsonar.com");
	AZ_DOMAINS(DomainsFacebook, "facebook.com", "fb.com", "fbcdn.net", "fbsbx.com", "fburl.com");
	AZ_DOMAINS(DomainsTwitter, "twitter.com", "x.com", "t.co", "twimg.com", "twitpic.com");
	AZ_DOMAINS(DomainsTiktok, "tiktok.com", "tiktokcdn.com", "tiktokv.com", "musical.ly", "muscdn.com");
	AZ_DOMAINS(DomainsWhatsapp, "whatsapp.com", "whatsapp.net", "wa.me", "web.whatsapp.com");
	AZ_DOMAINS(DomainsLinkedin, "linkedin.com", "licdn.com", "lnkd.in");
	AZ_DOMAINS(DomainsReddit, "reddit.com", "redditstatic.com", "redditmedia.com", "redd.it");
	AZ_DOMAINS(DomainsTwitch, "twitch.tv", "twitchcdn.net", "jtvnw.net", "ttvnw.net");
	AZ_DOMAINS(DomainsSoundcloud, "soundcloud.com", "sndcdn.com");
	AZ_DOMAINS(DomainsSpotify, "spotify.com", "scdn.co", "spotifycdn.com", "spotifycdn.net");
	AZ_DOMAINS(DomainsNetflix,
		"netflix.com", "nflxext.com", "nflximg.com", "nflximg.net", "nflxso.net", "nflxvideo.net");
	AZ_DOMAINS(DomainsChrome,
		"google.com", "googleapis.com", "gstatic.com", "chromium.org", "googleusercontent.com");
	AZ_DOMAINS(DomainsFirefox, "mozilla.org", "mozilla.com", "mozilla.net", "firefox.com");
	AZ_DOMAINS(DomainsEdge, "microsoft.com", "microsoftedge.com", "msedge.net", "live.com");
	AZ_DOMAINS(DomainsOpera, "opera.com", "opera.software", "operacdn.com");
	AZ_DOMAINS(DomainsBrave, "brave.com", "bravebrowser.com", "bravesoftware.com");
	AZ_DOMAINS(DomainsChatgpt, "openai.com", "chatgpt.com", "oaistatic.com", "oaiusercontent.com", "ai.com");
	AZ_DOMAINS(DomainsClaude, "claude.ai", "anthropic.com", "api.anthropic.com");
	AZ_DOMAINS(DomainsGemini,
		"gemini.google.com", "bard.google.com", "generativelanguage.googleapis.com",
		"alkalicore-pa.clients6.google.com", "aisandbox-pa.googleapis.com");
	AZ_DOMAINS(DomainsCopilot,
		"copilot.microsoft.com", "bing.com", "sydney.bing.com", "edgeservices.bing.com");
	AZ_DOMAINS(DomainsGrok,
		"x.ai", "grok.x.ai", "grok.com", "grok.x.com", "console.x.ai",
		"api.x.ai", "assets.grok.com");
	AZ_DOMAINS(DomainsPerplexity, "perplexity.ai", "www.perplexity.ai");
	AZ_DOMAINS(DomainsMidjourney, "midjourney.com", "cdn.midjourney.com");
	AZ_DOMAINS(DomainsCharacterAi, "character.ai", "beta.character.ai", "characterai.io");
	AZ_DOMAINS(DomainsGroq, "groq.com", "console.groq.com", "api.groq.com");
	AZ_DOMAINS(DomainsElevenlabs, "elevenlabs.io", "api.elevenlabs.io");
	AZ_DOMAINS(DomainsRunway, "runwayml.com", "app.runwayml.com");
	AZ_DOMAINS(DomainsLeonardo, "leonardo.ai", "cloud.leonardo.ai");
	AZ_DOMAINS(DomainsHuggingface, "huggingface.co", "hf.co", "cdn-lfs.huggingface.co");
	AZ_DOMAINS(DomainsPoe, "poe.com", "quora.com");
	AZ_DOMAINS(DomainsNotionAi, "notion.so", "notion.com", "www.notion.so");

	AZ_DOMAINS(DomainsGithub,
		"github.com", "githubusercontent.com", "github.io", "ghcr.io",
		"githubassets.com", "githubcopilot.com", "copilot.github.com");
	AZ_DOMAINS(DomainsGitlab, "gitlab.com", "gitlab.io", "gitlab.net");
	AZ_DOMAINS(DomainsBitbucket, "bitbucket.org", "atlassian.com", "atlassian.net", "bb-inf.net");
	AZ_DOMAINS(DomainsStackoverflow,
		"stackoverflow.com", "stackexchange.com", "serverfault.com", "superuser.com", "sstatic.net");
	AZ_DOMAINS(DomainsNpm, "npmjs.com", "npmjs.org", "registry.npmjs.org", "npm.pkg.github.com");
	AZ_DOMAINS(DomainsPypi, "pypi.org", "pythonhosted.org", "files.pythonhosted.org", "pypi.python.org");
	AZ_DOMAINS(DomainsCratesIo, "crates.io", "static.crates.io", "index.crates.io");
	AZ_DOMAINS(DomainsGolang, "go.dev", "pkg.go.dev", "proxy.golang.org", "sum.golang.org", "golang.org");
	AZ_DOMAINS(DomainsNuget, "nuget.org", "api.nuget.org", "nuget.azure.cn", "dot.net");
	AZ_DOMAINS(DomainsMaven, "maven.org", "repo1.maven.org", "central.sonatype.com", "search.maven.org");
	AZ_DOMAINS(DomainsDockerhub,
		"docker.com", "docker.io", "hub.docker.com", "registry-1.docker.io", "production.cloudflare.docker.com");
	AZ_DOMAINS(DomainsHashicorp, "hashicorp.com", "terraform.io", "releases.hashicorp.com", "registry.terraform.io");
	AZ_DOMAINS(DomainsAws, "amazonaws.com", "aws.amazon.com", "cloudfront.net", "amazon.com");
	AZ_DOMAINS(DomainsGcp, "cloud.google.com", "googleapis.com", "gcr.io", "appspot.com", "gstatic.com");
	AZ_DOMAINS(DomainsAzure,
		"azure.com", "azureedge.net", "microsoftonline.com", "visualstudio.com", "azurewebsites.net");
	AZ_DOMAINS(DomainsCloudflare, "cloudflare.com", "workers.dev", "r2.dev", "pages.dev", "cloudflareinsights.com");
	AZ_DOMAINS(DomainsVercel, "vercel.com", "vercel.app", "now.sh", "zeit.co");
	AZ_DOMAINS(DomainsNetlify, "netlify.com", "netlify.app", "netlifyusercontent.com");
	AZ_DOMAINS(DomainsDigitalocean, "digitalocean.com", "digitaloceanspaces.com", "ondigitalocean.app");
	AZ_DOMAINS(DomainsHeroku, "heroku.com", "herokuapp.com", "herokucdn.com");
	AZ_DOMAINS(DomainsRender, "render.com", "onrender.com");
	AZ_DOMAINS(DomainsRailway, "railway.app", "railway.com", "up.railway.app");
	AZ_DOMAINS(DomainsSupabase, "supabase.com", "supabase.co");
	AZ_DOMAINS(DomainsFirebase, "firebase.google.com", "firebaseio.com", "firebaseapp.com", "firebasestorage.app");
	AZ_DOMAINS(DomainsMongodbAtlas, "mongodb.com", "mongodb.net", "mongodb.org");
	AZ_DOMAINS(DomainsPlanetscale, "planetscale.com");
	AZ_DOMAINS(DomainsReplit, "replit.com", "repl.co", "replit.dev");
	AZ_DOMAINS(DomainsCodepen, "codepen.io", "cpwebassets.codepen.io");
	AZ_DOMAINS(DomainsJsfiddle, "jsfiddle.net");
	AZ_DOMAINS(DomainsCodesandbox, "codesandbox.io", "csb.app", "codesandbox.com");
	AZ_DOMAINS(DomainsStackblitz, "stackblitz.com", "webcontainer.io", "blitz.codes");
	AZ_DOMAINS(DomainsCursorIde, "cursor.com", "cursor.sh", "api2.cursor.sh");
	AZ_DOMAINS(DomainsJetbrains, "jetbrains.com", "jb.gg", "jetbrains.space", "intellij.net");
	AZ_DOMAINS(DomainsVscode,
		"code.visualstudio.com", "marketplace.visualstudio.com", "vscode.dev", "vscode-cdn.net");
	AZ_DOMAINS(DomainsSourceforge, "sourceforge.net", "sf.net", "fsdn.com");
	AZ_DOMAINS(DomainsGist, "gist.github.com", "githubusercontent.com");
	AZ_DOMAINS(DomainsGitkraken, "gitkraken.com", "axosoft.com", "gitkraken.dev");
	AZ_DOMAINS(DomainsPostman, "postman.com", "getpostman.com", "postman.co");
	AZ_DOMAINS(DomainsSentry, "sentry.io", "sentry-cdn.com");
	AZ_DOMAINS(DomainsDatadog, "datadoghq.com", "datadoghq.eu", "ddog-gov.com");
	AZ_DOMAINS(DomainsGrafana, "grafana.com", "grafana.net", "grafana.org");
	AZ_DOMAINS(DomainsOpenaiApi, "platform.openai.com", "api.openai.com", "openai.com");

	AZ_DOMAINS(DomainsSteam,
		"steampowered.com", "steamcommunity.com", "steamgames.com", "steamusercontent.com",
		"steamcontent.com", "steamstatic.com", "steam-chat.com", "steamstat.us");
	AZ_DOMAINS(DomainsEpic,
		"epicgames.com", "unrealengine.com", "fortnite.com", "epicgames.dev",
		"ol.epicgames.com", "epicgamesstore.com");
	AZ_DOMAINS(DomainsBattlenet,
		"battle.net", "blizzard.com", "blizzardgames.com", "battlenet.com.cn");
	AZ_DOMAINS(DomainsEaApp,
		"ea.com", "origin.com", "eaorigins.com", "easports.com", "tnt-ea.com");
	AZ_DOMAINS(DomainsUbisoft, "ubisoft.com", "ubi.com", "uplay.com", "ubisoftconnect.com");
	AZ_DOMAINS(DomainsRockstar, "rockstargames.com", "rockstar.com", "socialclub.rockstargames.com");
	AZ_DOMAINS(DomainsGog, "gog.com", "gog-statics.com", "goggalaxypresence");
	AZ_DOMAINS(DomainsRiot,
		"riotgames.com", "riotcdn.net", "pvp.net", "leagueoflegends.com", "valorant.com");
	AZ_DOMAINS(DomainsXbox,
		"xbox.com", "xboxlive.com", "xboxservices.com", "gamepass.com", "microsoft.com");
	AZ_DOMAINS(DomainsItch, "itch.io", "itch.zone");

	AZ_DOMAINS(DomainsSigame,
		"vladimirkhil.com", "www.vladimirkhil.com", "sigame.ru", "si.vladimirkhil.com");
	AZ_DOMAINS(DomainsGenshin,
		"hoyoverse.com", "mihoyo.com", "yuanshen.com", "genshin.hoyoverse.com",
		"hk4e-api.hoyoverse.com", "account.hoyoverse.com");
	AZ_DOMAINS(DomainsHonkai,
		"hoyoverse.com", "mihoyo.com", "starrail.hoyoverse.com", "hkrpg-api.hoyoverse.com");
	AZ_DOMAINS(DomainsZzz,
		"hoyoverse.com", "mihoyo.com", "zenless.hoyoverse.com", "nap-api.hoyoverse.com");
	AZ_DOMAINS(DomainsValorant, "playvalorant.com", "valorant.riotgames.com", "riotgames.com");
	AZ_DOMAINS(DomainsLeague, "leagueoflegends.com", "riotgames.com", "pvp.net");
	AZ_DOMAINS(DomainsFortnite, "fortnite.com", "epicgames.com", "ol.epicgames.com");
	AZ_DOMAINS(DomainsRoblox, "roblox.com", "rbxcdn.com", "roblox.qq.com");
	AZ_DOMAINS(DomainsMinecraft,
		"minecraft.net", "mojang.com", "minecraftservices.com", "xboxlive.com");
	AZ_DOMAINS(DomainsFaceit, "faceit.com", "faceitusercontent.com");
	AZ_DOMAINS(DomainsApex, "ea.com", "origin.com", "easports.com");
	AZ_DOMAINS(DomainsWarzone, "callofduty.com", "activision.com", "battle.net");
	AZ_DOMAINS(DomainsWot, "wargaming.net", "worldoftanks.eu", "worldofwarships.com", "wgcdn.co");

	AZ_DOMAINS(DomainsCs2, "counter-strike.net", "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsDota2, "dota2.com", "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsPubg, "pubg.com", "krafton.com", "pubgusercontent.com");
	AZ_DOMAINS(DomainsOverwatch, "overwatch.blizzard.com", "blizzard.com", "battle.net");
	AZ_DOMAINS(DomainsWow, "worldofwarcraft.com", "blizzard.com", "battle.net");
	AZ_DOMAINS(DomainsDiablo, "diablo.blizzard.com", "blizzard.com", "battle.net");
	AZ_DOMAINS(DomainsHearthstone, "playhearthstone.com", "blizzard.com", "battle.net");
	AZ_DOMAINS(DomainsPathOfExile, "pathofexile.com", "poecdn.com");
	AZ_DOMAINS(DomainsDestiny2, "bungie.net", "destinythegame.com");
	AZ_DOMAINS(DomainsGta5, "rockstargames.com", "socialclub.rockstargames.com");
	AZ_DOMAINS(DomainsRdr2, "rockstargames.com", "socialclub.rockstargames.com");
	AZ_DOMAINS(DomainsCyberpunk, "cyberpunk.net", "cdprojektred.com");
	AZ_DOMAINS(DomainsEldenRing, "bandainamcoent.com", "eldenring.com");
	AZ_DOMAINS(DomainsBg3, "baldursgate3.game", "larian.com");
	AZ_DOMAINS(DomainsHelldivers2, "arrowheadgamestudios.com", "helldivers2.com");
	AZ_DOMAINS(DomainsPalworld, "palworldgame.com", "pocketpair.jp");
	AZ_DOMAINS(DomainsRust, "rust.facepunch.com", "facepunch.com");
	AZ_DOMAINS(DomainsArk, "playark.com", "survivetheark.com");
	AZ_DOMAINS(DomainsTarkov, "escapefromtarkov.com", "battlestategames.com");
	AZ_DOMAINS(DomainsWarthunder, "warthunder.com", "gaijin.net");
	AZ_DOMAINS(DomainsFc25, "ea.com", "easports.com", "origin.com");
	AZ_DOMAINS(DomainsNba2k, "nba.com", "2k.com", "nba2k.com");
	AZ_DOMAINS(DomainsRocketLeague, "rocketleague.com", "epicgames.com");
	AZ_DOMAINS(DomainsDbd, "deadbydaylight.com", "bhvr.com");
	AZ_DOMAINS(DomainsAmongUs, "innersloth.com", "among.us");
	AZ_DOMAINS(DomainsTerraria, "terraria.org", "steampowered.com");
	AZ_DOMAINS(DomainsStardew, "stardewvalley.net");
	AZ_DOMAINS(DomainsSims4, "ea.com", "origin.com");
	AZ_DOMAINS(DomainsMhWilds, "monsterhunter.com", "capcom.com");
	AZ_DOMAINS(DomainsFfxiv, "finalfantasyxiv.com", "square-enix.com");
	AZ_DOMAINS(DomainsLostArk, "playlostark.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsNewWorld, "newworld.com", "amazon.com");
	AZ_DOMAINS(DomainsDeadlock, "playdeadlock.com", "steampowered.com");
	AZ_DOMAINS(DomainsTf2, "teamfortress.com", "steampowered.com");
	AZ_DOMAINS(DomainsFallGuys, "fallguys.com", "epicgames.com");
	AZ_DOMAINS(DomainsBrawlhalla, "brawlhalla.com", "ubisoft.com");
	AZ_DOMAINS(DomainsSmite, "smitegame.com", "hirezstudios.com");
	AZ_DOMAINS(DomainsOsu, "osu.ppy.sh", "ppy.sh");
	AZ_DOMAINS(DomainsVrchat, "vrchat.com", "vrchat.cloud");
	AZ_DOMAINS(DomainsPhasmophobia, "kineticgames.co.uk", "kineticgames.com");
	AZ_DOMAINS(DomainsWarface, "warface.com", "my.games", "mail.ru");
	AZ_DOMAINS(DomainsCrossout, "crossout.net", "my.games");
	AZ_DOMAINS(DomainsAlbion, "albiononline.com", "albiononline2d.com");
	AZ_DOMAINS(DomainsEve, "eveonline.com", "ccpgames.com");
	AZ_DOMAINS(DomainsHunt, "huntshowdown.com", "crytek.com");

	AZ_DOMAINS(DomainsMarvelRivals, "marvelrivals.com", "neteasegames.com", "netease.com");
	AZ_DOMAINS(DomainsDeltaForce, "deltaforcegame.com", "garena.com", "tidalgames.com");
	AZ_DOMAINS(DomainsPeak, "steampowered.com", "steamcommunity.com", "steamusercontent.com");
	AZ_DOMAINS(DomainsRepo, "steampowered.com", "steamcommunity.com", "steamusercontent.com");
	AZ_DOMAINS(DomainsArcRaiders, "arcraiders.com", "embark.games", "embark-studios.com");
	AZ_DOMAINS(DomainsScheduleI, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsPoe2, "pathofexile.com", "pathofexile2.com", "poecdn.com");
	AZ_DOMAINS(DomainsThroneLiberty, "playthroneandliberty.com", "throneandliberty.com", "ncsoft.com");
	AZ_DOMAINS(DomainsOnceHuman, "oncehuman.game", "neteasegames.com");
	AZ_DOMAINS(DomainsFragpunk, "fragpunk.com", "happyelements.com");
	AZ_DOMAINS(DomainsTheFinals, "reachthefinals.com", "embark.games", "embark-studios.com");
	AZ_DOMAINS(DomainsBattlefield, "ea.com", "origin.com", "battlefield.com");
	AZ_DOMAINS(DomainsAbioticFactor, "abioticfactor.com", "deeprockdigital.com");
	AZ_DOMAINS(DomainsContentWarning, "landfall.se", "steampowered.com");
	AZ_DOMAINS(DomainsLethalCompany, "zeekerss.com", "steampowered.com");
	AZ_DOMAINS(DomainsPartyAnimals, "partyanimalsgame.com", "recotechnology.com");
	AZ_DOMAINS(DomainsSpiritvale, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsRagnarokTnw, "steampowered.com", "gravity.co.kr", "ragnaroktheneworld.com");
	AZ_DOMAINS(DomainsZerospace, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsSoulbound, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsCarnivalHunt, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsCodenameCure2, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsGunXGunner, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsOuchCargo, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsFunnelRunners, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsGalleyMound, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsBodycamOnrecord, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsHaloEvolved, "halowaypoint.com", "xbox.com", "xboxlive.com");
	AZ_DOMAINS(DomainsNightreign, "bandainamcoent.com", "eldenring.com", "fromsoftware.jp");
	AZ_DOMAINS(DomainsUmamusume, "umamusume.com", "cygames.co.jp", "steampowered.com");
	AZ_DOMAINS(DomainsMecchaChameleon, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsDifficult, "steampowered.com", "steamcommunity.com");
	AZ_DOMAINS(DomainsWarena, "steampowered.com", "steamcommunity.com");

	AZ_DOMAINS(DomainsTorrents, "rutracker.org", "rutracker.net", "thepiratebay.org");
	AZ_DOMAINS(DomainsWindows, "windowsupdate.com", "update.microsoft.com", "microsoft.com", "windows.com");
	AZ_DOMAINS(DomainsYandexBrowser,
		"yandex.ru", "yandex.com", "yandex.net", "ya.ru", "yastatic.net", "yandex.st",
		"browser.yandex.ru", "api.browser.yandex.net", "sync.browser.yandex.net",
		"mc.yandex.ru", "yandexmetrica.com", "metrica.yandex.ru", "strm.yandex.ru");
	AZ_DOMAINS(DomainsAtomBrowser, "mail.ru", "vk.com", "vk.ru");
	AZ_DOMAINS(DomainsYandex,
		"yandex.ru", "yandex.com", "yandex.net", "ya.ru", "yastatic.net", "yandex.st",
		"mc.yandex.ru", "yandexmetrica.com", "metrica.yandex.ru", "strm.yandex.ru");
	AZ_DOMAINS(DomainsVk, "vk.com", "vk.ru", "vkuser.net", "vkplay.ru", "userapi.com");
	AZ_DOMAINS(DomainsMailru, "mail.ru", "my.mail.ru", "cloud.mail.ru");
	AZ_DOMAINS(DomainsOk, "ok.ru", "odnoklassniki.ru", "odkl.ru");
	AZ_DOMAINS(DomainsMax, "max.ru");
	AZ_DOMAINS(DomainsSberbank, "sberbank.ru", "sber.ru", "online.sberbank.ru");
	AZ_DOMAINS(DomainsTinkoff, "tbank.ru", "tinkoff.ru");
	AZ_DOMAINS(DomainsVtb, "vtb.ru", "online.vtb.ru");
	AZ_DOMAINS(DomainsAlfabank, "alfabank.ru", "alfa.me");
	AZ_DOMAINS(DomainsYoomoney, "yoomoney.ru", "yookassa.ru");
	AZ_DOMAINS(DomainsGazprombank, "gazprombank.ru", "gpb.ru");
	AZ_DOMAINS(DomainsSovcombank, "sovcombank.ru", "halvacard.ru");
	AZ_DOMAINS(DomainsRaiffeisen, "raiffeisen.ru", "online.raiffeisen.ru");
	AZ_DOMAINS(DomainsRosbank, "rosbank.ru", "online.rosbank.ru");
	AZ_DOMAINS(DomainsMtsbank, "mtsbank.ru", "mtsdengi.ru");
	AZ_DOMAINS(DomainsGosuslugi, "gosuslugi.ru", "esia.gosuslugi.ru", "lk.gosuslugi.ru");
	AZ_DOMAINS(DomainsNalog, "nalog.gov.ru", "lkfl2.nalog.ru", "gov.ru");
	AZ_DOMAINS(DomainsMosRu, "mos.ru", "my.mos.ru", "uslugi.mos.ru");
	AZ_DOMAINS(DomainsPochtaRu, "pochta.ru", "tracking.pochta.ru");
	AZ_DOMAINS(DomainsWildberries, "wildberries.ru", "wb.ru");
	AZ_DOMAINS(DomainsOzon, "ozon.ru", "ozon.app");
	AZ_DOMAINS(DomainsAvito, "avito.ru", "avito.st");
	AZ_DOMAINS(DomainsMegamarket, "megamarket.ru", "sbermegamarket.ru");
	AZ_DOMAINS(DomainsDnsShop, "dns-shop.ru", "dns-shop.net");
	AZ_DOMAINS(DomainsMvideo, "mvideo.ru", "eldorado.ru");
	AZ_DOMAINS(DomainsCitilink, "citilink.ru");
	AZ_DOMAINS(DomainsLamoda, "lamoda.ru", "lamoda.co");
	AZ_DOMAINS(DomainsGoldapple, "goldapple.ru", "gacdn.ru");
	AZ_DOMAINS(DomainsVkusvill, "vkusvill.ru", "online.vkusvill.ru");
	AZ_DOMAINS(DomainsPyaterochka, "5ka.ru", "perekrestok.ru", "x5.ru");
	AZ_DOMAINS(DomainsYandexGo, "taxi.yandex.ru", "eda.yandex.ru", "lavka.yandex.ru");
	AZ_DOMAINS(DomainsSamokat, "samokat.ru");
	AZ_DOMAINS(DomainsCdek, "cdek.ru", "lk.cdek.ru");
	AZ_DOMAINS(DomainsDeliveryClub, "delivery-club.ru");
	AZ_DOMAINS(DomainsMts, "mts.ru", "mymts.ru", "lk.mts.ru");
	AZ_DOMAINS(DomainsMegafon, "megafon.ru", "lk.megafon.ru");
	AZ_DOMAINS(DomainsBeeline, "beeline.ru", "my.beeline.ru");
	AZ_DOMAINS(DomainsTele2, "tele2.ru", "t2.ru", "my.tele2.ru");
	AZ_DOMAINS(DomainsRostelecom, "rt.ru", "rostelecom.ru", "lk.rt.ru");
	AZ_DOMAINS(DomainsRutube, "rutube.ru", "static.rutube.ru");
	AZ_DOMAINS(DomainsOkko, "okko.tv", "api.okko.tv");
	AZ_DOMAINS(DomainsIvi, "ivi.ru", "ivi.tv", "api.ivi.ru");
	AZ_DOMAINS(DomainsKion, "kion.ru", "api.kion.ru");
	AZ_DOMAINS(DomainsWink, "wink.ru", "api.wink.ru");
	AZ_DOMAINS(DomainsStart, "start.ru", "start.video");
	AZ_DOMAINS(DomainsPremier, "premier.one", "api.premier.one");
	AZ_DOMAINS(DomainsZvuk, "zvuk.com", "sberaudio.ru");
	AZ_DOMAINS(DomainsGis2, "2gis.ru", "2gis.com", "2gis.biz");
	AZ_DOMAINS(DomainsRzd, "rzd.ru", "ticket.rzd.ru", "pass.rzd.ru");
	AZ_DOMAINS(DomainsAeroflot, "aeroflot.ru", "booking.aeroflot.ru");
	AZ_DOMAINS(DomainsS7, "s7.ru", "api.s7.ru");
	AZ_DOMAINS(DomainsTutu, "tutu.ru", "api.tutu.ru");
	AZ_DOMAINS(DomainsAviasales, "aviasales.ru", "aviasales.com");
	AZ_DOMAINS(DomainsCian, "cian.ru", "api.cian.ru");
	AZ_DOMAINS(DomainsDomclick, "domclick.ru", "api.domclick.ru");
	AZ_DOMAINS(DomainsAutoRu, "auto.ru", "api.auto.ru");
	AZ_DOMAINS(DomainsDrom, "drom.ru", "auto.drom.ru");
	AZ_DOMAINS(DomainsHh, "hh.ru", "headhunter.ru");
	AZ_DOMAINS(DomainsSuperjob, "superjob.ru");
	AZ_DOMAINS(DomainsHabr, "habr.com", "career.habr.com");
	AZ_DOMAINS(DomainsSberhealth, "sberhealth.ru", "doctoronline.ru");
	AZ_DOMAINS(DomainsInvitro, "invitro.ru", "lk.invitro.ru");
	AZ_DOMAINS(DomainsApteka, "apteka.ru", "eapteka.ru");
	AZ_DOMAINS(DomainsKaspersky, "kaspersky.ru", "kaspersky.com");
	AZ_DOMAINS(DomainsDrweb, "drweb.ru", "drweb.com");
	AZ_DOMAINS(DomainsRustore, "rustore.ru");

#undef AZ_DOMAINS

	template<size_t N>
	constexpr DomainEntry MakeEntry(const char* const (&domains)[N])
	{
		return { domains, N };
	}

	const std::unordered_map<std::string, DomainEntry>& FallbackMap()
	{
		static const std::unordered_map<std::string, DomainEntry> map = {
			{ "2ip", MakeEntry(kDomains2ip) },
			{ "youtube", MakeEntry(kDomainsYoutube) },
			{ "discord", MakeEntry(kDomainsDiscord) },
			{ "telegram", MakeEntry(kDomainsTelegram) },
			{ "instagram", MakeEntry(kDomainsInstagram) },
			{ "facebook", MakeEntry(kDomainsFacebook) },
			{ "twitter", MakeEntry(kDomainsTwitter) },
			{ "tiktok", MakeEntry(kDomainsTiktok) },
			{ "whatsapp", MakeEntry(kDomainsWhatsapp) },
			{ "linkedin", MakeEntry(kDomainsLinkedin) },
			{ "reddit", MakeEntry(kDomainsReddit) },
			{ "twitch", MakeEntry(kDomainsTwitch) },
			{ "soundcloud", MakeEntry(kDomainsSoundcloud) },
			{ "spotify", MakeEntry(kDomainsSpotify) },
			{ "netflix", MakeEntry(kDomainsNetflix) },
			{ "chrome", MakeEntry(kDomainsChrome) },
			{ "firefox", MakeEntry(kDomainsFirefox) },
			{ "edge", MakeEntry(kDomainsEdge) },
			{ "opera", MakeEntry(kDomainsOpera) },
			{ "brave", MakeEntry(kDomainsBrave) },
			{ "chatgpt", MakeEntry(kDomainsChatgpt) },
			{ "claude", MakeEntry(kDomainsClaude) },
			{ "gemini", MakeEntry(kDomainsGemini) },
			{ "copilot", MakeEntry(kDomainsCopilot) },
			{ "grok", MakeEntry(kDomainsGrok) },
			{ "perplexity", MakeEntry(kDomainsPerplexity) },
			{ "midjourney", MakeEntry(kDomainsMidjourney) },
			{ "character_ai", MakeEntry(kDomainsCharacterAi) },
			{ "groq", MakeEntry(kDomainsGroq) },
			{ "elevenlabs", MakeEntry(kDomainsElevenlabs) },
			{ "runway", MakeEntry(kDomainsRunway) },
			{ "leonardo", MakeEntry(kDomainsLeonardo) },
			{ "huggingface", MakeEntry(kDomainsHuggingface) },
			{ "poe", MakeEntry(kDomainsPoe) },
			{ "notion_ai", MakeEntry(kDomainsNotionAi) },
			{ "github", MakeEntry(kDomainsGithub) },
			{ "gitlab", MakeEntry(kDomainsGitlab) },
			{ "bitbucket", MakeEntry(kDomainsBitbucket) },
			{ "stackoverflow", MakeEntry(kDomainsStackoverflow) },
			{ "npm", MakeEntry(kDomainsNpm) },
			{ "pypi", MakeEntry(kDomainsPypi) },
			{ "crates_io", MakeEntry(kDomainsCratesIo) },
			{ "golang", MakeEntry(kDomainsGolang) },
			{ "nuget", MakeEntry(kDomainsNuget) },
			{ "maven", MakeEntry(kDomainsMaven) },
			{ "dockerhub", MakeEntry(kDomainsDockerhub) },
			{ "hashicorp", MakeEntry(kDomainsHashicorp) },
			{ "aws", MakeEntry(kDomainsAws) },
			{ "gcp", MakeEntry(kDomainsGcp) },
			{ "azure", MakeEntry(kDomainsAzure) },
			{ "cloudflare", MakeEntry(kDomainsCloudflare) },
			{ "vercel", MakeEntry(kDomainsVercel) },
			{ "netlify", MakeEntry(kDomainsNetlify) },
			{ "digitalocean", MakeEntry(kDomainsDigitalocean) },
			{ "heroku", MakeEntry(kDomainsHeroku) },
			{ "render", MakeEntry(kDomainsRender) },
			{ "railway", MakeEntry(kDomainsRailway) },
			{ "supabase", MakeEntry(kDomainsSupabase) },
			{ "firebase", MakeEntry(kDomainsFirebase) },
			{ "mongodb_atlas", MakeEntry(kDomainsMongodbAtlas) },
			{ "planetscale", MakeEntry(kDomainsPlanetscale) },
			{ "replit", MakeEntry(kDomainsReplit) },
			{ "codepen", MakeEntry(kDomainsCodepen) },
			{ "jsfiddle", MakeEntry(kDomainsJsfiddle) },
			{ "codesandbox", MakeEntry(kDomainsCodesandbox) },
			{ "stackblitz", MakeEntry(kDomainsStackblitz) },
			{ "cursor_ide", MakeEntry(kDomainsCursorIde) },
			{ "jetbrains", MakeEntry(kDomainsJetbrains) },
			{ "vscode", MakeEntry(kDomainsVscode) },
			{ "sourceforge", MakeEntry(kDomainsSourceforge) },
			{ "gist", MakeEntry(kDomainsGist) },
			{ "gitkraken", MakeEntry(kDomainsGitkraken) },
			{ "postman", MakeEntry(kDomainsPostman) },
			{ "sentry", MakeEntry(kDomainsSentry) },
			{ "datadog", MakeEntry(kDomainsDatadog) },
			{ "grafana", MakeEntry(kDomainsGrafana) },
			{ "openai_api", MakeEntry(kDomainsOpenaiApi) },
			{ "steam", MakeEntry(kDomainsSteam) },
			{ "epic", MakeEntry(kDomainsEpic) },
			{ "battlenet", MakeEntry(kDomainsBattlenet) },
			{ "ea_app", MakeEntry(kDomainsEaApp) },
			{ "ubisoft", MakeEntry(kDomainsUbisoft) },
			{ "rockstar", MakeEntry(kDomainsRockstar) },
			{ "gog", MakeEntry(kDomainsGog) },
			{ "riot", MakeEntry(kDomainsRiot) },
			{ "xbox", MakeEntry(kDomainsXbox) },
			{ "itch", MakeEntry(kDomainsItch) },
			{ "sigame", MakeEntry(kDomainsSigame) },
			{ "genshin", MakeEntry(kDomainsGenshin) },
			{ "honkai", MakeEntry(kDomainsHonkai) },
			{ "zzz", MakeEntry(kDomainsZzz) },
			{ "valorant", MakeEntry(kDomainsValorant) },
			{ "league", MakeEntry(kDomainsLeague) },
			{ "fortnite", MakeEntry(kDomainsFortnite) },
			{ "roblox", MakeEntry(kDomainsRoblox) },
			{ "minecraft", MakeEntry(kDomainsMinecraft) },
			{ "faceit", MakeEntry(kDomainsFaceit) },
			{ "apex", MakeEntry(kDomainsApex) },
			{ "warzone", MakeEntry(kDomainsWarzone) },
			{ "wot", MakeEntry(kDomainsWot) },
			{ "cs2", MakeEntry(kDomainsCs2) },
			{ "dota2", MakeEntry(kDomainsDota2) },
			{ "pubg", MakeEntry(kDomainsPubg) },
			{ "overwatch", MakeEntry(kDomainsOverwatch) },
			{ "wow", MakeEntry(kDomainsWow) },
			{ "diablo", MakeEntry(kDomainsDiablo) },
			{ "hearthstone", MakeEntry(kDomainsHearthstone) },
			{ "path_of_exile", MakeEntry(kDomainsPathOfExile) },
			{ "destiny2", MakeEntry(kDomainsDestiny2) },
			{ "gta5", MakeEntry(kDomainsGta5) },
			{ "rdr2", MakeEntry(kDomainsRdr2) },
			{ "cyberpunk", MakeEntry(kDomainsCyberpunk) },
			{ "elden_ring", MakeEntry(kDomainsEldenRing) },
			{ "bg3", MakeEntry(kDomainsBg3) },
			{ "helldivers2", MakeEntry(kDomainsHelldivers2) },
			{ "palworld", MakeEntry(kDomainsPalworld) },
			{ "rust", MakeEntry(kDomainsRust) },
			{ "ark", MakeEntry(kDomainsArk) },
			{ "tarkov", MakeEntry(kDomainsTarkov) },
			{ "warthunder", MakeEntry(kDomainsWarthunder) },
			{ "fc25", MakeEntry(kDomainsFc25) },
			{ "nba2k", MakeEntry(kDomainsNba2k) },
			{ "rocket_league", MakeEntry(kDomainsRocketLeague) },
			{ "dbd", MakeEntry(kDomainsDbd) },
			{ "among_us", MakeEntry(kDomainsAmongUs) },
			{ "terraria", MakeEntry(kDomainsTerraria) },
			{ "stardew", MakeEntry(kDomainsStardew) },
			{ "sims4", MakeEntry(kDomainsSims4) },
			{ "mh_wilds", MakeEntry(kDomainsMhWilds) },
			{ "ffxiv", MakeEntry(kDomainsFfxiv) },
			{ "lost_ark", MakeEntry(kDomainsLostArk) },
			{ "new_world", MakeEntry(kDomainsNewWorld) },
			{ "deadlock", MakeEntry(kDomainsDeadlock) },
			{ "tf2", MakeEntry(kDomainsTf2) },
			{ "fall_guys", MakeEntry(kDomainsFallGuys) },
			{ "brawlhalla", MakeEntry(kDomainsBrawlhalla) },
			{ "smite", MakeEntry(kDomainsSmite) },
			{ "osu", MakeEntry(kDomainsOsu) },
			{ "vrchat", MakeEntry(kDomainsVrchat) },
			{ "phasmophobia", MakeEntry(kDomainsPhasmophobia) },
			{ "warface", MakeEntry(kDomainsWarface) },
			{ "crossout", MakeEntry(kDomainsCrossout) },
			{ "albion", MakeEntry(kDomainsAlbion) },
			{ "eve", MakeEntry(kDomainsEve) },
			{ "hunt", MakeEntry(kDomainsHunt) },
			{ "marvel_rivals", MakeEntry(kDomainsMarvelRivals) },
			{ "delta_force", MakeEntry(kDomainsDeltaForce) },
			{ "peak", MakeEntry(kDomainsPeak) },
			{ "repo", MakeEntry(kDomainsRepo) },
			{ "arc_raiders", MakeEntry(kDomainsArcRaiders) },
			{ "schedule_i", MakeEntry(kDomainsScheduleI) },
			{ "poe2", MakeEntry(kDomainsPoe2) },
			{ "throne_liberty", MakeEntry(kDomainsThroneLiberty) },
			{ "once_human", MakeEntry(kDomainsOnceHuman) },
			{ "fragpunk", MakeEntry(kDomainsFragpunk) },
			{ "the_finals", MakeEntry(kDomainsTheFinals) },
			{ "battlefield", MakeEntry(kDomainsBattlefield) },
			{ "abiotic_factor", MakeEntry(kDomainsAbioticFactor) },
			{ "content_warning", MakeEntry(kDomainsContentWarning) },
			{ "lethal_company", MakeEntry(kDomainsLethalCompany) },
			{ "party_animals", MakeEntry(kDomainsPartyAnimals) },
			{ "spiritvale", MakeEntry(kDomainsSpiritvale) },
			{ "ragnarok_tnw", MakeEntry(kDomainsRagnarokTnw) },
			{ "zerospace", MakeEntry(kDomainsZerospace) },
			{ "soulbound", MakeEntry(kDomainsSoulbound) },
			{ "carnival_hunt", MakeEntry(kDomainsCarnivalHunt) },
			{ "codename_cure2", MakeEntry(kDomainsCodenameCure2) },
			{ "gun_x_gunner", MakeEntry(kDomainsGunXGunner) },
			{ "ouch_cargo", MakeEntry(kDomainsOuchCargo) },
			{ "funnel_runners", MakeEntry(kDomainsFunnelRunners) },
			{ "galley_mound", MakeEntry(kDomainsGalleyMound) },
			{ "bodycam_onrecord", MakeEntry(kDomainsBodycamOnrecord) },
			{ "halo_evolved", MakeEntry(kDomainsHaloEvolved) },
			{ "nightreign", MakeEntry(kDomainsNightreign) },
			{ "monster_hunter_wilds", MakeEntry(kDomainsMhWilds) },
			{ "umamusume", MakeEntry(kDomainsUmamusume) },
			{ "meccha_chameleon", MakeEntry(kDomainsMecchaChameleon) },
			{ "difficult", MakeEntry(kDomainsDifficult) },
			{ "warena", MakeEntry(kDomainsWarena) },
			{ "torrents", MakeEntry(kDomainsTorrents) },
			{ "windows", MakeEntry(kDomainsWindows) },
			{ "yandex_browser", MakeEntry(kDomainsYandexBrowser) },
			{ "atom_browser", MakeEntry(kDomainsAtomBrowser) },
			{ "yandex", MakeEntry(kDomainsYandex) },
			{ "vk", MakeEntry(kDomainsVk) },
			{ "mailru", MakeEntry(kDomainsMailru) },
			{ "ok", MakeEntry(kDomainsOk) },
			{ "max", MakeEntry(kDomainsMax) },
			{ "sberbank", MakeEntry(kDomainsSberbank) },
			{ "tinkoff", MakeEntry(kDomainsTinkoff) },
			{ "vtb", MakeEntry(kDomainsVtb) },
			{ "alfabank", MakeEntry(kDomainsAlfabank) },
			{ "yoomoney", MakeEntry(kDomainsYoomoney) },
			{ "gazprombank", MakeEntry(kDomainsGazprombank) },
			{ "sovcombank", MakeEntry(kDomainsSovcombank) },
			{ "raiffeisen", MakeEntry(kDomainsRaiffeisen) },
			{ "rosbank", MakeEntry(kDomainsRosbank) },
			{ "mtsbank", MakeEntry(kDomainsMtsbank) },
			{ "gosuslugi", MakeEntry(kDomainsGosuslugi) },
			{ "nalog", MakeEntry(kDomainsNalog) },
			{ "mos_ru", MakeEntry(kDomainsMosRu) },
			{ "pochta_ru", MakeEntry(kDomainsPochtaRu) },
			{ "wildberries", MakeEntry(kDomainsWildberries) },
			{ "ozon", MakeEntry(kDomainsOzon) },
			{ "avito", MakeEntry(kDomainsAvito) },
			{ "megamarket", MakeEntry(kDomainsMegamarket) },
			{ "dns_shop", MakeEntry(kDomainsDnsShop) },
			{ "mvideo", MakeEntry(kDomainsMvideo) },
			{ "citilink", MakeEntry(kDomainsCitilink) },
			{ "lamoda", MakeEntry(kDomainsLamoda) },
			{ "goldapple", MakeEntry(kDomainsGoldapple) },
			{ "vkusvill", MakeEntry(kDomainsVkusvill) },
			{ "pyaterochka", MakeEntry(kDomainsPyaterochka) },
			{ "yandex_go", MakeEntry(kDomainsYandexGo) },
			{ "samokat", MakeEntry(kDomainsSamokat) },
			{ "cdek", MakeEntry(kDomainsCdek) },
			{ "delivery_club", MakeEntry(kDomainsDeliveryClub) },
			{ "mts", MakeEntry(kDomainsMts) },
			{ "megafon", MakeEntry(kDomainsMegafon) },
			{ "beeline", MakeEntry(kDomainsBeeline) },
			{ "tele2", MakeEntry(kDomainsTele2) },
			{ "rostelecom", MakeEntry(kDomainsRostelecom) },
			{ "rutube", MakeEntry(kDomainsRutube) },
			{ "okko", MakeEntry(kDomainsOkko) },
			{ "ivi", MakeEntry(kDomainsIvi) },
			{ "kion", MakeEntry(kDomainsKion) },
			{ "wink", MakeEntry(kDomainsWink) },
			{ "start", MakeEntry(kDomainsStart) },
			{ "premier", MakeEntry(kDomainsPremier) },
			{ "zvuk", MakeEntry(kDomainsZvuk) },
			{ "gis2", MakeEntry(kDomainsGis2) },
			{ "rzd", MakeEntry(kDomainsRzd) },
			{ "aeroflot", MakeEntry(kDomainsAeroflot) },
			{ "s7", MakeEntry(kDomainsS7) },
			{ "tutu", MakeEntry(kDomainsTutu) },
			{ "aviasales", MakeEntry(kDomainsAviasales) },
			{ "cian", MakeEntry(kDomainsCian) },
			{ "domclick", MakeEntry(kDomainsDomclick) },
			{ "auto_ru", MakeEntry(kDomainsAutoRu) },
			{ "drom", MakeEntry(kDomainsDrom) },
			{ "hh", MakeEntry(kDomainsHh) },
			{ "superjob", MakeEntry(kDomainsSuperjob) },
			{ "habr", MakeEntry(kDomainsHabr) },
			{ "sberhealth", MakeEntry(kDomainsSberhealth) },
			{ "invitro", MakeEntry(kDomainsInvitro) },
			{ "apteka", MakeEntry(kDomainsApteka) },
			{ "kaspersky", MakeEntry(kDomainsKaspersky) },
			{ "drweb", MakeEntry(kDomainsDrweb) },
			{ "rustore", MakeEntry(kDomainsRustore) },
		};
		return map;
	}
}

void VpnServiceFallbackDomains::Collect(const std::string& serviceId, std::vector<std::string>& outDomains)
{
	const auto& map = FallbackMap();
	const auto it = map.find(serviceId);
	if (it == map.end())
		return;

	for (size_t i = 0; i < it->second.count; ++i)
	{
		const char* domain = it->second.domains[i];
		if (domain == nullptr || domain[0] == '\0')
			continue;
		outDomains.emplace_back(domain);
	}
}
