"""Build human-readable strategy descriptions from winws argument lists."""

from __future__ import annotations

import re
from dataclasses import dataclass, field


DESYNC_LABELS: dict[str, str] = {
    "fake": "fake",
    "fakedsplit": "fakedsplit",
    "multisplit": "multisplit",
    "split": "split",
    "split2": "split2",
    "hostfakesplit": "hostfakesplit",
    "multidisorder": "multidisorder",
    "disorder": "disorder",
    "disorder2": "disorder2",
    "syndata": "syndata",
    "ipfrag": "ipfrag",
    "hopbyhop": "IPv6 hop-by-hop",
    "destopt": "IPv6 destopt",
    "tamper": "tamper",
}

FOOLING_LABELS: dict[str, str] = {
    "ts": "TCP timestamps",
    "badseq": "badseq",
    "md5sig": "md5sig",
}

TSPU_BY_TECHNIQUE: dict[str, str] = {
    "fake": "Перед handshake вставляется decoy-пакет — DPI «просматривает» подмену и часто не доходит до реального ClientHello.",
    "fakedsplit": "Decoy идёт вместе с разрезом настоящего ClientHello: DPI собирает TLS из fake-части, сервер получает целый hello.",
    "multisplit": "ClientHello режется на сегменты с seq-overlap из .bin-pattern — reassembly у DPI показывает «белый» SNI.",
    "split": "Простой split без fake: DPI читает SNI из первого фрагмента, остальное уходит вторым сегментом.",
    "split2": "Усиленный split (split2) с другой точкой разреза — для DPI, устойчивого к классическому split.",
    "hostfakesplit": "В одном из сегментов подменяется host/SNI на домен из whitelist (google, ozon, max.ru).",
    "multidisorder": "Несколько TCP-сегментов уходят out-of-order — stateful DPI теряет контекст TLS-парсера.",
    "disorder": "Disorder меняет порядок сегментов на старте потока, ломая reassembly на middlebox.",
    "disorder2": "Disorder2 — усиленный вариант перестановки сегментов для жёстких провайдеров.",
    "syndata": "Подмена/дублирование SYN-data — атака на самый ранний этап классификации потока.",
    "ipfrag": "IP-фрагментация на уровне desync — часть DPI не собирает фрагменты и пропускает SNI.",
    "hopbyhop": "IPv6 hop-by-hop options путают парсер, не рассчитанный на extension headers.",
    "destopt": "IPv6 Destination Options — аналог hop-by-hop для IPv6-only или dual-stack путей.",
    "quic_fake": "Подмена QUIC Initial (UDP/443) — обход блокировок QUIC/HTTP3 и Discord voice discovery.",
    "badseq": "Скачок TCP sequence number сбивает tracking DPI, не разрывая соединение у клиента.",
    "ts": "Подмена TCP timestamps мешает DPI связать decoy с потоком.",
    "md5sig": "Нестандартные TCP options (md5sig) ломают простые L4-классификаторы.",
    "autottl": "AutoTTL подбирает TTL decoy-пакетов, чтобы они не доходили до сервера, но виделись DPI.",
    "dup": "Дублирование пакетов с другим TTL — DPI засчитывает «лишнюю» копию как основную.",
    "wssize": "Window size manipulation на SYN — ломает эвристики DPI по TCP window.",
    "l7": "L7-фильтр (tls/quic/discord/stun) ограничивает desync только нужным протоколом — меньше побочных эффектов.",
    "cutoff": "Cutoff прекращает desync после N пакетов — стабильнее для long-lived соединений.",
    "auto_tls": "Dynamic fake-tls из вашего ClientHello — decoy не совпадает со static blacklist DPI.",
    "tamper": "Tamper меняет поля UDP/TCP payload на лету — DPI видит искажённый пакет, сервер принимает оригинал.",
}

