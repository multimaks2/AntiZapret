@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Protocol-Aware (L7 Filtering)
:: Only processes actual protocols, not random traffic
:: More efficient, less CPU usage

start "" /b winws.exe ^
--wf-tcp=80,443 --wf-udp=443,19294-19344,50000-65535 ^
--filter-tcp=80 --filter-l7=http --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --new ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-ultimate.txt --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --new ^
--filter-udp=50000-65535 --filter-l7=stun,discord --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=6
