#include "vpn/vpn_adult_sites.h"

namespace
{
	struct AdultSiteDef
	{
		const char* id;
		const char* name;
		// Comma-separated DOMAIN-SUFFIX list (primary + common mirrors/CDNs).
		const char* domains;
	};

	void AppendDomainsCsv(const char* csv, std::vector<std::string>& outDomains)
	{
		if (!csv || !csv[0])
			return;

		const char* start = csv;
		while (*start)
		{
			while (*start == ' ' || *start == ',')
				++start;
			if (!*start)
				break;

			const char* end = start;
			while (*end && *end != ',')
				++end;

			const char* trimEnd = end;
			while (trimEnd > start && (trimEnd[-1] == ' ' || trimEnd[-1] == '\t'))
				--trimEnd;

			if (trimEnd > start)
				outDomains.emplace_back(start, trimEnd);
			start = end;
		}
	}

	// Mainstream + RU-search favorites (18+). Reflects sites that commonly rank for «порно».
	constexpr AdultSiteDef kSites[] = {
		{ "adult_pornhub", "Pornhub", "pornhub.com,phncdn.com,pornhub.org" },
		{ "adult_xvideos", "XVideos", "xvideos.com,xvideos-cdn.com,xvideos.red" },
		{ "adult_xnxx", "XNXX", "xnxx.com,xnxx-cdn.com" },
		{ "adult_xhamster", "xHamster", "xhamster.com,xhcdn.com" },
		{ "adult_redtube", "RedTube", "redtube.com,rdtcdn.com" },
		{ "adult_youporn", "YouPorn", "youporn.com,ypncdn.com" },
		{ "adult_spankbang", "SpankBang", "spankbang.com,sb-cd.com" },
		{ "adult_tube8", "Tube8", "tube8.com" },
		{ "adult_porn", "Porn.com", "porn.com" },
		{ "adult_beeg", "Beeg", "beeg.com" },
		{ "adult_eporner", "Eporner", "eporner.com" },
		{ "adult_hqporner", "HQPorner", "hqporner.com" },
		{ "adult_txxx", "TXXX", "txxx.com" },
		{ "adult_porntrex", "PornTrex", "porntrex.com" },
		{ "adult_youjizz", "YouJizz", "youjizz.com" },
		{ "adult_drtuber", "DrTuber", "drtuber.com" },
		{ "adult_tnaflix", "TNAFlix", "tnaflix.com" },
		{ "adult_ixxx", "iXXX", "ixxx.com" },
		{ "adult_pornone", "PornOne", "pornone.com" },
		{ "adult_porn300", "Porn300", "porn300.com" },
		{ "adult_xxxbunker", "XXXBunker", "xxxbunker.com" },
		{ "adult_4tube", "4Tube", "4tube.com" },
		{ "adult_porntube", "PornTube", "porntube.com" },
		{ "adult_porndoe", "PornDoe", "porndoe.com" },
		{ "adult_porngo", "PornGO", "porngo.com" },
		{ "adult_sxyprn", "SxyPrn", "sxyprn.com" },
		{ "adult_nuvid", "Nuvid", "nuvid.com" },
		{ "adult_keezmovies", "KeezMovies", "keezmovies.com" },
		{ "adult_spankwire", "SpankWire", "spankwire.com" },
		{ "adult_xtube", "XTube", "xtube.com" },
		{ "adult_thisvid", "ThisVid", "thisvid.com" },
		{ "adult_erome", "Erome", "erome.com" },
		{ "adult_redgifs", "RedGIFs", "redgifs.com" },
		{ "adult_motherless", "Motherless", "motherless.com" },
		{ "adult_imagefap", "ImageFap", "imagefap.com" },
		{ "adult_pornpics", "PornPics", "pornpics.com" },
		{ "adult_sex", "Sex.com", "sex.com" },
		{ "adult_okxxx", "OK.XXX", "ok.xxx" },
		{ "adult_sexvid", "SexVid", "sexvid.xxx" },
		{ "adult_xxxtik", "XXXTik", "xxxtik.com" },
		{ "adult_fapello", "Fapello", "fapello.com" },
		{ "adult_thothub", "ThotHub", "thothub.to,thothub.tv" },
		{ "adult_chaturbate", "Chaturbate", "chaturbate.com,highwebmedia.com" },
		{ "adult_stripchat", "Stripchat", "stripchat.com,strpst.com" },
		{ "adult_bongacams", "BongaCams", "bongacams.com,bngpt.com" },
		{ "adult_livejasmin", "LiveJasmin", "livejasmin.com,dditscdn.com" },
		{ "adult_cam4", "Cam4", "cam4.com" },
		{ "adult_myfreecams", "MyFreeCams", "myfreecams.com" },
		{ "adult_camsoda", "CamSoda", "camsoda.com" },
		{ "adult_streamate", "Streamate", "streamate.com" },
		{ "adult_imlive", "ImLive", "imlive.com" },
		{ "adult_xhamsterlive", "xHamster Live", "xhamsterlive.com" },
		{ "adult_onlyfans", "OnlyFans", "onlyfans.com,ofcdn.com" },
		{ "adult_fansly", "Fansly", "fansly.com" },
		{ "adult_manyvids", "ManyVids", "manyvids.com" },
		{ "adult_clips4sale", "Clips4Sale", "clips4sale.com" },
		{ "adult_faphouse", "FapHouse", "faphouse.com" },
		{ "adult_adulttime", "Adult Time", "adulttime.com" },
		{ "adult_brazzers", "Brazzers", "brazzers.com,brazzersnetwork.com" },
		{ "adult_bangbros", "BangBros", "bangbros.com" },
		{ "adult_realitykings", "Reality Kings", "realitykings.com" },
		{ "adult_naughtyamerica", "Naughty America", "naughtyamerica.com" },
		{ "adult_mofos", "Mofos", "mofos.com" },
		{ "adult_digitalplayground", "Digital Playground", "digitalplayground.com" },
		{ "adult_twistys", "Twistys", "twistys.com" },
		{ "adult_vixen", "Vixen", "vixen.com" },
		{ "adult_blacked", "BLACKED", "blacked.com" },
		{ "adult_tushy", "Tushy", "tushy.com" },
		{ "adult_evilangel", "Evil Angel", "evilangel.com" },
		{ "adult_teamskeet", "TeamSkeet", "teamskeet.com" },
		{ "adult_julesjordan", "Jules Jordan", "julesjordan.com" },
		{ "adult_propertysex", "Property Sex", "propertysex.com" },
		{ "adult_fakehub", "FakeHub", "fakehub.com" },
		{ "adult_bellesa", "Bellesa", "bellesa.co" },
		{ "adult_lustery", "Lustery", "lustery.com" },
		{ "adult_adultempire", "Adult Empire", "adultempire.com" },
		{ "adult_aebn", "AEBN", "aebn.com" },
		{ "adult_hotmovies", "HotMovies", "hotmovies.com" },
		{ "adult_pornhubpremium", "Pornhub Premium", "pornhubpremium.com" },
		{ "adult_literotica", "Literotica", "literotica.com" },
		{ "adult_ashemaletube", "aShemaleTube", "ashemaletube.com" },
		{ "adult_nhentai", "nhentai", "nhentai.net" },
		{ "adult_hitomi", "Hitomi", "hitomi.la" },
		{ "adult_hanime", "hanime.tv", "hanime.tv" },
		{ "adult_gelbooru", "Gelbooru", "gelbooru.com" },
		{ "adult_rule34", "Rule34", "rule34.xxx" },
		{ "adult_missav", "MissAV", "missav.com,missav.ws" },
		{ "adult_javlibrary", "JAVLibrary", "javlibrary.com" },
		{ "adult_javdb", "JAVDB", "javdb.com" },
		{ "adult_avgle", "Avgle", "avgle.com" },
		{ "adult_jizzbunker", "JizzBunker", "jizzbunker.com" },
		{ "adult_pornhat", "PornHat", "pornhat.com" },
		{ "adult_daftsex", "DaftSex", "daftsex.com" },
		{ "adult_pornhd", "PornHD", "pornhd.com" },
		{ "adult_epovr", "EPOVR", "epovr.com" },
		{ "adult_vrporn", "VRPorn", "vrporn.com" },
		{ "adult_adultfriendfinder", "AdultFriendFinder", "adultfriendfinder.com" },
		{ "adult_theporndude", "ThePornDude", "theporndude.com" },
		{ "adult_pornmd", "PornMD", "pornmd.com" },
		{ "adult_xmoviesforyou", "XMoviesForYou", "xmoviesforyou.com" },
		{ "adult_pornute", "Pornute", "pornute.com" },

		// --- Russian / CIS search favorites (often top for «порно») ---
		{ "adult_sex_studentki", "Секс Студентки", "sex-studentki.live,sex-studentki.pub,sex-studentki.best" },
		{ "adult_porno365", "Porno365", "porno365.xxx,porno365.bike,porno365.pics,porno365.su,porno365.plus,porno365.red,porno365.expert" },
		{ "adult_sosalkino", "Sosalkino", "sosalkino.tv,sosalkino.space,sosalkino.best,sosalkino.cam,sosalkino.top,sslkn.wiki" },
		{ "adult_pornosveta", "PornoSveta", "pornosveta.net,pornosveta.tv" },
		{ "adult_prostoporno", "ProstoPorno", "prostoporno.live,prostoporno.net" },
		{ "adult_pornk", "PornK", "pornk.com" },
		{ "adult_pornobaze", "PornoBaze", "pornobaze.com" },
		{ "adult_rusporno", "RusPorno", "rusporno.cc,rusporno.com" },
		{ "adult_sosushka", "Sosushka", "sosushka.one" },
		{ "adult_pornoblesk", "PornoBlesk", "pornoblesk.net" },
		{ "adult_pornolab", "PornoLab", "pornolab.net,pornolab.cc,pornolab.biz" },
		{ "adult_rusvideos", "RusVideos", "rusvideos.net" },
		{ "adult_pornomaniya", "Pornomaniya", "pornomaniya.xxx,pornomaniya.com" },
		{ "adult_tvoeporno", "TvoePorno", "tvoe.porn,tvoeporno.com" },
		{ "adult_24video", "24Video", "24video.site,24video.net" },
		{ "adult_pornomorda", "Pornomorda", "pornomorda.me" },
		{ "adult_pornovideo_name", "Pornovideo.name", "pornovideo.name" },
		{ "adult_x_fetish", "X-Fetish", "x-fetish.tube" },

		// --- Extra international tubes often in RU results ---
		{ "adult_hclips", "HClips", "hclips.com" },
		{ "adult_hotmovs", "HotMovs", "hotmovs.com" },
		{ "adult_upornia", "Upornia", "upornia.com" },
		{ "adult_voyeurhit", "VoyeurHit", "voyeurhit.com" },
		{ "adult_hdzog", "HDZog", "hdzog.com" },
		{ "adult_tubepornclassic", "TubePornClassic", "tubepornclassic.com" },
		{ "adult_pornzog", "PornZog", "pornzog.com" },
		{ "adult_zbporn", "ZBPorn", "zbporn.com" },
		{ "adult_analdin", "Analdin", "analdin.com" },
		{ "adult_xozilla", "Xozilla", "xozilla.com" },
		{ "adult_porndig", "PornDig", "porndig.com" },
		{ "adult_pornheed", "Pornheed", "pornheed.com" },
		{ "adult_pornoxo", "Pornoxo", "pornoxo.com" },
		{ "adult_heavyr", "Heavy-R", "heavy-r.com" },
		{ "adult_pornhits", "PornHits", "pornhits.com" },
		{ "adult_pornhoarder", "PornHoarder", "pornhoarder.tv" },
		{ "adult_yourdailyporn", "YourDailyPornVideos", "yourdailypornvideos.com" },
		{ "adult_veporn", "VePorn", "veporn.com" },
		{ "adult_pornobae", "PornoBae", "pornobae.com" },
		{ "adult_shortsxxx", "Shorts.xxx", "shorts.xxx" },
		{ "adult_leakxxx", "Leak.xxx", "leak.xxx" },
		{ "adult_luckycrush", "LuckyCrush", "luckycrush.live" },
		{ "adult_swaglive", "SWAG.live", "swag.live" },
		{ "adult_camwhores", "CamWhores", "camwhores.tv" },
		{ "adult_theyarehuge", "TheyAreHuge", "theyarehuge.com" },
		{ "adult_e_hentai", "E-Hentai", "e-hentai.org,exhentai.org" },
		{ "adult_sankaku", "Sankaku Complex", "sankakucomplex.com" },
		{ "adult_danbooru", "Danbooru", "donmai.us" },
		{ "adult_simpcity", "SimpCity", "simpcity.su" },
		{ "adult_undressxxx", "Undress.xxx", "undress.xxx" },
		{ "adult_baddiesxxx", "Baddies.xxx", "baddies.xxx" },
		{ "adult_pornktube", "PornKTube", "pornktube.com" },
		{ "adult_pornhd3x", "PornHD3x", "pornhd3x.net" },
		{ "adult_severeporn", "SeverePorn", "severeporn.com" },
		{ "adult_pornovideoshub", "PornoVideosHub", "pornovideoshub.com" },
		{ "adult_usersporn", "UsersPorn", "usersporn.com" },
		{ "adult_pornlib", "PornLib", "pornlib.com" },
		{ "adult_pornx", "PornX", "pornx.tube" },
		{ "adult_onlyporn", "OnlyPorn", "onlyporn.com" },
	};

	static_assert(sizeof(kSites) / sizeof(kSites[0]) >= 150, "Need a denser adult catalog");
}

bool VpnAdultSites::IsAdultServiceId(const std::string& serviceId)
{
	return serviceId.compare(0, 6, "adult_") == 0;
}

const std::vector<ServiceCatalogEntry>& VpnAdultSites::Catalog()
{
	static const std::vector<ServiceCatalogEntry> catalog = []
	{
		std::vector<ServiceCatalogEntry> out;
		out.reserve(sizeof(kSites) / sizeof(kSites[0]));
		for (const AdultSiteDef& site : kSites)
		{
			out.push_back({
				site.id,
				0xE8A5,
				false,
				site.name,
				site.domains,
				ServiceCatalogRegion::Foreign,
				ServiceCatalogSection::ForeignAdult,
			});
		}
		return out;
	}();
	return catalog;
}

void VpnAdultSites::CollectFallbackDomains(
	const std::string& serviceId,
	std::vector<std::string>& outDomains)
{
	for (const AdultSiteDef& site : kSites)
	{
		if (serviceId != site.id)
			continue;
		AppendDomainsCsv(site.domains, outDomains);
		return;
	}
}