FAMILY_INTROS: dict[str, str] = {
    "discordfix": "Пресет под Discord: голос/STUN UDP, CDN TCP и QUIC с list-ultimate или ipset-discord.",
    "youtubefix": "Пресет под YouTube/Google: QUIC fake, TLS split/fake на list-google и CDN ipset.",
    "ultimatefix": "Комплексный пресет форка: Discord + general + Cloudflare через list-ultimate и ipset-all.",
    "russiafix": "RU-профиль: акцент на отечественные CDN/сервисы и list-general.",
    "ubisoftfix": "Игровой профиль Ubisoft: ipset-ubisoft и умеренный desync без лишней нагрузки.",
    "aggressive": "Максимальное давление на DPI: высокие repeats, dup/autottl и комбинации fooling.",
    "gaming": "Игровой режим: multisplit/fake с GameFilter и ipset-ubisoft, без экстремального dup.",
    "stealth": "Тихий режим: меньше repeats и без тяжёлых fooling — ниже нагрузка и меньше заметность.",
    "minimal-cpu": "Облегчённая конфигурация с минимумом правил winws для слабых ПК.",
    "smart-auto": "Автовыбор профиля desync внутри одного .bat по эвристикам форка.",
    "smart-multiprofile": "Несколько профилей desync в одной стратегии с переключением по спискам.",
    "preset_russia": "Готовый пресет «Россия» из форка v2.",
    "discord-optimized": "Узкая оптимизация под Discord: только discord list + L7 tls/stun.",
    "discord-voice-only": "Только голос Discord/STUN — минимальный набор правил без general-трафика.",
    "youtube-optimized": "Узкая оптимизация под YouTube: QUIC killer + google lists.",
    "youtube-quic-killer": "Блокировка/подмена QUIC Initial для обхода throttle YouTube.",
}


@dataclass
class StrategyAnalysis:
    rule_count: int = 0
    desync_modes: set[str] = field(default_factory=set)
    fooling: set[str] = field(default_factory=set)
    hostlists: set[str] = field(default_factory=set)
    ipsets: set[str] = field(default_factory=set)
    l7_filters: set[str] = field(default_factory=set)
    tcp_ports: set[str] = field(default_factory=set)
    udp_ports: set[str] = field(default_factory=set)
    max_repeats: int = 0
    split_positions: set[str] = field(default_factory=set)
    seq_overlaps: set[str] = field(default_factory=set)
    bin_patterns: set[str] = field(default_factory=set)
    has_quic_fake: bool = False
    has_auto_tls: bool = False
    has_dup: bool = False
    has_autottl: bool = False
    has_wssize: bool = False
    has_syndata: bool = False
    has_ipfrag: bool = False
    has_ipv6_ext: bool = False
    has_cutoff: bool = False
    cutoff_values: set[str] = field(default_factory=set)
    badseq_increment: str | None = None
    rule_bullets: list[str] = field(default_factory=list)
    arg_count: int = 0


def _basename(path_value: str) -> str:
    normalized = path_value.replace("\\", "/")
    if normalized.startswith("@"):
        normalized = normalized[1:]
    normalized = re.sub(r"\$\{[^}]+\}", "", normalized)
    normalized = normalized.strip("/")
    if not normalized:
        return path_value
    return normalized.rsplit("/", 1)[-1]


def _first_values(rule: dict[str, list[str]], key: str) -> list[str]:
    return rule.get(key, [])


def _join_unique(items: set[str] | list[str], limit: int = 4) -> str:
    ordered = list(dict.fromkeys(items))
    if len(ordered) > limit:
        return ", ".join(ordered[:limit]) + f" (+{len(ordered) - limit})"
    return ", ".join(ordered)


def parse_winws_rules(args: list[str]) -> list[dict[str, list[str]]]:
    rules: list[dict[str, list[str]]] = []
    current: dict[str, list[str]] = {}
    pending_key: str | None = None

    for token in args:
        if token == "--new":
            if current:
                rules.append(current)
            current = {}
            pending_key = None
            continue

        if token.startswith("--"):
            key, _, value = token[2:].partition("=")
            if value:
                current.setdefault(key, []).append(value)
                pending_key = None
            else:
                current.setdefault(key, []).append("yes")
                pending_key = key
            continue

        if pending_key is not None:
            current[pending_key][-1] = token
            pending_key = None
            continue

    if current:
        rules.append(current)
    return rules


