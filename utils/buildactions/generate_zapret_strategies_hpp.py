#!/usr/bin/env python3
"""Parse vendor/zapret-discord-youtube general*.bat and emit source/zapret/strategies.hpp."""

from __future__ import annotations

import argparse
import re
import sys
from functools import cmp_to_key
from pathlib import Path
from typing import Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from strategy_display_labels import build_extra_display_label


GAME_FILTER_MODES = [
    ("Disabled", "disabled", "12", "12", "12"),
    ("All", "all", "1024-65535", "1024-65535", "1024-65535"),
    ("Tcp", "tcp", "1024-65535", "1024-65535", "12"),
    ("Udp", "udp", "1024-65535", "12", "1024-65535"),
]

RUNTIME_VARIABLES = ("GameFilter", "GameFilterTCP", "GameFilterUDP")
PATH_VARIABLES = ("BIN", "LISTS")
BASE_STRATEGY_ID = "general"


def compare_strategy_names_natural(left: str, right: str) -> int:
    """Explorer-like sort: ALT2 before ALT10; digits compared numerically."""
    i = 0
    j = 0
    while i < len(left) and j < len(right):
        char_left = left[i]
        char_right = right[j]
        digit_left = char_left.isdigit()
        digit_right = char_right.isdigit()
        if digit_left and digit_right:
            ai = i
            while ai < len(left) and left[ai].isdigit():
                ai += 1
            bj = j
            while bj < len(right) and right[bj].isdigit():
                bj += 1
            value_left = int(left[i:ai])
            value_right = int(right[j:bj])
            if value_left != value_right:
                return -1 if value_left < value_right else 1
            i = ai
            j = bj
            continue

        lower_left = char_left.lower()
        lower_right = char_right.lower()
        if lower_left != lower_right:
            return -1 if lower_left < lower_right else 1
        i += 1
        j += 1

    if len(left) == len(right):
        return 0
    return -1 if len(left) < len(right) else 1


def sort_strategy_names_natural(names: list[str]) -> list[str]:
    return sorted(names, key=cmp_to_key(compare_strategy_names_natural))


def sort_paths_by_strategy_name(paths: list[Path]) -> list[Path]:
    return sorted(paths, key=cmp_to_key(lambda left, right: compare_strategy_names_natural(left.stem, right.stem)))


def order_base_strategy_paths(bat_files: list[Path]) -> list[Path]:
    general = [path for path in bat_files if path.stem == BASE_STRATEGY_ID]
    others = sort_paths_by_strategy_name([path for path in bat_files if path.stem != BASE_STRATEGY_ID])
    return general + others


def normalize_variables(text: str) -> str:
    for name in RUNTIME_VARIABLES:
        text = text.replace(f"%{name}%", f"${{{name}}}")
    for name in PATH_VARIABLES:
        text = text.replace(f"%{name}%", f"${{{name}}}")
    return text


def strip_bat_continuations(text: str) -> str:
    return re.sub(r"\^\s*(?:\r?\n)", " ", text)


def extract_command_tail(content: str) -> str:
    content = strip_bat_continuations(content)
    match = re.search(r'winws\.exe"\s+(.*)', content, flags=re.IGNORECASE | re.DOTALL)
    if not match:
        match = re.search(r"winws\.exe\s+(.*)", content, flags=re.IGNORECASE | re.DOTALL)
    if not match:
        raise ValueError("winws.exe launch line not found")
    tail = match.group(1).strip()
    tail = re.split(r"\r?\n\s*(?:@echo|call |cd |set |echo:|echo |chcp |::)", tail, maxsplit=1)[0]
    return tail.strip()


def tokenize_args(command: str) -> list[str]:
    args: list[str] = []
    current: list[str] = []
    in_quotes = False
    i = 0
    while i < len(command):
        ch = command[i]
        if ch == '"':
            in_quotes = not in_quotes
            i += 1
            continue
        if ch.isspace() and not in_quotes:
            if current:
                args.append("".join(current))
                current = []
            i += 1
            continue
        current.append(ch)
        i += 1
    if current:
        args.append("".join(current))
    return args


