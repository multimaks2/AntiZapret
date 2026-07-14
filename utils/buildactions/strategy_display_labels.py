"""Short UI labels for extra (fork) strategies only."""

from __future__ import annotations

import re

EXTRA_DISPLAY_LABELS: dict[str, str] = {
    "aggressive": "Макс. давление",
    "discord-optimized": "DC · Opt",
    "discord-voice-only": "DC · Voice",
    "DiscordFix": "DC · Base",
    "DiscordFix (для Билайн, Ростелеком, Инфолинк)": "DC · RTK",
    "DiscordFix (для МГТС)": "DC · MGTS",
    "DiscordFix v9.2": "DC · 9.2",
    "DiscordFix v9.3": "DC · 9.3",
    "DiscordFix_ALT": "DC · ALT",
    "gaming": "Для игр",
    "general (ALT13)": "Gen · ALT13",
    "general (ALT14)": "Gen · ALT14",
    "general (FAKE TLS ALT)": "Gen · FakeTLS",
    "general (FAKE TLS AUTO ALT4)": "Gen · AutoTLS4",
    "general (FAKE TLS)": "Gen · FakeTLS",
    "general (HOSTFAKESPLIT)": "Gen · HostSplit",
    "general (IPFRAG)": "Gen · IPfrag",
    "general (IPv6 DESTOPT)": "Gen · IPv6 opt",
    "general (IPv6 HOPBYHOP)": "Gen · IPv6 hop",
    "general (L7 FILTER)": "Gen · L7",
    "general (MULTIDISORDER AUTO)": "Gen · Disorder",
    "general (SNIEXT)": "Gen · SNIext",
    "general (SYNDATA v2)": "Gen · Syndata2",
    "general (WSSIZE)": "Gen · WSSize",
    "general (МГТС)": "Gen · MGTS",
    "general (МГТС2)": "Gen · MGTS2",
    "general v9.2": "Gen · 9.2",
    "general v9.2 (ALT)": "Gen · 9.2 ALT",
    "general v9.2 (Beeline-Rostelecom)": "Gen · 9.2 RTK",
    "general v9.2 (FAKE TLS AUTO)": "Gen · 9.2 Auto",
    "general v9.2 (MGTS)": "Gen · 9.2 MGTS",
    "general v9.2 (SIMPLE FAKE)": "Gen · 9.2 Simple",
    "general v9.2 (SYNDATA)": "Gen · 9.2 Syndata",
    "general v9.3": "Gen · 9.3",
    "general v9.3 (DUP)": "Gen · 9.3 Dup",
    "general v9.3 (HOSTFAKESPLIT)": "Gen · 9.3 Host",
    "minimal-cpu": "Лёгкая CPU",
    "preset_russia": "RU · Preset",
    "preset_russia_etelecom": "RU · Etelecom",
    "RussiaFix": "RU · Fix",
    "RussiaFix (ALT)": "RU · Fix ALT",
    "RussiaFix (Rostelekom)": "RU · RTK",
    "smart-auto": "Smart · Auto",
    "smart-multiprofile": "Smart · Multi",
    "stealth": "Тихая",
    "UbisoftFix": "Ubisoft · Fix",
    "UbisoftFix (ALT)": "Ubisoft · ALT",
    "UltimateFix": "UF · Base",
    "UltimateFix (для Билайн, Ростелеком, Инфолинк)": "UF · RTK",
    "UltimateFix (для МГТС)": "UF · MGTS",
    "UltimateFix v9.2": "UF · 9.2",
    "UltimateFix v9.3": "UF · 9.3",
    "UltimateFix_ALT": "UF · ALT",
    "UltimateFix_ALT (для Билайна и Ростелеком)": "UF · ALT RTK",
    "UltimateFix_ALT (для МГТС)": "UF · ALT MGTS",
    "UltimateFix_ALT_EXTENDED": "UF · EXT",
    "UltimateFix_ALT_PRE": "UF · PRE",
    "UltimateFix_ALT_v2": "UF · ALT2",
    "UltimateFix_ALT_v2 (для Билайна и Ростелеком)": "UF · ALT2 RTK",
    "UltimateFix_ALT_v2 (для МГТС)": "UF · ALT2 MGTS",
    "UltimateFix_ALT_v3": "UF · ALT3",
    "UltimateFix_ALT_v3 (для Билайна и Ростелеком)": "UF · ALT3 RTK",
    "UltimateFix_ALT_v3 (для МГТС)": "UF · ALT3 MGTS",
    "UltimateFix_ALT_v4": "UF · ALT4",
    "UltimateFix_ALT_v4 (для Билайна и Ростелеком)": "UF · ALT4 RTK",
    "UltimateFix_ALT_v4 (для МГТС)": "UF · ALT4 MGTS",
    "UltimateFix_ALT_v4_PRE (для МГТС)": "UF · ALT4 PRE",
    "UltimateFix_ALT_v5": "UF · ALT5",
    "youtube-optimized": "YT · Opt",
    "youtube-quic-killer": "YT · QUIC",
    "YoutubeFix": "YT · Base",
    "YoutubeFix (для МГТС)": "YT · MGTS",
    "YoutubeFix (для ТТК)": "YT · TTK",
    "YoutubeFix v9.2": "YT · 9.2",
    "YoutubeFix v9.3": "YT · 9.3",
    "YoutubeFix_ALT": "YT · ALT",
    "YoutubeFix_ALT (для МГТС)": "YT · ALT MGTS",
    "YoutubeFix_ALT (для ТТК)": "YT · ALT TTK",
}


def build_extra_display_label(strategy_id: str, index: int) -> str:
    if strategy_id in EXTRA_DISPLAY_LABELS:
        return EXTRA_DISPLAY_LABELS[strategy_id]

    label = strategy_id
    label = re.sub(r"^UltimateFix", "UF", label)
    label = re.sub(r"^DiscordFix", "DC", label)
    label = re.sub(r"^YoutubeFix", "YT", label)
    label = re.sub(r"^general ", "Gen · ", label)
    label = label.replace(" (для ", " · ")
    label = label.replace(")", "")
    label = re.sub(r"\s+", " ", label).strip()
    if len(label) > 22:
        label = label[:20].rstrip() + "…"
    if not label:
        label = f"Extra {index + 1}"
    return label