def _describe_scope(rule: dict[str, list[str]]) -> str:
    parts: list[str] = []
    tcp = _first_values(rule, "filter-tcp")
    udp = _first_values(rule, "filter-udp")
    l7 = _first_values(rule, "filter-l7")
    domains = _first_values(rule, "hostlist-domains")

    if tcp:
        ports = tcp[0]
        if len(ports) > 28:
            ports = ports[:25] + "…"
        parts.append(f"TCP {ports}")
    if udp:
        ports = udp[0]
        if len(ports) > 28:
            ports = ports[:25] + "…"
        parts.append(f"UDP {ports}")
    if l7:
        parts.append(f"L7 {','.join(l7)}")
    if domains:
        parts.append(domains[0])

    hostlists = [_basename(value) for value in _first_values(rule, "hostlist")]
    ipsets = [_basename(value) for value in _first_values(rule, "ipset")]
    if hostlists:
        parts.append(f"hostlist {_join_unique(hostlists, 2)}")
    if ipsets:
        parts.append(f"ipset {_join_unique(ipsets, 2)}")

    return " · ".join(parts) if parts else "общий фильтр"


def _describe_desync(rule: dict[str, list[str]]) -> list[str]:
    chunks: list[str] = []
    desync_values = _first_values(rule, "dpi-desync")
    if not desync_values:
        return chunks

    modes: list[str] = []
    for value in desync_values:
        for part in value.split(","):
            part = part.strip()
            if part:
                modes.append(DESYNC_LABELS.get(part, part))

    if modes:
        chunks.append(_join_unique(modes, 5))

    split_pos = _first_values(rule, "dpi-desync-split-pos")
    if split_pos:
        chunks.append(f"split-pos={split_pos[0]}")

    seqovl = _first_values(rule, "dpi-desync-split-seqovl")
    if seqovl:
        chunks.append(f"seqovl={seqovl[0]}")

    pattern = _first_values(rule, "dpi-desync-split-seqovl-pattern")
    if pattern:
        chunks.append(f"pattern {_basename(pattern[0])}")

    fake_tls = _first_values(rule, "dpi-desync-fake-tls")
    if fake_tls:
        value = fake_tls[0]
        if value in ("!", "auto"):
            chunks.append("fake-tls=dynamic")
        else:
            chunks.append(f"fake-tls {_basename(value)}")

    fooling = _first_values(rule, "dpi-desync-fooling")
    if fooling:
        labels = []
        for item in fooling[0].split(","):
            item = item.strip()
            if item:
                labels.append(FOOLING_LABELS.get(item, item))
        if labels:
            chunks.append("fooling " + "+".join(labels))

    repeats = _first_values(rule, "dpi-desync-repeats")
    if repeats:
        chunks.append(f"repeats={repeats[0]}")

    cutoff = _first_values(rule, "dpi-desync-cutoff")
    if cutoff:
        chunks.append(f"cutoff={cutoff[0]}")

    dup = _first_values(rule, "dup")
    if dup:
        chunks.append(f"dup={dup[0]}")

    autottl = _first_values(rule, "dpi-desync-autottl")
    if autottl:
        chunks.append(f"autottl={autottl[0] if autottl[0] != 'yes' else '1'}")

    return chunks