def normalize_arg(arg: str) -> str:
    arg = normalize_variables(arg.strip())
    if arg.startswith('"') and arg.endswith('"'):
        arg = arg[1:-1]
    return arg


def collect_variable_refs(args: Iterable[str]) -> list[str]:
    refs: list[str] = []
    seen: set[str] = set()
    for arg in args:
        for match in re.finditer(r"\$\{([A-Za-z0-9_]+)\}", arg):
            name = match.group(1)
            if name not in seen:
                seen.add(name)
                refs.append(name)
    return refs


def cpp_string_literal(value: str) -> str:
    escaped = (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )
    return f'"{escaped}"'


def normalize_fork_arg(arg: str) -> str:
    arg = normalize_variables(arg)
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)list-ultimate\.txt"',
        "${LISTS}list-ultimate.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)list-discord\.txt"',
        "${LISTS}list-discord.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)list-google\.txt"',
        "${LISTS}list-google.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)list-youtube\.txt"',
        "${LISTS}list-youtube.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)ipset-cloudflare\.txt"',
        "${LISTS}ipset-cloudflare.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)ipset-discord\.txt"',
        "${LISTS}ipset-discord.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)ipset-ubisoft\.txt"',
        "${LISTS}ipset-ubisoft.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r'"[^"]*(?:/|\\)lists(?:/|\\)ipset-russia\.txt"',
        "${LISTS}ipset-russia.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r"\.\.(?:/|\\)lists(?:/|\\)list-ultimate\.txt",
        "${LISTS}list-ultimate.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r"\.\.(?:/|\\)lists(?:/|\\)list-google\.txt",
        "${LISTS}list-google.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(
        r"\.\.(?:/|\\)lists(?:/|\\)list-youtube\.txt",
        "${LISTS}list-youtube.txt",
        arg,
        flags=re.IGNORECASE,
    )
    arg = re.sub(r"%LIST_PATH%", "${LISTS}list-ultimate.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%CLOUDFLARE_IPSET_PATH%", "${LISTS}ipset-cloudflare.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%DISCORD_IPSET_PATH%", "${LISTS}ipset-discord.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%UBISOFT_IPSET_PATH%", "${LISTS}ipset-ubisoft.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%RUSSIA_IPSET_PATH%", "${LISTS}ipset-russia.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%LISTS%list-ultimate\.txt", "${LISTS}list-ultimate.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"@tls_", "${BIN}tls_", arg)
    arg = re.sub(r"@quic_", "${BIN}quic_", arg)

    if arg.startswith("--wf-tcp=") or arg.startswith("--filter-tcp="):
        arg = arg.replace("%GModeRange%", "${GameFilterTCP}")
    elif arg.startswith("--wf-udp=") or arg.startswith("--filter-udp="):
        arg = arg.replace("%GModeRange%", "${GameFilterUDP}")
    else:
        arg = arg.replace("%GModeRange%", "${GameFilter}")

    if arg.startswith('"') and arg.endswith('"'):
        arg = arg[1:-1]
    return arg


def fingerprint(args: Iterable[str]) -> tuple[str, ...]:
    normalized: list[str] = []
    for arg in args:
        item = arg
        item = re.sub(r"\$\{LISTS\}[^\s]+", "${LISTS}*", item)
        item = re.sub(r"\$\{BIN\}[^\\/\s]+", "${BIN}*", item)
        item = item.replace("${GameFilterTCP}", "${GF}")
        item = item.replace("${GameFilterUDP}", "${GF}")
        item = item.replace("${GameFilter}", "${GF}")
        normalized.append(item)
    return tuple(normalized)


def parse_fork_strategy(path: Path) -> dict | None:
    try:
        raw = path.read_text(encoding="utf-8")
        tail = extract_command_tail(raw)
    except (ValueError, UnicodeDecodeError):
        return None

    args = []
    for arg in tokenize_args(tail):
        normalized = normalize_fork_arg(arg)
        if normalized:
            args.append(normalized)

    if not args:
        return None

    strategy_id = path.stem
    return {
        "id": strategy_id,
        "file": path.name,
        "args": args,
        "variableRefs": collect_variable_refs(args),
        "isExtra": True,
    }


def load_base_strategies(vendor_dir: Path) -> list[dict]:
    bat_files = list(vendor_dir.glob("general*.bat"))
    if not bat_files:
        raise SystemExit(f"No general*.bat files found in {vendor_dir}")
    ordered_paths = order_base_strategy_paths(bat_files)
    return [parse_strategy(path, is_extra=False) for path in ordered_paths]


def load_fork_strategies(fork_dir: Path, base: list[dict]) -> list[dict]:
    if not fork_dir.is_dir():
        return []

    base_ids = {strategy["id"] for strategy in base}
    base_fps = {fingerprint(strategy["args"]) for strategy in base}
    seen_fps = set(base_fps)
    unique: list[dict] = []

    for path in sort_paths_by_strategy_name(list(fork_dir.glob("*.bat"))):
        strategy = parse_fork_strategy(path)
        if strategy is None:
            continue
        if strategy["id"] in base_ids:
            continue
        fp = fingerprint(strategy["args"])
        if fp in seen_fps:
            continue
        seen_fps.add(fp)
        unique.append(strategy)

    return unique


def merge_strategies(base: list[dict], fork: list[dict]) -> list[dict]:
    fork_sorted = sorted(
        fork,
        key=cmp_to_key(lambda left, right: compare_strategy_names_natural(left["id"], right["id"])),
    )
    return base + fork_sorted


def make_symbol_name(strategy_id: str, index: int) -> str:
    symbol = re.sub(r"[^0-9A-Za-z]+", "_", strategy_id).strip("_")
    if not symbol:
        symbol = "strategy"
    if symbol[0].isdigit():
        symbol = f"s_{symbol}"
    return f"{symbol.lower()}_{index}"


def parse_strategy(path: Path, is_extra: bool = False) -> dict:
    raw = path.read_text(encoding="utf-8")
    tail = extract_command_tail(raw)
    args = [normalize_arg(arg) for arg in tokenize_args(tail) if normalize_arg(arg)]
    strategy_id = path.stem
    return {
        "id": strategy_id,
        "file": path.name,
        "args": args,
        "variableRefs": collect_variable_refs(args),
        "isExtra": is_extra,
    }


def render_header(strategies: list[dict], source_dir: str) -> str:
    general_index = next(
        (index for index, strategy in enumerate(strategies) if strategy["id"] == BASE_STRATEGY_ID),
        len(strategies) - 1,
    )
    lines: list[str] = [
        "#pragma once",
        "",
        "// Auto-generated by utils/buildactions/generate_zapret_strategies_hpp.py",
        "// Do not edit manually.",
        "",
        "#include <cstddef>",
        "#include <string_view>",
        "",
        "namespace ZapretStrategies",
        "{",
        "",
        "inline constexpr const char* kExecutable = \"bin/winws.exe\";",
        f"inline constexpr const char* kSourceDir = \"{source_dir}\";",
        "",
        "enum class GameFilterMode",
        "{",
    ]
    for index, (enum_name, _, _, _, _) in enumerate(GAME_FILTER_MODES):
        suffix = "," if index + 1 < len(GAME_FILTER_MODES) else ""
        lines.append(f"\t{enum_name}{suffix}")
    lines.extend(
        [
            "};",
            "",
            "struct GameFilterValues",
            "{",
            "\tstd::string_view gameFilter;",
            "\tstd::string_view gameFilterTcp;",
            "\tstd::string_view gameFilterUdp;",
            "};",
            "",
            "inline constexpr GameFilterValues kGameFilterModes[] =",
            "{",
        ]
    )
    for index, (_, mode_id, gf, gtcp, gudp) in enumerate(GAME_FILTER_MODES):
        suffix = "," if index + 1 < len(GAME_FILTER_MODES) else ""
        lines.append(f'\t{{ "{gf}", "{gtcp}", "{gudp}" }}{suffix}  // {mode_id}')
    lines.extend(
        [
            "};",
            "",
            "inline constexpr GameFilterMode kDefaultGameFilterMode = GameFilterMode::Disabled;",
            "",
            "struct StrategyDefinition",
            "{",
            "\tstd::string_view id;",
            "\tstd::string_view label;",
            "\tstd::string_view file;",
            "\tconst char* const* args;",
            "\tstd::size_t argCount;",
            "\tconst char* const* variableRefs;",
            "\tstd::size_t variableRefCount;",
            "\tbool isExtra;",
            "};",
            "",
            "namespace detail",
            "{",
            "\tinline constexpr const char* kEmptyVariableRefs[] = { nullptr };",
            "",
        ]
    )

    for index, strategy in enumerate(strategies):
        symbol = make_symbol_name(strategy["id"], index)
        lines.append(f"\tnamespace {symbol}")
        lines.append("\t{")
        lines.append("\t\tinline constexpr const char* kArgs[] =")
        lines.append("\t\t{")
        for arg_index, arg in enumerate(strategy["args"]):
            suffix = "," if arg_index + 1 < len(strategy["args"]) else ""
            lines.append(f"\t\t\t{cpp_string_literal(arg)}{suffix}")
        lines.append("\t\t};")
        refs = strategy["variableRefs"]
        if refs:
            lines.append("")
            lines.append("\t\tinline constexpr const char* kVariableRefs[] =")
            lines.append("\t\t{")
            for ref_index, ref in enumerate(refs):
                suffix = "," if ref_index + 1 < len(refs) else ""
                lines.append(f'\t\t\t"{ref}"{suffix}')
            lines.append("\t\t};")
        lines.append("\t}  // namespace " + symbol)
        lines.append("")

    lines.extend(
        [
            "}  // namespace detail",
            "",
            "inline constexpr StrategyDefinition kStrategies[] =",
            "{",
        ]
    )
    for index, strategy in enumerate(strategies):
        symbol = make_symbol_name(strategy["id"], index)
        suffix = "," if index + 1 < len(strategies) else ""
        refs = strategy["variableRefs"]
        if refs:
            ref_ptr = f"detail::{symbol}::kVariableRefs"
            ref_count = f"sizeof(detail::{symbol}::kVariableRefs) / sizeof(detail::{symbol}::kVariableRefs[0])"
        else:
            ref_ptr = "detail::kEmptyVariableRefs"
            ref_count = "0"

        lines.append(
            "\t{ "
            f'{cpp_string_literal(strategy["id"])}, '
            f'{cpp_string_literal(strategy["label"])}, '
            f'{cpp_string_literal(strategy["file"])}, '
            f"detail::{symbol}::kArgs, "
            f"sizeof(detail::{symbol}::kArgs) / sizeof(detail::{symbol}::kArgs[0]), "
            f"{ref_ptr}, "
            f"{ref_count}, "
            f"{'true' if strategy['isExtra'] else 'false'} "
            f"}}{suffix}"
        )
    lines.extend(
        [
            "};",
            "",
            "inline constexpr std::size_t kStrategyCount = sizeof(kStrategies) / sizeof(kStrategies[0]);",
            "",
            f"inline constexpr int kGeneralStrategyIndex = {general_index};",
            "",
            "inline constexpr std::size_t kBaseStrategyCount =",
            "\t[]() constexpr -> std::size_t {",
            "\t\tstd::size_t count = 0;",
            "\t\tfor (const StrategyDefinition& strategy : kStrategies)",
            "\t\t\tif (!strategy.isExtra)",
            "\t\t\t\t++count;",
            "\t\treturn count;",
            "\t}();",
            "",
            "inline bool IsStrategyVisible(int strategyIndex, bool showExtraStrategies)",
            "{",
            "\tif (strategyIndex < 0 || strategyIndex >= static_cast<int>(kStrategyCount))",
            "\t\treturn false;",
            "\tif (showExtraStrategies)",
            "\t\treturn true;",
            "\treturn !kStrategies[static_cast<std::size_t>(strategyIndex)].isExtra;",
            "}",
            "",
            "inline int CountVisibleStrategies(bool showExtraStrategies)",
            "{",
            "\tint count = 0;",
            "\tfor (std::size_t i = 0; i < kStrategyCount; ++i)",
            "\t\tif (IsStrategyVisible(static_cast<int>(i), showExtraStrategies))",
            "\t\t\t++count;",
            "\treturn count;",
            "}",
            "",
            "inline int GetVisibleStrategyAt(int visibleIndex, bool showExtraStrategies)",
            "{",
            "\tint seen = 0;",
            "\tfor (std::size_t i = 0; i < kStrategyCount; ++i)",
            "\t{",
            "\t\tif (!IsStrategyVisible(static_cast<int>(i), showExtraStrategies))",
            "\t\t\tcontinue;",
            "\t\tif (seen == visibleIndex)",
            "\t\t\treturn static_cast<int>(i);",
            "\t\t++seen;",
            "\t}",
            "\treturn -1;",
            "}",
            "",
            "inline int FindVisibleStrategyPosition(int strategyIndex, bool showExtraStrategies)",
            "{",
            "\tif (!IsStrategyVisible(strategyIndex, showExtraStrategies))",
            "\t\treturn -1;",
            "\tint position = 0;",
            "\tfor (int i = 0; i < strategyIndex; ++i)",
            "\t{",
            "\t\tif (IsStrategyVisible(i, showExtraStrategies))",
            "\t\t\t++position;",
            "\t}",
            "\treturn position;",
            "}",
            "",
            "inline std::string_view GetStrategyLabel(int strategyIndex)",
            "{",
            "\tif (strategyIndex < 0 || strategyIndex >= static_cast<int>(kStrategyCount))",
            "\t\treturn {};",
            "\treturn kStrategies[static_cast<std::size_t>(strategyIndex)].label;",
            "}",
            "",
            "inline constexpr GameFilterValues GetGameFilterValues(GameFilterMode mode)",
            "{",
            "\tswitch (mode)",
            "\t{",
        ]
    )
    for enum_name, _, _, _, _ in GAME_FILTER_MODES:
        lines.append(f"\tcase GameFilterMode::{enum_name}:")
        lines.append(f"\t\treturn kGameFilterModes[static_cast<std::size_t>(GameFilterMode::{enum_name})];")
    lines.extend(
        [
            "\t}",
            "\treturn kGameFilterModes[static_cast<std::size_t>(kDefaultGameFilterMode)];",
            "}",
            "",
            "}  // namespace ZapretStrategies",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--vendor-dir",
        default="vendor/zapret-discord-youtube",
        help="Path to zapret-discord-youtube checkout",
    )
    parser.add_argument(
        "--output",
        default="source/zapret/strategies.hpp",
        help="Generated header path",
    )
    parser.add_argument(
        "--fork-dir",
        default="",
        help="Optional fork pre-configs directory with additional unique strategies",
    )
    parser.add_argument(
        "--manifest",
        default="source/zapret/strategies_manifest.json",
        help="Optional JSON manifest of merged strategies for description generation",
    )
    args = parser.parse_args()

    vendor_dir = Path(args.vendor_dir)
    base = load_base_strategies(vendor_dir)
    fork = load_fork_strategies(Path(args.fork_dir), base)
    strategies = merge_strategies(base, fork)
    for index, strategy in enumerate(strategies):
        if strategy["isExtra"]:
            strategy["label"] = build_extra_display_label(strategy["id"], index)
        else:
            strategy["label"] = strategy["id"]

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render_header(strategies, args.vendor_dir.replace("\\", "/")), encoding="utf-8")

    if args.manifest:
        import json

        manifest_path = Path(args.manifest)
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(
            json.dumps(strategies, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )

    print(f"Generated {output} ({len(strategies)} strategies, +{len(fork)} from fork)")
    for strategy in strategies:
        print(f"  - {strategy['id']}: {len(strategy['args'])} args, vars={','.join(strategy['variableRefs'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
