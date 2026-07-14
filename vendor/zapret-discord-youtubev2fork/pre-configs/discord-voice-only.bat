@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Discord Voice Only
:: Minimal config for voice/video calls only
:: Does not affect web browsing, very low CPU

start "" /b winws.exe ^
--wf-udp=19294-19344,50000-65535 ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --dup=2 --dup-ttl=3 --new ^
--filter-udp=50000-65535 --filter-l7=stun,discord --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=8