def analyze_strategy_args(args: list[str]) -> StrategyAnalysis:
    analysis = StrategyAnalysis(arg_count=len(args))
    rules = parse_winws_rules(args)
    analysis.rule_count = len(rules)

    for rule in rules:
        desync_values = _first_values(rule, "dpi-desync")
        for value in desync_values:
            for part in value.split(","):
                part = part.strip().lower()
                if part:
                    analysis.desync_modes.add(part)

        for fool_value in _first_values(rule, "dpi-desync-fooling"):
            for part in fool_value.split(","):
                part = part.strip().lower()
                if part:
                    analysis.fooling.add(part)

        for hostlist in _first_values(rule, "hostlist"):
            analysis.hostlists.add(_basename(hostlist))
        for ipset in _first_values(rule, "ipset"):
            analysis.ipsets.add(_basename(ipset))
        for l7 in _first_values(rule, "filter-l7"):
            for part in l7.split(","):
                part = part.strip().lower()
                if part:
                    analysis.l7_filters.add(part)

        for tcp in _first_values(rule, "filter-tcp"):
            for part in tcp[0].split(","):
                part = part.strip()
                if part:
                    analysis.tcp_ports.add(part)
        for udp in _first_values(rule, "filter-udp"):
            for part in udp[0].split(","):
                part = part.strip()
                if part:
                    analysis.udp_ports.add(part)

        for repeats in _first_values(rule, "dpi-desync-repeats"):
            try:
                analysis.max_repeats = max(analysis.max_repeats, int(repeats))
            except ValueError:
                pass

        for pos in _first_values(rule, "dpi-desync-split-pos"):
            analysis.split_positions.add(pos)
        for seqovl in _first_values(rule, "dpi-desync-split-seqovl"):
            analysis.seq_overlaps.add(seqovl)

        for key in (
            "dpi-desync-split-seqovl-pattern",
            "dpi-desync-fake-tls",
            "dpi-desync-fake-quic",
            "dpi-desync-fake-discord",
            "dpi-desync-fake-stun",
            "dpi-desync-fake-syndata",
            "dpi-desync-fake-http",
        ):
            for value in _first_values(rule, key):
                analysis.bin_patterns.add(_basename(value))

        if _first_values(rule, "dpi-desync-fake-quic") or _first_values(rule, "dpi-desync-fake-discord"):
            analysis.has_quic_fake = True

        for fake_tls in _first_values(rule, "dpi-desync-fake-tls"):
            if fake_tls in ("!", "auto"):
                analysis.has_auto_tls = True

        if _first_values(rule, "dup"):
            analysis.has_dup = True
        if _first_values(rule, "dpi-desync-autottl"):
            analysis.has_autottl = True
        if _first_values(rule, "wssize"):
            analysis.has_wssize = True
        if "syndata" in analysis.desync_modes:
            analysis.has_syndata = True
        if "ipfrag" in analysis.desync_modes:
            analysis.has_ipfrag = True
        if "hopbyhop" in analysis.desync_modes or "destopt" in analysis.desync_modes:
            analysis.has_ipv6_ext = True
        if _first_values(rule, "dpi-desync-cutoff"):
            analysis.has_cutoff = True
            analysis.cutoff_values.update(_first_values(rule, "dpi-desync-cutoff"))

        increment = _first_values(rule, "dpi-desync-badseq-increment")
        if increment:
            analysis.badseq_increment = increment[0]

        desync_chunks = _describe_desync(rule)
        if desync_chunks:
            scope = _describe_scope(rule)
            analysis.rule_bullets.append(f"{scope}: {', '.join(desync_chunks)}")

    return analysis


def _family_key(strategy_id: str) -> str | None:
    lowered = strategy_id.lower()
    for key in sorted(FAMILY_INTROS, key=len, reverse=True):
        if key in lowered.replace(" ", "").replace("_", ""):
            return key
    if lowered.startswith("discord"):
        return "discordfix"
    if lowered.startswith("youtube"):
        return "youtubefix"
    if lowered.startswith("ultimate"):
        return "ultimatefix"
    if lowered.startswith("russia"):
        return "russiafix"
    if lowered.startswith("ubisoft"):
        return "ubisoftfix"
    return None


def provider_hint(strategy_id: str) -> str | None:
    lowered = strategy_id.lower()
    if "мгтс" in lowered or "mgts" in lowered:
        return "МГТС"
    if "билайн" in lowered or "beeline" in lowered or "rostelecom" in lowered or "rostelekom" in lowered:
        return "Билайн / Ростелеком / Инфолинк"
    if "ттк" in lowered:
        return "ТТК"
    if "инфолинк" in lowered:
        return "Инфолинк"
    if "etelecom" in lowered:
        return "Etelecom"
    if "russia" in lowered or "preset_russia" in lowered:
        return "RU-сети"
    return None


