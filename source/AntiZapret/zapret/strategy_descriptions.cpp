#include "zapret/strategy_descriptions.h"

#include "zapret/strategies.hpp"
#include <cstring>

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
		"fooling=ts на TCP-правилах",
		"UDP fake QUIC с repeats=6",
		"Отдельные hostlist для google/general/discord",
		nullptr
	};
	const char* kBullets_general_alt4_4[] = {
		"fake + multisplit на TCP 443",
		"fooling=badseq с increment=1000",
		"Static fake TLS (google, stun, max.ru)",
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
		"TCP: multisplit без fake — как general, но единый pattern",
		"Все TCP-правила: split-seqovl=681, tls_clienthello_www_google_com.bin",
		"В отличие от general: нет 4pda/seqovl=568 на general list",
		"Google 443: ip-id=zero; UDP fake QUIC repeats=6",
		"Простой multisplit-профиль без fake injection",
		nullptr
	};
	const char* kBullets_general_alt7_7[] = {
		"split-pos=2,sniext+1 — разрез у SNI extension",
		"split-seqovl=679, google pattern",
		"Основной режим: multisplit без fake",
		"ipset-all TCP: отдельно syndata",
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
		"hostfakesplit на discord.media / google / general",
		"general list: host=ozon.ru + fooling=ts,md5sig",
		"discord/google: host=www.google.com, fooling=ts, repeats=4",
		"UDP fake QUIC, repeats=6",
		"RU whitelist fingerprint (ozon.ru)",
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
	const char* kBullets_general_alt13_13[] = {
		"Списки Google/YouTube для видео и QUIC-трафика.",
		"cutoff n4 — desync только на первых пакетах, меньше влияния на сессию.",
		"UDP 443 · hostlist list-general.txt, list-general-user.txt: fake, repeats=11",
		"UDP 19294-19344,50000-50100 · L7 discord,stun: fake, repeats=5",
		"TCP 2053,2083,2087,2096,8443 · discord.media: fake, multisplit, split-pos=1, seqovl=681, pattern tls_clienthello_www_google_com.bin, fake-tls tls_clienthello_www_google_com.bin, fooling TCP timestamps, repeats=7",
		"TCP 443 · hostlist list-google.txt: hostfakesplit, fooling TCP timestamps",
		nullptr
	};
	const char* kBullets_general_exp_14[] = {
		"Экспериментальный гибрид Flowseal: fake+multisplit + hostfakesplit",
		"Google 443: hostfakesplit host=www.google.com, fooling=ts",
		"General/ipset: fake+multisplit, seqovl=480, pattern stun2.bin, fake max_ru",
		"QUIC fake repeats=11; Discord UDP fake repeats=4",
		"GameFilter TCP/UDP с any-protocol и cutoff=n4",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt_15[] = {
		"fake + fakedsplit на TCP 443",
		"fooling=badseq increment=2",
		"fake-tls-mod=rnd,dupsid,sni=www.google.com",
		"QUIC fake repeats=11; TCP repeats=8",
		"Без fake-tls=! — decoy через tls-mod, не live clone",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt2_16[] = {
		"fake + multisplit, split-seqovl=681",
		"fooling=badseq increment=10000000",
		"fake-tls-mod=rnd,dupsid,sni=www.google.com",
		"Экстремальный seq jump",
		"Только для жёстких блокировок",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_alt3_17[] = {
		"fake + multisplit, split-seqovl=681",
		"fooling=ts вместо badseq",
		"fake-tls-mod=rnd,dupsid,sni=www.google.com",
		"Мягче ALT2, стабильнее для VoIP",
		"TCP repeats=8",
		nullptr
	};
	const char* kBullets_general_fake_tls_auto_18[] = {
		"fake + multidisorder (split-pos=1,midsld)",
		"fake-tls=! — live ClientHello clone",
		"SNI google.com в decoy (rnd,dupsid)",
		"fooling=badseq, repeats=11",
		"Сильный режим для TLS inspection",
		nullptr
	};
	const char* kBullets_general_simple_fake_alt_19[] = {
		"Только fake, без split",
		"fooling=badseq вместо ts",
		"Static fake TLS из .bin",
		"Минимальная сложность",
		"UDP fake QUIC",
		nullptr
	};
	const char* kBullets_general_simple_fake_alt2_20[] = {
		"fake + tls_clienthello_max_ru.bin",
		"cutoff=n5 — раннее прекращение desync",
		"fooling=ts",
		"Static decoy",
		"Меньше влияния на long-lived соединения",
		nullptr
	};
	const char* kBullets_general_simple_fake_21[] = {
		"TCP: fake only — без multisplit/fakedsplit",
		"Static fake TLS (google, stun) + fake-http=max_ru",
		"fooling=ts, repeats=6",
		"Game TCP cutoff=n4",
		"Самый простой режим — только ложные ClientHello",
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
			"Multisplit с единым google pattern (seqovl=681) — без ветки 4pda как у general.",
			kBullets_general_alt6_6,
			"Классический multisplit: DPI собирает ClientHello из частей с overlap-pattern, видит «левый» SNI, сервер получает оригинал. От general отличается тем, что general list тоже идёт через google pattern/681, без 4pda."
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
			"Hostfakesplit: google.com на Discord/Google, ozon.ru+md5sig на general.",
			kBullets_general_alt9_9,
			"Подмена Host на whitelist-домены (google.com / ozon.ru). md5sig на general добавляет нестандартные TCP options; repeats=4 снижает нагрузку."
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
			"general (ALT13): fake, hostfakesplit, multisplit, repeats до 11.",
			kBullets_general_alt13_13,
			"В одном из сегментов подменяется host/SNI на домен из whitelist (google, ozon, max.ru). ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI. Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello. Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery."
		},
		{
			"Экспериментальный гибрид: fake+multisplit, hostfakesplit на Google, stun2/max_ru.",
			kBullets_general_exp_14,
			"Google уходит через hostfakesplit (whitelist SNI). Остальной TCP — fake+multisplit с seqovl=480 и pattern stun2.bin / decoy max_ru: DPI видит «белый» overlap и decoy, сервер собирает настоящий ClientHello. QUIC и Discord UDP закрываются отдельным fake."
		},
		{
			"Fake+fakedsplit с badseq+2 и tls-mod (rnd/dupsid/SNI google).",
			kBullets_general_fake_tls_auto_alt_15,
			"Decoy модифицируется через fake-tls-mod (не live clone). Fakedsplit режет оригинал рядом; badseq+2 сбивает sequence tracking DPI без жёсткого разрыва соединения."
		},
		{
			"Fake+multisplit + extreme badseq (10M).",
			kBullets_general_fake_tls_auto_alt2_16,
			"Экстремальный badseq заставляет stateful middlebox сбросить состояние потока. fake-tls-mod рандомизирует decoy; multisplit ломает reassembly SNI."
		},
		{
			"Fake+multisplit + ts fooling (мягче ALT2).",
			kBullets_general_fake_tls_auto_alt3_17,
			"Тот же fake+multisplit с tls-mod, но timestamps вместо seq jump — для провайдеров, где badseq режет соединение. Multisplit всё ещё ломает reassembly SNI."
		},
		{
			"Fake + multidisorder + live TLS clone (fake-tls=!) — флагман AUTO.",
			kBullets_general_fake_tls_auto_18,
			"multidisorder переставляет сегменты вокруг середины SLD; fake-tls=! клонирует ваш ClientHello. fooling=badseq. DPI парсит TLS out-of-order и не находит блокируемый SNI."
		},
		{
			"Simple fake с badseq вместо ts.",
			kBullets_general_simple_fake_alt_19,
			"Только injection decoy без split. badseq desync — когда ts fooling уже в blacklist провайдера."
		},
		{
			"Simple fake + max_ru pattern + ранний cutoff.",
			kBullets_general_simple_fake_alt2_20,
			"Decoy с RU TLS profile на general list. cutoff=n5 прекращает desync раньше — меньше шанс сломать long-lived соединения после handshake."
		},
		{
			"Простейший режим: static fake TLS + ts, без split.",
			kBullets_general_simple_fake_21,
			"Перед настоящим ClientHello вставляется копия из .bin. DPI засчитывает decoy как «просмотренный» пакет; ts мешает привязать decoy к потоку. Минимум сложности, часто достаточно на слабом DPI."
		}
	};

	static_assert(
		sizeof(kDescriptions) / sizeof(kDescriptions[0]) == ZapretStrategies::kStrategyCount,
		"Strategy descriptions must match kStrategyCount");
}

const StrategyDescription* StrategyDescriptions::GetByIndex(int strategyIndex)
{
	if (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))
		return nullptr;
	return &kDescriptions[static_cast<size_t>(strategyIndex)];
}

const StrategyDescription* StrategyDescriptions::GetById(const char* strategyId)
{
	if (!strategyId || !strategyId[0])
		return nullptr;
	for (std::size_t i = 0; i < ZapretStrategies::kStrategyCount; ++i)
	{
		const auto& strategy = ZapretStrategies::kStrategies[i];
		if (_stricmp(strategy.id.data(), strategyId) == 0
			|| _stricmp(strategy.label.data(), strategyId) == 0
			|| _stricmp(strategy.file.data(), strategyId) == 0)
			return &kDescriptions[i];
	}
	return nullptr;
}
