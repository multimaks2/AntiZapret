#!/usr/bin/env python3
"""Generate source/zapret/strategy_descriptions.cpp from strategies_manifest.json."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from strategy_description_builder import build_fork_description


BASE_DESCRIPTIONS: dict[str, dict[str, object]] = {
    "general (ALT)": {
        "summary": "Комбинированный fake+fakedsplit с TCP timestamp fooling — универсальный ALT к base general.",
        "bullets": [
            "UDP/443 и Discord: подмена QUIC Initial готовыми .bin-пакетами (6 повторов)",
            "TCP: fake + fakedsplit — ложный TLS перед разрезанием реального ClientHello",
            "fooling=ts — подмена TCP timestamps, чтобы сбить stateful-отслеживание DPI",
            "fakedsplit-pattern=0x00 и статические fake TLS/HTTP (google.com, max.ru)",
            "Отдельные правила для discord.media, list-google и list-general",
        ],
        "tspu": (
            "ТСПУ анализирует TLS ClientHello и QUIC Initial в потоке. Стратегия шлёт поддельный TLS с тем же 5-tuple, "
            "режет настоящий ClientHello (fakedsplit) и портит TCP timestamps (fooling=ts). DPI часто «склеивает» decoy "
            "или теряет состояние потока, тогда как сервер принимает корректный ClientHello после split."
        ),
    },
    "general (ALT10)": {
        "summary": "Static fake TLS с fooling=ts; general-трафик через 4pda overlap pattern.",
        "bullets": [
            "TCP: только fake (без split) + fooling=ts",
            "Статические fake TLS: google, stun, max.ru HTTP",
            "General-трафик: tls_clienthello_4pda_to.bin — overlap под RU-сайты",
            "Discord: fake-tls-mod=none — минимальная модификация decoy",
            "UDP QUIC fake, repeats=6 на всех UDP-правилах",
        ],
        "tspu": (
            "Decoy ClientHello уводит DPI на статический шаблон, ts ломает tracking. На general list pattern 4pda "
            "имитирует overlap популярного RU TLS — DPI видит «знакомый» fingerprint, реальный пакет проходит отдельно."
        ),
    },
    "general (ALT11)": {
        "summary": "Усиленный fake+multisplit с повышенными repeats для «давления» на stateful DPI.",
        "bullets": [
            "TCP: fake + multisplit одновременно + fooling=ts",
            "Повышенные repeats: UDP=11, TCP=8 — «давление» на stateful DPI",
            "Discord/Google: split-seqovl=681, google pattern",
            "General: split-seqovl=664, tls_clienthello_max_ru.bin",
            "Несколько static fake TLS на каждом TCP-правиле",
        ],
        "tspu": (
            "Двойной удар: fake сбивает классификатор, multisplit ломает reassembly. Больше repeats — больше шансов "
            "переполнить таблицу состояний ТСПУ на дешёвом оборудовании провайдера."
        ),
    },
    "general (ALT12)": {
        "summary": "Гибрид ALT11: hostfakesplit для Google, fake+multisplit для остального.",
        "bullets": [
            "Гибрид ALT11: fake+multisplit для general/discord",
            "Google 443: hostfakesplit вместо fake+multisplit",
            "Discord UDP: доп. stun.bin, repeats=3 (легче, чем у ALT11)",
            "fooling=ts, general pattern max_ru.bin, seqovl=664",
            "Тонкая настройка под разные типы трафика в одной стратегии",
        ],
        "tspu": (
            "Разные приёмы под разные списки: Google получает hostfakesplit (подмена host в сегменте), "
            "остальной трафик — классический fake+split. Снижает ложные срабатывания на Discord UDP."
        ),
    },
    "general (ALT2)": {
        "summary": "Multisplit с split-pos=2 — смещение точки разреза относительно base general.",
        "bullets": [
            "TCP: только multisplit (как base general), без fake",
            "split-pos=2 — разрез глубже, чем у general (pos=1)",
            "split-seqovl=652, единый pattern tls_clienthello_www_google_com.bin",
            "Нет fooling — чистая атака на reassembly DPI",
            "Подходит, если DPI «научился» на split-pos=1",
        ],
        "tspu": (
            "DPI часто заточен под split-pos=1. Сдвиг на 2 байта меняет байтовую картину reassembly — "
            "middlebox читает неверный SNI, сервер получает целый ClientHello."
        ),
    },
    "general (ALT3)": {
        "summary": "Hostfakesplit с подменой SNI на google.com/ya.ru и rnd,dupsid randomization.",
        "bullets": [
            "Hostfakesplit с подменой SNI на google.com/ya.ru",
            "fake-tls-mod=rnd,dupsid — random session id и random bytes",
            "fooling=ts на TCP-правилах",
            "UDP fake QUIC с repeats=6",
            "Отдельные hostlist для google/general/discord",
        ],
        "tspu": (
            "В сегмент попадает SNI «белого» домена. rnd,dupsid меняет session ID и random каждый раз — "
            "ТСПУ не может заблокировать по статическому fingerprint decoy."
        ),
    },
    "general (ALT4)": {
        "summary": "Fake+multisplit с badseq increment 1000 — агрессивный TCP seq desync.",
        "bullets": [
            "fake + multisplit на TCP 443",
            "fooling=badseq с increment=1000",
            "Static fake TLS (google, stun, max.ru)",
            "UDP fake QUIC, repeats=6",
            "Для провайдеров с жёстким TLS inspection",
        ],
        "tspu": (
            "Большой скачок sequence number заставляет DPI считать поток «битым» и прекратить deep inspection, "
            "при этом end-host TCP stack принимает данные (increment подобран под tolerance стека)."
        ),
    },
    "general (ALT5)": {
        "summary": "⚠ Экстремальный syndata+multidisorder на L3 — не рекомендуется.",
        "bullets": [
            "syndata + multidisorder на TCP",
            "Экстремальный режим, может ломать игры/VoIP",
            "Использовать только если остальное не помогло",
            "Высокая нагрузка на CPU и latency",
            "Не для постоянного использования",
        ],
        "tspu": (
            "Полное переупорядочивание TCP-сегментов на старте соединения. Может сломать DPI flow tracking целиком, "
            "но нестабильно для игр/VoIP и часто хуже по latency — используйте только если ничего else не работает."
        ),
    },
    "general (ALT6)": {
        "summary": "Multisplit с единым google pattern (seqovl=681) — без ветки 4pda как у general.",
        "bullets": [
            "TCP: multisplit без fake — как general, но единый pattern",
            "Все TCP-правила: split-seqovl=681, tls_clienthello_www_google_com.bin",
            "В отличие от general: нет 4pda/seqovl=568 на general list",
            "Google 443: ip-id=zero; UDP fake QUIC repeats=6",
            "Простой multisplit-профиль без fake injection",
        ],
        "tspu": (
            "Классический multisplit: DPI собирает ClientHello из частей с overlap-pattern, видит «левый» SNI, "
            "сервер получает оригинал. От general отличается тем, что general list тоже идёт через google pattern/681, без 4pda."
        ),
    },
    "general (ALT7)": {
        "summary": "Multisplit на sniext+1 — разрез у границы SNI extension.",
        "bullets": [
            "split-pos=2,sniext+1 — разрез у SNI extension",
            "split-seqovl=679, google pattern",
            "Основной режим: multisplit без fake",
            "ipset-all TCP: отдельно syndata",
            "Для DPI с парсингом TLS extensions",
        ],
        "tspu": (
            "Парсеры ТСПУ часто читают длину SNI extension. Разрез sniext+1 кладёт decoy в extension area — "
            "классификатор получает мусор, валидный SNI уезжает в следующий сегмент."
        ),
    },
    "general (ALT8)": {
        "summary": "Мягкий fake с badseq+2 и fake-tls-mod=none.",
        "bullets": [
            "fake без split + fooling=badseq increment=2",
            "fake-tls-mod=none — decoy без модификаций",
            "Статические fake TLS из .bin",
            "Мягкий режим для нестабильных провайдеров",
            "UDP fake QUIC repeats=6",
        ],
        "tspu": (
            "Минимальные изменения decoy — для DPI, который блокирует «тяжёлые» fake. Небольшой badseq сбивает "
            "sequence tracking без разрыва соединения."
        ),
    },
    "general (ALT9)": {
        "summary": "Hostfakesplit: google.com на Discord/Google, ozon.ru+md5sig на general.",
        "bullets": [
            "hostfakesplit на discord.media / google / general",
            "general list: host=ozon.ru + fooling=ts,md5sig",
            "discord/google: host=www.google.com, fooling=ts, repeats=4",
            "UDP fake QUIC, repeats=6",
            "RU whitelist fingerprint (ozon.ru)",
        ],
        "tspu": (
            "Подмена Host на whitelist-домены (google.com / ozon.ru). md5sig на general добавляет нестандартные TCP "
            "options; repeats=4 снижает нагрузку."
        ),
    },
    "general (EXP)": {
        "summary": "Экспериментальный гибрид: fake+multisplit, hostfakesplit на Google, stun2/max_ru.",
        "bullets": [
            "Экспериментальный гибрид Flowseal: fake+multisplit + hostfakesplit",
            "Google 443: hostfakesplit host=www.google.com, fooling=ts",
            "General/ipset: fake+multisplit, seqovl=480, pattern stun2.bin, fake max_ru",
            "QUIC fake repeats=11; Discord UDP fake repeats=4",
            "GameFilter TCP/UDP с any-protocol и cutoff=n4",
        ],
        "tspu": (
            "Google уходит через hostfakesplit (whitelist SNI). Остальной TCP — fake+multisplit с seqovl=480 и pattern "
            "stun2.bin / decoy max_ru: DPI видит «белый» overlap и decoy, сервер собирает настоящий ClientHello. "
            "QUIC и Discord UDP закрываются отдельным fake."
        ),
    },
    "general (FAKE TLS AUTO ALT)": {
        "summary": "Fake+fakedsplit с badseq+2 и tls-mod (rnd/dupsid/SNI google).",
        "bullets": [
            "fake + fakedsplit на TCP 443",
            "fooling=badseq increment=2",
            "fake-tls-mod=rnd,dupsid,sni=www.google.com",
            "QUIC fake repeats=11; TCP repeats=8",
            "Без fake-tls=! — decoy через tls-mod, не live clone",
        ],
        "tspu": (
            "Decoy модифицируется через fake-tls-mod (не live clone). Fakedsplit режет оригинал рядом; badseq+2 "
            "сбивает sequence tracking DPI без жёсткого разрыва соединения."
        ),
    },
    "general (FAKE TLS AUTO ALT2)": {
        "summary": "Fake+multisplit + extreme badseq (10M).",
        "bullets": [
            "fake + multisplit, split-seqovl=681",
            "fooling=badseq increment=10000000",
            "fake-tls-mod=rnd,dupsid,sni=www.google.com",
            "Экстремальный seq jump",
            "Только для жёстких блокировок",
        ],
        "tspu": (
            "Экстремальный badseq заставляет stateful middlebox сбросить состояние потока. fake-tls-mod рандомизирует "
            "decoy; multisplit ломает reassembly SNI."
        ),
    },
    "general (FAKE TLS AUTO ALT3)": {
        "summary": "Fake+multisplit + ts fooling (мягче ALT2).",
        "bullets": [
            "fake + multisplit, split-seqovl=681",
            "fooling=ts вместо badseq",
            "fake-tls-mod=rnd,dupsid,sni=www.google.com",
            "Мягче ALT2, стабильнее для VoIP",
            "TCP repeats=8",
        ],
        "tspu": (
            "Тот же fake+multisplit с tls-mod, но timestamps вместо seq jump — для провайдеров, где badseq режет "
            "соединение. Multisplit всё ещё ломает reassembly SNI."
        ),
    },
    "general (FAKE TLS AUTO)": {
        "summary": "Fake + multidisorder + live TLS clone (fake-tls=!) — флагман AUTO.",
        "bullets": [
            "fake + multidisorder (split-pos=1,midsld)",
            "fake-tls=! — live ClientHello clone",
            "SNI google.com в decoy (rnd,dupsid)",
            "fooling=badseq, repeats=11",
            "Сильный режим для TLS inspection",
        ],
        "tspu": (
            "multidisorder переставляет сегменты вокруг середины SLD; fake-tls=! клонирует ваш ClientHello. "
            "fooling=badseq. DPI парсит TLS out-of-order и не находит блокируемый SNI."
        ),
    },
    "general (SIMPLE FAKE ALT)": {
        "summary": "Simple fake с badseq вместо ts.",
        "bullets": [
            "Только fake, без split",
            "fooling=badseq вместо ts",
            "Static fake TLS из .bin",
            "Минимальная сложность",
            "UDP fake QUIC",
        ],
        "tspu": "Только injection decoy без split. badseq desync — когда ts fooling уже в blacklist провайдера.",
    },
    "general (SIMPLE FAKE ALT2)": {
        "summary": "Simple fake + max_ru pattern + ранний cutoff.",
        "bullets": [
            "fake + tls_clienthello_max_ru.bin",
            "cutoff=n5 — раннее прекращение desync",
            "fooling=ts",
            "Static decoy",
            "Меньше влияния на long-lived соединения",
        ],
        "tspu": (
            "Decoy с RU TLS profile на general list. cutoff=n5 прекращает desync раньше — меньше шанс "
            "сломать long-lived соединения после handshake."
        ),
    },
    "general (SIMPLE FAKE)": {
        "summary": "Простейший режим: static fake TLS + ts, без split.",
        "bullets": [
            "TCP: fake only — без multisplit/fakedsplit",
            "Static fake TLS (google, stun) + fake-http=max_ru",
            "fooling=ts, repeats=6",
            "Game TCP cutoff=n4",
            "Самый простой режим — только ложные ClientHello",
        ],
        "tspu": (
            "Перед настоящим ClientHello вставляется копия из .bin. DPI засчитывает decoy как «просмотренный» "
            "пакет; ts мешает привязать decoy к потоку. Минимум сложности, часто достаточно на слабом DPI."
        ),
    },
    "general": {
        "summary": "Базовая: multisplit TLS без fake — референс zapret-discord-youtube.",
        "bullets": [
            "Базовая стратегия zapret-discord-youtube",
            "TCP: multisplit — разрез TLS ClientHello без fake-пакетов",
            "Discord/Google: split-seqovl=681, pos=1, google pattern",
            "General: split-seqovl=568, tls_clienthello_4pda_to.bin",
            "Google 443: ip-id=zero; UDP fake QUIC repeats=6/12",
        ],
        "tspu": (
            "WinDivert перехватывает SYN/данные, winws вставляет split-сегменты с seq overlap. DPI reassembler "
            "склеивает bytes 0..N из pattern (google/4pda), не видит Discord/YouTube SNI. Сервер игнорирует "
            "overlap-bytes по TCP semantics и читает настоящий ClientHello."
        ),
    },
}


def generate_description(strategy_id: str, args: list[str]) -> dict[str, object]:
    if strategy_id in BASE_DESCRIPTIONS:
        return BASE_DESCRIPTIONS[strategy_id]
    return build_fork_description(strategy_id, args)


def cpp_string(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def symbol_name(strategy_id: str, index: int) -> str:
    symbol = re.sub(r"[^0-9A-Za-z]+", "_", strategy_id).strip("_")
    if not symbol:
        symbol = f"strategy_{index}"
    if symbol[0].isdigit():
        symbol = f"s_{symbol}"
    return f"kBullets_{symbol.lower()}_{index}"


def render_cpp(strategies: list[dict]) -> str:
    bullet_blocks: list[str] = []
    description_entries: list[str] = []

    for index, strategy in enumerate(strategies):
        desc = generate_description(strategy["id"], strategy["args"])
        bullets = desc["bullets"]
        bullet_name = symbol_name(strategy["id"], index)
        bullet_lines = [f"\tconst char* {bullet_name}[] = {{"]
        for bullet in bullets:
            bullet_lines.append(f"\t\t{cpp_string(str(bullet))},")
        bullet_lines.append("\t\tnullptr")
        bullet_lines.append("\t};")
        bullet_blocks.append("\n".join(bullet_lines))

        description_entries.append(
            "\t\t{\n"
            f"\t\t\t{cpp_string(str(desc['summary']))},\n"
            f"\t\t\t{bullet_name},\n"
            f"\t\t\t{cpp_string(str(desc['tspu']))}\n"
            "\t\t}"
        )

    smart_bullets = [
        "\tconst char* kSmartStrategyBullets[] = {",
        '\t\t"Мутирует параметры winws (dpi-desync, repeats, fooling) от шаблона general",',
        '\t\t"«Подбор умной» перебирает варианты и сохраняет лучший конфиг",',
        '\t\t"Учитывает Discord, YouTube, Telegram/MTProto и ping",',
        "\t\tnullptr",
        "\t};",
    ]

    lines = [
        '#include "zapret/strategy_descriptions.h"',
        "",
        '#include "zapret/strategies.hpp"',
        "",
        "namespace",
        "{",
        *bullet_blocks,
        "",
        *smart_bullets,
        "",
        "\tconst StrategyDescription kDescriptions[] = {",
        ",\n".join(description_entries),
        "\t};",
        "",
        "\tstatic_assert(",
        "\t\tsizeof(kDescriptions) / sizeof(kDescriptions[0]) == ZapretStrategies::kStrategyCount,",
        '\t\t"Strategy descriptions must match kStrategyCount");',
        "",
        "\tconst StrategyDescription kSmartDescription = {",
        '\t\t"Умная стратегия: подбор собственных аргументов winws на базе general.",',
        "\t\tkSmartStrategyBullets,",
        '\t\t"В отличие от «Автовыбора лучшей», здесь не выбирается готовый .bat — "',
        '\t\t"алгоритм меняет dpi-desync, repeats и fooling, тестирует и сохраняет лучший набор. "',
        '\t\t"Профиль хранится в smart_strategy.ini."',
        "\t};",
        "}",
        "",
        "const StrategyDescription* StrategyDescriptions::GetByIndex(int strategyIndex)",
        "{",
        "\tif (strategyIndex < 0 || strategyIndex >= static_cast<int>(ZapretStrategies::kStrategyCount))",
        "\t\treturn nullptr;",
        "\treturn &kDescriptions[static_cast<size_t>(strategyIndex)];",
        "}",
        "",
        "const StrategyDescription* StrategyDescriptions::GetSmartStrategy()",
        "{",
        "\treturn &kSmartDescription;",
        "}",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--manifest",
        default="source/zapret/strategies_manifest.json",
        help="Strategy manifest generated together with strategies.hpp",
    )
    parser.add_argument(
        "--output",
        default="source/zapret/strategy_descriptions.cpp",
        help="Generated descriptions source path",
    )
    args = parser.parse_args()

    manifest_path = Path(args.manifest)
    if not manifest_path.is_file():
        raise SystemExit(f"Manifest not found: {manifest_path}")

    strategies = json.loads(manifest_path.read_text(encoding="utf-8"))
    output = Path(args.output)
    output.write_text(render_cpp(strategies), encoding="utf-8")
    print(f"Generated {output} ({len(strategies)} descriptions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