def _variant_hint(strategy_id: str) -> str | None:
    lowered = strategy_id.lower()
    if "alt_extended" in lowered or "extended" in lowered:
        return "расширенный ALT"
    if "alt_pre" in lowered or "_pre" in lowered:
        return "pre-release профиль"
    if re.search(r"alt_v\d", lowered):
        match = re.search(r"alt_v(\d+)", lowered)
        if match:
            return f"ветка ALT v{match.group(1)}"
    if "_alt" in lowered or " alt" in lowered:
        return "альтернативная ветка"
    if "v9.3" in lowered:
        return "ветка zapret v9.3"
    if "v9.2" in lowered:
        return "ветка zapret v9.2"
    return None


def build_summary(strategy_id: str, analysis: StrategyAnalysis) -> str:
    family = _family_key(strategy_id)
    lead = FAMILY_INTROS.get(family, "").rstrip(".") if family else strategy_id

    modifiers: list[str] = []
    provider = provider_hint(strategy_id)
    if provider:
        modifiers.append(f"профиль {provider}")

    variant = _variant_hint(strategy_id)
    if variant:
        modifiers.append(variant)

    if analysis.desync_modes:
        modes = _join_unique([DESYNC_LABELS.get(mode, mode) for mode in sorted(analysis.desync_modes)], 4)
        modifiers.append(modes)

    if analysis.max_repeats >= 10:
        modifiers.append(f"repeats до {analysis.max_repeats}")
    elif analysis.rule_count <= 2 and analysis.arg_count <= 18:
        modifiers.append("компактная конфигурация")

    if modifiers:
        return f"{lead}: {', '.join(modifiers)}."
    return f"{lead}."


def build_bullets(strategy_id: str, analysis: StrategyAnalysis) -> list[str]:
    bullets: list[str] = []

    provider = provider_hint(strategy_id)
    if provider:
        bullets.append(f"Профиль заточен под провайдера {provider}.")

    family = _family_key(strategy_id)
    if family and family in FAMILY_INTROS:
        intro = FAMILY_INTROS[family]
        if intro not in bullets:
            bullets.append(intro)

    if "list-ultimate.txt" in analysis.hostlists:
        bullets.append("list-ultimate.txt — широкий список сервисов форка (Discord, Google, CDN).")
    if any("discord" in name for name in analysis.ipsets):
        bullets.append("ipset-discord — голос/STUN/медиа Discord по IP-диапазонам.")
    if any("cloudflare" in name for name in analysis.ipsets):
        bullets.append("ipset-cloudflare — отдельные правила для CDN Cloudflare.")
    if any("google" in name or "youtube" in name for name in analysis.hostlists):
        bullets.append("Списки Google/YouTube для видео и QUIC-трафика.")
    if any("ubisoft" in name for name in analysis.ipsets):
        bullets.append("ipset-ubisoft — игровые серверы Ubisoft.")
    if any("russia" in name for name in analysis.ipsets):
        bullets.append("ipset-russia — RU-сервисы и CDN.")

    if analysis.has_auto_tls:
        bullets.append("Dynamic fake-tls: decoy строится из вашего ClientHello, а не из статического .bin.")
    if analysis.has_dup:
        bullets.append("dup + dup-ttl: дубликаты пакетов с коротким TTL — DPI часто принимает «лишнюю» копию.")
    if analysis.has_autottl:
        bullets.append("autottl подбирает TTL decoy так, чтобы он не доходил до сервера.")
    if analysis.has_wssize:
        bullets.append("wssize меняет TCP window на SYN — ломает эвристики stateful DPI.")
    if analysis.has_ipv6_ext:
        bullets.append("IPv6 extension headers — для сетей с IPv6-inspection на middlebox.")
    if analysis.has_ipfrag:
        bullets.append("IP fragmentation desync — DPI не собирает фрагменты целиком.")
    if analysis.has_cutoff:
        bullets.append(
            f"cutoff {_join_unique(analysis.cutoff_values, 2)} — desync только на первых пакетах, меньше влияния на сессию."
        )
    if analysis.badseq_increment:
        bullets.append(f"badseq increment={analysis.badseq_increment} — агрессивный seq-desync.")

    for rule_bullet in analysis.rule_bullets:
        if len(bullets) >= 6:
            break
        if rule_bullet not in bullets:
            bullets.append(rule_bullet)

    if len(bullets) < 3:
        if analysis.bin_patterns:
            bullets.append(f"Шаблоны: {_join_unique(analysis.bin_patterns, 3)}.")
        if analysis.l7_filters:
            bullets.append(f"L7-фильтры: {_join_unique(analysis.l7_filters, 4)}.")
    if len(bullets) < 3:
        bullets.append(f"{analysis.rule_count} правил winws, {analysis.arg_count} аргументов.")

    while len(bullets) < 3:
        bullets.append("Уникальная комбинация фильтров и dpi-desync из форка v2.")

    return bullets[:6]


