#include "zapret/strategy_descriptions.h"

#include "zapret/strategies.hpp"

namespace
{
	const char* kBullets_general_0[] = {
		"Базовая стратегия zapret-discord-youtube",
		"TCP: multisplit — разрез TLS ClientHello без fake-пакетов",
		"Discord/Google: split-seqovl=681, pos=1, google pattern",
		"General: split-seqovl=568, tls_clienthello_4pda_to.bin",
		"Google 443: ip-id=zero; UDP fake QUIC repeats=6/12",
		nullptr
	};
	const char* kBullets_general_alt_1[] = {
		"UDP/443 и Discord: подмена QUIC Initial готовыми .bin-пакетами (6 повторов)",
		"TCP: fake + fakedsplit — ложный TLS перед разрезанием реального ClientHello",
		"fooling=ts — подмена TCP timestamps, чтобы сбить stateful-отслеживание DPI",
		"fakedsplit-pattern=0x00 и статические fake TLS/HTTP (google.com, max.ru)",
		"Отдельные правила для discord.media, list-google и list-general",
		nullptr
	};
	const char* kBullets_general_alt2_2[] = {
		"TCP: только multisplit (как base general), без fake",
		"split-pos=2 — разрез глубже, чем у general (pos=1)",
		"split-seqovl=652, единый pattern tls_clienthello_www_google_com.bin",
		"Нет fooling — чистая атака на reassembly DPI",
		"Подходит, если DPI «научился» на split-pos=1",
		nullptr
	};
	const char* kBullets_general_alt3_3[] = {
		"Hostfakesplit с подменой SNI на google.com/ya.ru",
		"fake-tls-mod=rnd,dupsid — random session id и random bytes",
		"split-pos=1,midsld — разрез в середине second-level domain",
		"UDP fake QUIC с repeats=6",
		"Отдельные hostlist для google/general/discord",
		nullptr
	};
	const char* kBullets_general_alt4_4[] = {
		"fake + multisplit на TCP 443",
		"fooling=badseq с increment=1000",
		"split-seqovl=681, google pattern",
		"UDP fake QUIC, repeats=6",
		"Для провайдеров с жёстким TLS inspection",
		nullptr
	};
	const char* kBullets_general_alt5_5[] = {
		"syndata + multidisorder на TCP",
		"Экстремальный режим, может ломать игры/VoIP",
		"Использовать только если остальное не помогло",
		"Высокая нагрузка на CPU и latency",
		"Не для постоянного использования",
		nullptr
	};
	const char* kBullets_general_alt6_6[] = {
		"TCP: multisplit — разрез TLS ClientHello без fake-пакетов",
		"Discord/Google: split-seqovl=681, google pattern",
		"General: split-seqovl=568, tls_clienthello_4pda_to.bin",
		"Google 443: ip-id=zero; UDP fake QUIC repeats=6/12",
		"Самый простой режим — только ложные ClientHello",
		nullptr
	};
	const char* kBullets_general_alt7_7[] = {
		"split-pos=2,sniext+1 — разрез у SNI extension",
		"split-seqovl=679, google pattern",
		"Только multisplit, без fake",
		"Discord/media и general lists",
		"Для DPI с парсингом TLS extensions",
		nullptr
	};
	const char* kBullets_general_alt8_8[] = {
		"fake без split + fooling=badseq increment=2",
		"fake-tls-mod=none — decoy без модификаций",
		"Статические fake TLS из .bin",
		"Мягкий режим для нестабильных провайдеров",
		"UDP fake QUIC repeats=6",
		nullptr
	};
	const char* kBullets_general_alt9_9[] = {
		"hostfakesplit с SNI ozon.ru на general list",
		"fooling=md5sig на TCP",
		"fake+multisplit для discord/google",
		"repeats=4 — умеренная нагрузка",
		"RU whitelist fingerprint",
		nullptr
	};
	const char* kBullets_general_alt10_10[] = {
		"TCP: только fake (без split) + fooling=ts",
		"Статические fake TLS: google, stun, max.ru HTTP",
		"General-трафик: tls_clienthello_4pda_to.bin — overlap под RU-сайты",
		"Discord: fake-tls-mod=none — минимальная модификация decoy",
		"UDP QUIC fake, repeats=6 на всех UDP-правилах",
		nullptr
	};
	const char* kBullets_general_alt11_11[] = {
		"TCP: fake + multisplit одновременно + fooling=ts",
		"Повышенные repeats: UDP=11, TCP=8 — «давление» на stateful DPI",
		"Discord/Google: split-seqovl=681, google pattern",
		"General: split-seqovl=664, tls_clienthello_max_ru.bin",
		"Несколько static fake TLS на каждом TCP-правиле",
		nullptr
	};
	const char* kBullets_general_alt12_12[] = {
		"Гибрид ALT11: fake+multisplit для general/discord",
		"Google 443: hostfakesplit вместо fake+multisplit",
		"Discord UDP: доп. stun.bin, repeats=3 (легче, чем у ALT11)",
		"fooling=ts, general pattern max_ru.bin, seqovl=664",
		"Тонкая настройка под разные типы трафика в одной стратегии",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt_13[] = {
		"fake-tls=! — decoy из реального ClientHello",
		"fake + fakedsplit на TCP 443",
		"fooling=ts",
		"SNI подмена на google.com",
		"Dynamic fake сложнее детектировать DPI",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt2_14[] = {
		"fake-tls=! + multisplit",
		"fooling=badseq increment=10000000",
		"Экстремальный seq jump",
		"Dynamic TLS fake",
		"Только для жёстких блокировок",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt3_15[] = {
		"fake-tls=! + multisplit",
		"fooling=ts вместо badseq",
		"Dynamic ClientHello decoy",
		"Мягче ALT2, стабильнее для VoIP",
		"split-seqovl=681",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_16[] = {
		"fake + multidisorder",
		"fake-tls=! — live ClientHello clone",
		"SNI google.com в decoy",
		"fooling=ts",
		"Сильный режим для TLS inspection",
		nullptr
	};
	const char* kBullets_general_simple_fake_alt_17[] = {
		"Только fake, без split",
		"fooling=badseq вместо ts",
		"Static fake TLS из .bin",
		"Минимальная сложность",
		"UDP fake QUIC",
		nullptr
	};
	const char* kBullets_general_simple_fake_alt2_18[] = {
		"fake + tls_clienthello_max_ru.bin",
		"cutoff=n5 — раннее прекращение desync",
		"fooling=ts",
		"Static decoy",
		"Меньше влияния на long-lived соединения",
		nullptr
	};
	const char* kBullets_general_simple_fake_19[] = {
		"TCP: fake only — без multisplit/fakedsplit",
		"Static fake TLS (google, stun) + fake-http=max_ru",
		"fooling=ts, repeats=6",
		"Game TCP cutoff=n4",
		"Самый простой режим — только ложные ClientHello",
		nullptr
	};
	const char* kBullets_aggressive_20[] = {
		"Максимальное давление на DPI: высокие repeats, dup/autottl и комбинации fooling.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80: fake, fakedsplit, split-pos=2, fooling md5sig+badseq+TCP timestamps, repeats=8, dup=2, autottl=2",
		"TCP 443 · hostlist list-ultimate.txt: fake, fakedsplit, split-pos=1,host+1,midsld,endhost, seqovl=1, fooling md5sig+badseq+TCP timestamps, repeats=10, dup=3, autottl=2",
		nullptr
	};
	const char* kBullets_discord_optimized_21[] = {
		"Узкая оптимизация под Discord: только discord list + L7 tls/stun.",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"TCP 443 · L7 tls · hostlist list-discord.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"TCP 2053,2083,2087,2096,8443: fake, multisplit, split-pos=1,midsld, seqovl=568, fake-tls tls_clienthello_4pda_to.bin, fooling md5sig, repeats=6",
		"UDP 443 · L7 quic · hostlist list-discord.txt: fake, repeats=6",
		"UDP 19294-19344 · L7 discord: fake, repeats=10, dup=2",
		nullptr
	};
	const char* kBullets_discord_voice_only_22[] = {
		"Только голос Discord/STUN — минимальный набор правил без general-трафика.",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"UDP 19294-19344 · L7 discord: fake, repeats=8, dup=2",
		"UDP 50000-65535 · L7 stun,discord: fake, repeats=8",
		nullptr
	};
	const char* kBullets_discordfix_23[] = {
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_discordfix_24[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"wssize меняет TCP window на SYN — ломает эвристики stateful DPI.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 50000-65535 · ipset ipset-discord.txt: fake, syndata, split2, disorder2, repeats=6, cutoff=d3",
		nullptr
	};
	const char* kBullets_discordfix_25[] = {
		"Профиль заточен под провайдера МГТС.",
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_discordfix_v9_2_26[] = {
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-discord.txt: fake, repeats=6",
		"UDP 19294-19344,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_discordfix_v9_3_27[] = {
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"TCP 2053,2083,2087,2096,8443: fake, multisplit, split-pos=1,midsld, seqovl=568, fake-tls tls_clienthello_4pda_to.bin, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-discord.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443 · hostlist list-discord.txt: fake, repeats=6",
		"UDP 19294-19344: fake, repeats=8",
		"UDP 50000-50100: fake, repeats=8",
		nullptr
	};
	const char* kBullets_discordfix_alt_28[] = {
		"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d4 — desync только на первых пакетах, меньше влияния на сессию.",
		"TCP 80: fake, split2, fooling md5sig, autottl=2",
		"UDP 443 · hostlist list-ultimate.txt: fake, disorder2, repeats=6, cutoff=d4",
		nullptr
	};
	const char* kBullets_gaming_29[] = {
		"Игровой режим: multisplit/fake с GameFilter и ipset-ubisoft, без экстремального dup.",
		"ipset-ubisoft — игровые серверы Ubisoft.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-steam.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-nvidia.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"TCP 443 · ipset ipset-ubisoft.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		nullptr
	};
	const char* kBullets_general_alt13_30[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"badseq increment=2 — агрессивный seq-desync.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_alt14_31[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"badseq increment=2 — агрессивный seq-desync.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_fake_tls_alt_32[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		"UDP 1400,596-599,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt4_33[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"badseq increment=10000000 — агрессивный seq-desync.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=11",
		nullptr
	};
	const char* kBullets_general_fake_tls_34[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=8",
		"UDP 1400,596-599,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_hostfakesplit_35[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		"UDP 1400,596-599,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_ipfrag_36[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: ipfrag2, fake, fooling md5sig, repeats=6",
		"UDP 443: ipfrag2, fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_ipv6_destopt_37[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"IPv6 extension headers — для сетей с IPv6-inspection на middlebox.",
		"TCP 80: IPv6 destopt, fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: IPv6 destopt, fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443: IPv6 destopt, fake, repeats=6",
		"UDP 50000-65535: IPv6 destopt, fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_ipv6_hopbyhop_38[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"IPv6 extension headers — для сетей с IPv6-inspection на middlebox.",
		"TCP 80: IPv6 hop-by-hop, fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: IPv6 hop-by-hop, fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443: IPv6 hop-by-hop, fake, repeats=6",
		"UDP 50000-65535: IPv6 hop-by-hop, fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_l7_filter_39[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"TCP 80 · L7 http: fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · L7 tls · hostlist list-ultimate.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443 · L7 quic: fake, repeats=6",
		"UDP 19294-19344 · L7 discord: fake, repeats=8",
		"UDP 50000-65535 · L7 stun,discord: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_multidisorder_auto_40[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=11",
		"UDP 1400,596-599,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_sniext_41[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		"UDP 1400,596-599,50000-50100 · L7 discord,stun: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_syndata_v2_42[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: syndata, fooling md5sig, repeats=6, autottl=2",
		"UDP 443: fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_wssize_43[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"wssize меняет TCP window на SYN — ломает эвристики stateful DPI.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443: fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_44[] = {
		"Профиль заточен под провайдера МГТС.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_2_45[] = {
		"Профиль заточен под провайдера МГТС.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_2_46[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_2_alt_47[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=11",
		nullptr
	};
	const char* kBullets_general_v9_2_beeline_rostelecom_48[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_general_v9_2_fake_tls_auto_49[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=11",
		nullptr
	};
	const char* kBullets_general_v9_2_mgts_50[] = {
		"Профиль заточен под провайдера МГТС.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_general_v9_2_simple_fake_51[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_2_syndata_52[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n2 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_3_53[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6, autottl=2",
		"TCP 2053,2083,2087,2096,8443: fake, multisplit, split-pos=1,midsld, seqovl=568, fake-tls tls_clienthello_4pda_to.bin, fooling md5sig, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6",
		"UDP 443: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_3_dup_54[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6, dup=3",
		"TCP 2053,2083,2087,2096,8443: fake, multisplit, split-pos=1,midsld, seqovl=568, fake-tls tls_clienthello_4pda_to.bin, fooling md5sig, repeats=6, dup=3",
		"TCP 443 · hostlist list-ultimate.txt: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6, dup=3",
		"UDP 443: fake, repeats=6",
		nullptr
	};
	const char* kBullets_general_v9_3_hostfakesplit_55[] = {
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"TCP 80: fake, hostfakesplit, fooling md5sig+TCP timestamps, repeats=6",
		"TCP 2053,2083,2087,2096,8443: fake, hostfakesplit, seqovl=568, fake-tls tls_clienthello_4pda_to.bin, fooling md5sig+TCP timestamps, repeats=6",
		"TCP 443 · hostlist list-ultimate.txt: fake, hostfakesplit, seqovl=568, fooling md5sig+TCP timestamps, repeats=6",
		"UDP 443: fake, repeats=6",
		"UDP 19294-19344: fake, repeats=8",
		nullptr
	};
	const char* kBullets_minimal_cpu_56[] = {
		"Облегчённая конфигурация с минимумом правил winws для слабых ПК.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"TCP 443 · L7 tls · hostlist list-discord.txt, list-youtube.txt: fake, multisplit, split-pos=1,midsld, fooling md5sig, repeats=4",
		"UDP 443 · L7 quic · hostlist list-google.txt: fake, repeats=4",
		"UDP 19294-19344 · L7 discord: fake, repeats=4",
		nullptr
	};
	const char* kBullets_preset_russia_57[] = {
		"Профиль заточен под провайдера RU-сети.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n4 — desync только на первых пакетах, меньше влияния на сессию.",
		"TCP 80: fake, split2, fooling md5sig, autottl=2",
		nullptr
	};
	const char* kBullets_preset_russia_etelecom_58[] = {
		"Профиль заточен под провайдера Etelecom.",
		"общий фильтр: fake, split, fake-tls 0x00000000",
		"Шаблоны: 0x00000000.",
		nullptr
	};
	const char* kBullets_russiafix_59[] = {
		"Профиль заточен под провайдера RU-сети.",
		"RU-профиль: акцент на отечественные CDN/сервисы и list-general.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n4 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_russiafix_alt_60[] = {
		"Профиль заточен под провайдера RU-сети.",
		"RU-профиль: акцент на отечественные CDN/сервисы и list-general.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff n4 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_russiafix_rostelekom_61[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"RU-профиль: акцент на отечественные CDN/сервисы и list-general.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"wssize меняет TCP window на SYN — ломает эвристики stateful DPI.",
		nullptr
	};
	const char* kBullets_smart_auto_62[] = {
		"Автовыбор профиля desync внутри одного .bat по эвристикам форка.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6, autottl=2",
		"TCP 443: fake, multisplit, split-pos=1,midsld, seqovl=568, fooling md5sig, repeats=6, autottl=2",
		"UDP 443: fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6",
		nullptr
	};
	const char* kBullets_smart_multiprofile_63[] = {
		"Несколько профилей desync в одной стратегии с переключением по спискам.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80: fake, multisplit, split-pos=2, fooling md5sig, repeats=6, autottl=2",
		"TCP 443 · hostlist list-google.txt: fake, multisplit, split-pos=1,midsld, seqovl=681, fake-tls tls_clienthello_www_google_com.bin, fooling md5sig, repeats=8",
		nullptr
	};
	const char* kBullets_stealth_64[] = {
		"Тихий режим: меньше repeats и без тяжёлых fooling — ниже нагрузка и меньше заметность.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"Dynamic fake-tls: decoy строится из вашего ClientHello, а не из статического .bin.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 443 · L7 tls · hostlist list-ultimate.txt: fake, multisplit, split-pos=midsld, seqovl=568, fake-tls=dynamic, fooling md5sig, repeats=3, autottl=1",
		"UDP 443 · L7 quic: fake, repeats=3, autottl=1",
		nullptr
	};
	const char* kBullets_ubisoftfix_65[] = {
		"Игровой профиль Ubisoft: ipset-ubisoft и умеренный desync без лишней нагрузки.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-ubisoft — игровые серверы Ubisoft.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80 · hostlist list-ultimate.txt · ipset ipset-ubisoft.txt: fake, split2, fooling md5sig, autottl=2",
		"TCP 443 · hostlist list-ultimate.txt · ipset ipset-ubisoft.txt: fake, split, fake-tls tls_clienthello_www_google_com.bin, fooling badseq, repeats=6, autottl=2",
		nullptr
	};
	const char* kBullets_ubisoftfix_alt_66[] = {
		"Игровой профиль Ubisoft: ipset-ubisoft и умеренный desync без лишней нагрузки.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-ubisoft — игровые серверы Ubisoft.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80 · hostlist list-ultimate.txt · ipset ipset-ubisoft.txt: fake, disorder2, fooling md5sig, autottl=4",
		"TCP 443 · hostlist list-ultimate.txt · ipset ipset-ubisoft.txt: syndata, disorder2, split-pos=3, fake-tls tls_clienthello_www_google_com.bin, fooling badseq, repeats=10",
		nullptr
	};
	const char* kBullets_ultimatefix_67[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_68[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_69[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_v9_2_70[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_v9_3_71[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 80 · L7 http: fake, multisplit, split-pos=2, fooling md5sig, repeats=6, autottl=2",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_72[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_73[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_74[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_extended_75[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_pre_76[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v2_77[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v2_78[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v2_79[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v3_80[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v3_81[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v3_82[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v4_83[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v4_84[] = {
		"Профиль заточен под провайдера Билайн / Ростелеком / Инфолинк.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v4_85[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v4_pre_86[] = {
		"Профиль заточен под провайдера МГТС.",
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		nullptr
	};
	const char* kBullets_ultimatefix_alt_v5_87[] = {
		"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.",
		"ipset-cloudflare — отдельные правила для CDN Cloudflare.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		nullptr
	};
	const char* kBullets_youtube_optimized_88[] = {
		"Узкая оптимизация под YouTube: QUIC killer + google lists.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"TCP 443 · L7 tls · hostlist list-google.txt, list-youtube.txt: fake, multisplit, split-pos=1,host+1,midsld,endsld, seqovl=681, fake-tls tls_clienthello_www_google_com.bin, fooling md5sig+TCP timestamps, repeats=8, autottl=2",
		"UDP 443 · L7 quic · hostlist list-google.txt, list-youtube.txt: fake, repeats=10",
		nullptr
	};
	const char* kBullets_youtube_quic_killer_89[] = {
		"Блокировка/подмена QUIC Initial для обхода throttle YouTube.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.",
		"TCP 443 · hostlist list-google.txt, list-youtube.txt: fake, multisplit, split-pos=1,host+1,midsld, seqovl=681, fake-tls tls_clienthello_www_google_com.bin, fooling md5sig+TCP timestamps, repeats=8, dup=2",
		"UDP 443 · L7 quic · hostlist list-google.txt: fake, repeats=16",
		nullptr
	};
	const char* kBullets_youtubefix_90[] = {
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6, cutoff=d3",
		nullptr
	};
	const char* kBullets_youtubefix_91[] = {
		"Профиль заточен под провайдера МГТС.",
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_youtubefix_92[] = {
		"Профиль заточен под провайдера ТТК.",
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_youtubefix_v9_2_93[] = {
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"UDP 443 · hostlist list-youtube.txt: fake, repeats=6",
		"UDP 443 · hostlist list-google.txt: fake, repeats=6",
		"TCP 443 · hostlist list-google.txt: multisplit, split-pos=1, seqovl=681, pattern tls_clienthello_www_google_com.bin",
		"TCP 80,443 · hostlist list-youtube.txt: multisplit, split-pos=1, seqovl=681, pattern tls_clienthello_www_google_com.bin",
		nullptr
	};
	const char* kBullets_youtubefix_v9_3_94[] = {
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"TCP 443 · hostlist list-google.txt: fake, multisplit, split-pos=1,midsld, seqovl=681, fake-tls tls_clienthello_www_google_com.bin, fooling md5sig, repeats=6",
		"UDP 443 · hostlist list-google.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_youtubefix_alt_95[] = {
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		"UDP 50000-65535: fake, repeats=6, cutoff=d3",
		nullptr
	};
	const char* kBullets_youtubefix_alt_96[] = {
		"Профиль заточен под провайдера МГТС.",
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};
	const char* kBullets_youtubefix_alt_97[] = {
		"Профиль заточен под провайдера ТТК.",
		"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
		"list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).",
		"autottl подбирает TTL decoy так, чтобы он не доходил до сервера.",
		"cutoff d3 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-ultimate.txt: fake, repeats=6",
		nullptr
	};

	const char* kSmartStrategyBullets[] = {
		"Мутирует параметры winws (dpi-desync, repeats, fooling) от шаблона general",
		"«Подбор умной» перебирает варианты и сохраняет лучший конфиг",
		"Учитывает Discord, YouTube, Telegram/MTProto и ping",
		nullptr
	};

	const StrategyDescription kDescriptions[] = {
		{
			"Базовая: multisplit TLS без fake — референс zapret-discord-youtube.",
			kBullets_general_0,
			"WinDivert перехватывает SYN/данные, winws вставляет split-сегменты с seq overlap. DPI reassembler склеивает bytes 0..N из pattern (google/4pda), не видит Discord/YouTube SNI. Сервер игнорирует overlap-bytes по TCP semantics и читает настоящий ClientHello."
		},
		{
			"Комбинированный fake+fakedsplit с TCP timestamp fooling — универсальный ALT к base general.",
			kBullets_general_alt_1,
			"ТСПУ анализирует TLS ClientHello и QUIC Initial в потоке. Стратегия шлёт поддельный TLS с тем же 5-tuple, режет настоящий ClientHello (fakedsplit) и портит TCP timestamps (fooling=ts). DPI часто «склеивает» decoy или теряет состояние потока, тогда как сервер принимает корректный ClientHello после split."
		},
		{
			"Multisplit с split-pos=2 — смещение точки разреза относительно base general.",
			kBullets_general_alt2_2,
			"DPI часто заточен под split-pos=1. Сдвиг на 2 байта меняет байтовую картину reassembly — middlebox читает неверный SNI, сервер получает целый ClientHello."
		},
		{
			"Hostfakesplit с подменой SNI на google.com/ya.ru и rnd,dupsid randomization.",
			kBullets_general_alt3_3,
			"В сегмент попадает SNI «белого» домена. rnd,dupsid меняет session ID и random каждый раз — ТСПУ не может заблокировать по статическому fingerprint decoy."
		},
		{
			"Fake+multisplit с badseq increment 1000 — агрессивный TCP seq desync.",
			kBullets_general_alt4_4,
			"Большой скачок sequence number заставляет DPI считать поток «битым» и прекратить deep inspection, при этом end-host TCP stack принимает данные (increment подобран под tolerance стека)."
		},
		{
			"⚠ Экстремальный syndata+multidisorder на L3 — не рекомендуется.",
			kBullets_general_alt5_5,
			"Полное переупорядочивание TCP-сегментов на старте соединения. Может сломать DPI flow tracking целиком, но нестабильно для игр/VoIP и часто хуже по latency — используйте только если ничего else не работает."
		},
		{
			"Идентична base general — multisplit без fake.",
			kBullets_general_alt6_6,
			"Классический multisplit: DPI собирает ClientHello из частей с overlap-pattern, видит «левый» SNI, сервер получает оригинал. Эквивалент general.bat."
		},
		{
			"Multisplit на sniext+1 — разрез у границы SNI extension.",
			kBullets_general_alt7_7,
			"Парсеры ТСПУ часто читают длину SNI extension. Разрез sniext+1 кладёт decoy в extension area — классификатор получает мусор, валидный SNI уезжает в следующий сегмент."
		},
		{
			"Мягкий fake с badseq+2 и fake-tls-mod=none.",
			kBullets_general_alt8_8,
			"Минимальные изменения decoy — для DPI, который блокирует «тяжёлые» fake. Небольшой badseq сбивает sequence tracking без разрыва соединения."
		},
		{
			"Hostfakesplit с ozon.ru на general и md5sig fooling.",
			kBullets_general_alt9_9,
			"Подмена Host на RU e-commerce CDN (ozon.ru) — часто в whitelist. md5sig добавляет нестандартные TCP options, путая DPI parser; repeats=4 снижает нагрузку."
		},
		{
			"Static fake TLS с fooling=ts; general-трафик через 4pda overlap pattern.",
			kBullets_general_alt10_10,
			"Decoy ClientHello уводит DPI на статический шаблон, ts ломает tracking. На general list pattern 4pda имитирует overlap популярного RU TLS — DPI видит «знакомый» fingerprint, реальный пакет проходит отдельно."
		},
		{
			"Усиленный fake+multisplit с повышенными repeats для «давления» на stateful DPI.",
			kBullets_general_alt11_11,
			"Двойной удар: fake сбивает классификатор, multisplit ломает reassembly. Больше repeats — больше шансов переполнить таблицу состояний ТСПУ на дешёвом оборудовании провайдера."
		},
		{
			"Гибрид ALT11: hostfakesplit для Google, fake+multisplit для остального.",
			kBullets_general_alt12_12,
			"Разные приёмы под разные списки: Google получает hostfakesplit (подмена host в сегменте), остальной трафик — классический fake+split. Снижает ложные срабатывания на Discord UDP."
		},
		{
			"Auto-TLS fake + fakedsplit из live ClientHello.",
			kBullets_general_fake_tls_auto_alt_13,
			"Decoy не из .bin, а из вашего реального ClientHello с подменой SNI на google.com. Fakedsplit режет оригинал рядом — DPI сложнее отличить fake от legitimate retry."
		},
		{
			"Auto-TLS + multisplit + extreme badseq (10M).",
			kBullets_general_fake_tls_auto_alt2_14,
			"Экстремальный badseq заставляет stateful middlebox сбросить состояние потока. Dynamic TLS fake не match'ится с blacklist static patterns."
		},
		{
			"Auto-TLS + multisplit + ts fooling (мягче ALT2).",
			kBullets_general_fake_tls_auto_alt3_15,
			"Тот же dynamic fake, но timestamps вместо seq jump — для провайдеров, где badseq режет соединение. Multisplit всё ещё ломает reassembly SNI."
		},
		{
			"Fake + multidisorder + dynamic TLS — флагман AUTO семейства.",
			kBullets_general_fake_tls_auto_16,
			"multidisorder переставляет сегменты вокруг середины SLD; fake-tls=! клонирует ваш ClientHello. DPI парсит TLS out-of-order и не находит блокируемый SNI."
		},
		{
			"Simple fake с badseq вместо ts.",
			kBullets_general_simple_fake_alt_17,
			"Только injection decoy без split. badseq desync — когда ts fooling уже в blacklist провайдера."
		},
		{
			"Simple fake + max_ru pattern + ранний cutoff.",
			kBullets_general_simple_fake_alt2_18,
			"Decoy с RU TLS profile на general list. cutoff=n5 прекращает desync раньше — меньше шанс сломать long-lived соединения после handshake."
		},
		{
			"Простейший режим: static fake TLS + ts, без split.",
			kBullets_general_simple_fake_19,
			"Перед настоящим ClientHello вставляется копия из .bin. DPI засчитывает decoy как «просмотренный» пакет; ts мешает привязать decoy к потоку. Минимум сложности, часто достаточно на слабом DPI."
		},
		{
			"Максимальное давление на DPI: высокие repeats, dup/autottl и комбинации fooling: fake, fakedsplit, repeats до 12.",
			kBullets_aggressive_20,
			"Decoy идёт вместе с разрезом настоящего ClientHello: DPI собирает TLS из fake-части, сервер получает целый hello. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"Узкая оптимизация под Discord: только discord list + L7 tls/stun: fake, multisplit, repeats до 10.",
			kBullets_discord_optimized_21,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Только голос Discord/STUN — минимальный набор правил без general-трафика: fake, компактная конфигурация.",
			kBullets_discord_voice_only_22,
			"Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Дублирование пакетов с другим TTL — DPI засчитывает «лишнюю» копию как основную. L7-фильтр (tls/quic/discord/stun) ограничивает desync только нужным протоколом — меньше побочных эффектов."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: fake, split.",
			kBullets_discordfix_23,
			"Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: профиль Билайн / Ростелеком / Инфолинк, disorder2, fake, split2, syndata.",
			kBullets_discordfix_24,
			"Профиль DiscordFix (для Билайн, Ростелеком, Инфолинк) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: профиль МГТС, fake, tamper.",
			kBullets_discordfix_25,
			"Профиль DiscordFix (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Tamper меняет поля UDP/TCP payload на лету — DPI видит искажённый пакет, сервер принимает оригинал. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: ветка zapret v9.2, fake, multisplit, repeats до 12.",
			kBullets_discordfix_v9_2_26,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. AutoTTL подбирает TTL decoy-пакетов, чтобы они не доходили до сервера, но виделись DPI."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: ветка zapret v9.3, fake, multisplit.",
			kBullets_discordfix_v9_3_27,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord: альтернативная ветка, disorder2, fake, split2, tamper.",
			kBullets_discordfix_alt_28,
			"Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Tamper меняет поля UDP/TCP payload на лету — DPI видит искажённый пакет, сервер принимает оригинал."
		},
		{
			"Игровой режим: multisplit/fake с GameFilter и ipset-ubisoft, без экстремального dup: fake, multisplit.",
			kBullets_gaming_29,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (ALT13): fake, multisplit, repeats до 12.",
			kBullets_general_alt13_30,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"general (ALT14): fake, fakedsplit, multisplit, repeats до 10.",
			kBullets_general_alt14_31,
			"Decoy идёт вместе с разрезом настоящего ClientHello: DPI собирает TLS из fake-части, сервер получает целый hello. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (FAKE TLS ALT): альтернативная ветка, fake, multisplit, repeats до 10.",
			kBullets_general_fake_tls_alt_32,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (FAKE TLS AUTO ALT4): альтернативная ветка, fake, multisplit, repeats до 11.",
			kBullets_general_fake_tls_auto_alt4_33,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"general (FAKE TLS): fake, multisplit, repeats до 12.",
			kBullets_general_fake_tls_34,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (HOSTFAKESPLIT): fake, hostfakesplit, multisplit, repeats до 12.",
			kBullets_general_hostfakesplit_35,
			"В одном из сегментов подменяется host/SNI на домен из whitelist (google, ozon, max.ru). ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (IPFRAG): fake, ipfrag2, multisplit.",
			kBullets_general_ipfrag_36,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (IPv6 DESTOPT): IPv6 destopt, fake, multisplit.",
			kBullets_general_ipv6_destopt_37,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. IPv6 Destination Options — аналог hop-by-hop для IPv6-only или dual-stack путей. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (IPv6 HOPBYHOP): fake, IPv6 hop-by-hop, multisplit.",
			kBullets_general_ipv6_hopbyhop_38,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. IPv6 hop-by-hop options путают парсер, не рассчитанный на extension headers. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (L7 FILTER): fake, multisplit.",
			kBullets_general_l7_filter_39,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (MULTIDISORDER AUTO): fake, multidisorder, multisplit, repeats до 11.",
			kBullets_general_multidisorder_auto_40,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (SNIEXT): fake, multisplit, repeats до 12.",
			kBullets_general_sniext_41,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (SYNDATA v2): fake, multisplit, syndata.",
			kBullets_general_syndata_v2_42,
			"Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (WSSIZE): fake, multisplit.",
			kBullets_general_wssize_43,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general (МГТС): профиль МГТС, fake, multisplit, repeats до 10.",
			kBullets_general_44,
			"Профиль general (МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general (МГТС2): профиль МГТС, fake, multisplit, repeats до 12.",
			kBullets_general_2_45,
			"Профиль general (МГТС2) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general v9.2: ветка zapret v9.2, fake, multisplit, repeats до 12.",
			kBullets_general_v9_2_46,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. AutoTTL подбирает TTL decoy-пакетов, чтобы они не доходили до сервера, но виделись DPI."
		},
		{
			"general v9.2 (ALT): ветка zapret v9.2, fake, multisplit, repeats до 11.",
			kBullets_general_v9_2_alt_47,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком."
		},
		{
			"general v9.2 (Beeline-Rostelecom): профиль Билайн / Ростелеком / Инфолинк, ветка zapret v9.2, fake, multidisorder, multisplit, repeats до 10.",
			kBullets_general_v9_2_beeline_rostelecom_48,
			"Профиль general v9.2 (Beeline-Rostelecom) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"general v9.2 (FAKE TLS AUTO): ветка zapret v9.2, fake, multidisorder, repeats до 11.",
			kBullets_general_v9_2_fake_tls_auto_49,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"general v9.2 (MGTS): профиль МГТС, ветка zapret v9.2, fake, multisplit, repeats до 12.",
			kBullets_general_v9_2_mgts_50,
			"Профиль general v9.2 (MGTS) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general v9.2 (SIMPLE FAKE): ветка zapret v9.2, fake, repeats до 12.",
			kBullets_general_v9_2_simple_fake_51,
			"Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком. AutoTTL подбирает TTL decoy-пакетов, чтобы они не доходили до сервера, но виделись DPI."
		},
		{
			"general v9.2 (SYNDATA): ветка zapret v9.2, fake, multisplit, syndata, repeats до 12.",
			kBullets_general_v9_2_syndata_52,
			"Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"general v9.3: ветка zapret v9.3, fake, multisplit.",
			kBullets_general_v9_3_53,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general v9.3 (DUP): ветка zapret v9.3, fake, multisplit.",
			kBullets_general_v9_3_dup_54,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"general v9.3 (HOSTFAKESPLIT): ветка zapret v9.3, fake, hostfakesplit.",
			kBullets_general_v9_3_hostfakesplit_55,
			"В одном из сегментов подменяется host/SNI на домен из whitelist (google, ozon, max.ru). Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком."
		},
		{
			"Облегчённая конфигурация с минимумом правил winws для слабых ПК: fake, multisplit.",
			kBullets_minimal_cpu_56,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"preset_russia: профиль RU-сети, disorder2, fake, split2, repeats до 11.",
			kBullets_preset_russia_57,
			"Профиль preset_russia собран под особенности DPI RU-сети: набор fooling и split-pos подобран эмпирически в форке v2. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"preset_russia_etelecom: профиль Etelecom, fake, split, компактная конфигурация.",
			kBullets_preset_russia_etelecom_58,
			"Профиль preset_russia_etelecom собран под особенности DPI Etelecom: набор fooling и split-pos подобран эмпирически в форке v2. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"RU-профиль: акцент на отечественные CDN/сервисы и list-general: профиль RU-сети, disorder2, fake, split2, repeats до 11.",
			kBullets_russiafix_59,
			"Профиль RussiaFix собран под особенности DPI RU-сети: набор fooling и split-pos подобран эмпирически в форке v2. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"RU-профиль: акцент на отечественные CDN/сервисы и list-general: профиль RU-сети, disorder2, fake, split2, tamper, repeats до 10.",
			kBullets_russiafix_alt_60,
			"Профиль RussiaFix (ALT) собран под особенности DPI RU-сети: набор fooling и split-pos подобран эмпирически в форке v2. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"RU-профиль: акцент на отечественные CDN/сервисы и list-general: профиль Билайн / Ростелеком / Инфолинк, disorder2, fake, split2, syndata (+1), repeats до 10.",
			kBullets_russiafix_rostelekom_61,
			"Профиль RussiaFix (Rostelekom) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split."
		},
		{
			"Автовыбор профиля desync внутри одного .bat по эвристикам форка: fake, multisplit.",
			kBullets_smart_auto_62,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Несколько профилей desync в одной стратегии с переключением по спискам: fake, multisplit.",
			kBullets_smart_multiprofile_63,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Тихий режим: меньше repeats и без тяжёлых fooling — ниже нагрузка и меньше заметность: fake, multisplit.",
			kBullets_stealth_64,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Dynamic fake-tls из вашего ClientHello — decoy не совпадает со static blacklist DPI."
		},
		{
			"Игровой профиль Ubisoft: ipset-ubisoft и умеренный desync без лишней нагрузки: fake, split, split2.",
			kBullets_ubisoftfix_65,
			"Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Игровой профиль Ubisoft: ipset-ubisoft и умеренный desync без лишней нагрузки: disorder2, fake, split2, syndata, repeats до 10.",
			kBullets_ubisoftfix_alt_66,
			"Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: fake, multidisorder, split, split2.",
			kBullets_ultimatefix_67,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль Билайн / Ростелеком / Инфолинк, disorder2, fake, multidisorder, split2 (+1).",
			kBullets_ultimatefix_68,
			"Профиль UltimateFix (для Билайн, Ростелеком, Инфолинк) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, fake, multidisorder, split2.",
			kBullets_ultimatefix_69,
			"Профиль UltimateFix (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка zapret v9.2, fake, multidisorder, multisplit, split2, repeats до 10.",
			kBullets_ultimatefix_v9_2_70,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка zapret v9.3, fake, multisplit.",
			kBullets_ultimatefix_v9_3_71,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: альтернативная ветка, fake, multidisorder, split, split2.",
			kBullets_ultimatefix_alt_72,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль Билайн / Ростелеком / Инфолинк, альтернативная ветка, disorder2, fake, multidisorder, split (+2).",
			kBullets_ultimatefix_alt_73,
			"Профиль UltimateFix_ALT (для Билайна и Ростелеком) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, альтернативная ветка, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_74,
			"Профиль UltimateFix_ALT (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: расширенный ALT, fake, multidisorder, split, split2.",
			kBullets_ultimatefix_alt_extended_75,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: pre-release профиль, fake, multidisorder, split, split2.",
			kBullets_ultimatefix_alt_pre_76,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка ALT v2, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_v2_77,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль Билайн / Ростелеком / Инфолинк, ветка ALT v2, disorder2, fake, multidisorder, split (+2).",
			kBullets_ultimatefix_alt_v2_78,
			"Профиль UltimateFix_ALT_v2 (для Билайна и Ростелеком) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, ветка ALT v2, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_v2_79,
			"Профиль UltimateFix_ALT_v2 (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка ALT v3, fake, multidisorder, split, split2.",
			kBullets_ultimatefix_alt_v3_80,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль Билайн / Ростелеком / Инфолинк, ветка ALT v3, disorder2, fake, multidisorder, split (+2).",
			kBullets_ultimatefix_alt_v3_81,
			"Профиль UltimateFix_ALT_v3 (для Билайна и Ростелеком) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, ветка ALT v3, fake, multidisorder, split, split2.",
			kBullets_ultimatefix_alt_v3_82,
			"Профиль UltimateFix_ALT_v3 (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка ALT v4, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_v4_83,
			"Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль Билайн / Ростелеком / Инфолинк, ветка ALT v4, disorder2, fake, multidisorder, split2 (+1).",
			kBullets_ultimatefix_alt_v4_84,
			"Профиль UltimateFix_ALT_v4 (для Билайна и Ростелеком) собран под особенности DPI Билайн / Ростелеком / Инфолинк: набор fooling и split-pos подобран эмпирически в форке v2. Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, ветка ALT v4, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_v4_85,
			"Профиль UltimateFix_ALT_v4 (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: профиль МГТС, pre-release профиль, fake, multidisorder, split2.",
			kBullets_ultimatefix_alt_v4_pre_86,
			"Профиль UltimateFix_ALT_v4_PRE (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all: ветка ALT v5, fake, multidisorder, split2, syndata.",
			kBullets_ultimatefix_alt_v5_87,
			"Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока. Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello."
		},
		{
			"Узкая оптимизация под YouTube: QUIC killer + google lists: fake, multisplit, repeats до 10.",
			kBullets_youtube_optimized_88,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком."
		},
		{
			"Блокировка/подмена QUIC Initial для обхода throttle YouTube: fake, multisplit, repeats до 16.",
			kBullets_youtube_quic_killer_89,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Подмена TCP timestamps мешает DPI связать decoy с потоком."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: fake, split, split2.",
			kBullets_youtubefix_90,
			"Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: профиль МГТС, fake.",
			kBullets_youtubefix_91,
			"Профиль YoutubeFix (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: профиль ТТК, fake, split2.",
			kBullets_youtubefix_92,
			"Профиль YoutubeFix (для ТТК) собран под особенности DPI ТТК: набор fooling и split-pos подобран эмпирически в форке v2. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: ветка zapret v9.2, fake, multisplit.",
			kBullets_youtubefix_v9_2_93,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: ветка zapret v9.3, fake, multisplit, компактная конфигурация.",
			kBullets_youtubefix_v9_3_94,
			"ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Нестандартные TCP options (md5sig) ломают простые L4-классификаторы."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: альтернативная ветка, fake, split, split2.",
			kBullets_youtubefix_alt_95,
			"Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: профиль МГТС, альтернативная ветка, fake.",
			kBullets_youtubefix_alt_96,
			"Профиль YoutubeFix_ALT (для МГТС) собран под особенности DPI МГТС: набор fooling и split-pos подобран эмпирически в форке v2. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery. Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента."
		},
		{
			"Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset: профиль ТТК, альтернативная ветка, fake, split2.",
			kBullets_youtubefix_alt_97,
			"Профиль YoutubeFix_ALT (для ТТК) собран под особенности DPI ТТК: набор fooling и split-pos подобран эмпирически в форке v2. Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		}
	};

	static_assert(
		sizeof(kDescriptions) / sizeof(kDescriptions[0]) == ZapretStrategies::kStrategyCount,
		"Strategy descriptions must match kStrategyCount");

	const StrategyDescription kSmartDescription = {
		"Умная стратегия: подбор собственных аргументов winws на базе general.",
		kSmartStrategyBullets,
		"В отличие от «Автовыбора лучшей», здесь не выбирается готовый .bat — "
		"алгоритм меняет dpi-desync, repeats и fooling, тестирует и сохраняет лучший набор. "
		"Профиль хранится в smart_strategy.ini."
	};
}

const StrategyDescription* StrategyDescriptions::GetByIndex(int strategyIndex)
{
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return nullptr;
	return &kDescriptions[static_cast<size_t>(strategyIndex)];
}

const StrategyDescription* StrategyDescriptions::GetSmartStrategy()
{
	return &kSmartDescription;
}
