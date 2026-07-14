#!/usr/bin/env python3
"""Analyze fork pre-configs and print unique strategies vs existing strategies.hpp."""

from __future__ import annotations

import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "utils" / "buildactions" / "generate_zapret_strategies_hpp.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("gen", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def normalize_fork_arg(arg: str) -> str:
    gen = load_generator()
    arg = gen.normalize_variables(arg)
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
    arg = re.sub(r"\.\.(?:/|\\)lists(?:/|\\)list-ultimate\.txt", "${LISTS}list-ultimate.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"\.\.(?:/|\\)lists(?:/|\\)list-google\.txt", "${LISTS}list-google.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"\.\.(?:/|\\)lists(?:/|\\)list-youtube\.txt", "${LISTS}list-youtube.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%LIST_PATH%", "${LISTS}list-ultimate.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%CLOUDFLARE_IPSET_PATH%", "${LISTS}ipset-cloudflare.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%DISCORD_IPSET_PATH%", "${LISTS}ipset-discord.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%UBISOFT_IPSET_PATH%", "${LISTS}ipset-ubisoft.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%RUSSIA_IPSET_PATH%", "${LISTS}ipset-russia.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r"%LISTS%list-ultimate\.txt", "${LISTS}list-ultimate.txt", arg, flags=re.IGNORECASE)
    arg = re.sub(r'@\$\{BIN\}', "${BIN}", arg)
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


def parse_fork_strategy(path: Path, gen) -> dict | None:
    try:
        raw = path.read_text(encoding="utf-8")
        tail = gen.extract_command_tail(raw)
    except (ValueError, UnicodeDecodeError):
        return None

    args = []
    for arg in gen.tokenize_args(tail):
        normalized = normalize_fork_arg(arg)
        if normalized:
            args.append(normalized)

    if not args:
        return None

    return {
        "id": path.stem,
        "file": path.name,
        "args": args,
        "variableRefs": gen.collect_variable_refs(args),
    }


def fingerprint(args: list[str]) -> tuple[str, ...]:
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


def main() -> int:
    gen = load_generator()
    base_dir = ROOT / "vendor" / "zapret-discord-youtube"
    fork_dir = ROOT / "vendor" / "zapret-discord-youtubev2fork" / "pre-configs"

    base = [gen.parse_strategy(path) for path in sorted(base_dir.glob("general*.bat"), key=lambda p: p.name.lower())]
    base_ids = {s["id"] for s in base}
    base_fps = {fingerprint(s["args"]) for s in base}

    fork_paths = sorted(fork_dir.glob("*.bat"), key=lambda p: p.name.lower())
    unique: list[dict] = []
    seen_fps: set[tuple[str, ...]] = set(base_fps)

    for path in fork_paths:
        strategy = parse_fork_strategy(path, gen)
        if strategy is None:
            print(f"SKIP (parse): {path.name}")
            continue

        fp = fingerprint(strategy["args"])
        if strategy["id"] in base_ids:
            print(f"SKIP (id exists): {path.name}")
            continue
        if fp in seen_fps:
            print(f"SKIP (duplicate): {path.name}")
            continue

        seen_fps.add(fp)
        unique.append(strategy)
        print(f"ADD: {path.name} ({len(strategy['args'])} args)")

    print(f"\nBase={len(base)} unique_fork={len(unique)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