def build_tspu(strategy_id: str, analysis: StrategyAnalysis) -> str:
    paragraphs: list[str] = []

    provider = provider_hint(strategy_id)
    if provider:
        paragraphs.append(
            f"Профиль {strategy_id} собран под особенности DPI {provider}: "
            "набор fooling и split-pos подобран эмпирически в форке v2."
        )

    technique_order = [
        "syndata",
        "multidisorder",
        "disorder2",
        "disorder",
        "hostfakesplit",
        "fakedsplit",
        "multisplit",
        "split2",
        "split",
        "fake",
        "ipfrag",
        "hopbyhop",
        "destopt",
    ]
    for mode in technique_order:
        if mode in analysis.desync_modes and mode in TSPU_BY_TECHNIQUE:
            paragraphs.append(TSPU_BY_TECHNIQUE[mode])

    if "tamper" in analysis.desync_modes:
        paragraphs.append(TSPU_BY_TECHNIQUE["tamper"])
    if analysis.has_quic_fake:
        paragraphs.append(TSPU_BY_TECHNIQUE["quic_fake"])
    if analysis.has_auto_tls:
        paragraphs.append(TSPU_BY_TECHNIQUE["auto_tls"])
    if "badseq" in analysis.fooling:
        paragraphs.append(TSPU_BY_TECHNIQUE["badseq"])
    if "ts" in analysis.fooling:
        paragraphs.append(TSPU_BY_TECHNIQUE["ts"])
    if "md5sig" in analysis.fooling:
        paragraphs.append(TSPU_BY_TECHNIQUE["md5sig"])
    if analysis.has_autottl:
        paragraphs.append(TSPU_BY_TECHNIQUE["autottl"])
    if analysis.has_dup:
        paragraphs.append(TSPU_BY_TECHNIQUE["dup"])
    if analysis.has_wssize:
        paragraphs.append(TSPU_BY_TECHNIQUE["wssize"])
    if analysis.l7_filters:
        paragraphs.append(TSPU_BY_TECHNIQUE["l7"])
    if analysis.has_cutoff:
        paragraphs.append(TSPU_BY_TECHNIQUE["cutoff"])

    if not paragraphs:
        paragraphs.append(
            "WinDivert перехватывает трафик на handshake, winws применяет dpi-desync к выбранным портам и спискам. "
            "DPI классифицирует подменённые или разрезанные сегменты, сервер получает корректный TLS/QUIC."
        )

    # Deduplicate while preserving order
    seen: set[str] = set()
    unique: list[str] = []
    for paragraph in paragraphs:
        if paragraph not in seen:
            seen.add(paragraph)
            unique.append(paragraph)

    return " ".join(unique[:4])


def build_fork_description(strategy_id: str, args: list[str]) -> dict[str, object]:
    analysis = analyze_strategy_args(args)
    return {
        "summary": build_summary(strategy_id, analysis),
        "bullets": build_bullets(strategy_id, analysis),
        "tspu": build_tspu(strategy_id, analysis),
    }
