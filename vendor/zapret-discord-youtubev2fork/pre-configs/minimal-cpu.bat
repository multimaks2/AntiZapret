@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Minimal CPU
:: Optimized for low CPU usage on weak PCs
:: Uses L7 filtering + minimal repeats + specific hostlists only

start "" /b winws.exe ^
--wf-tcp=443 --wf-udp=443,19294-19344 ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-fooling=md5sig --dpi-desync-repeats=4 --hostlist=..\lists\list-discord.txt --hostlist=..\lists\list-youtube.txt --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=4 --hostlist=..\lists\list-google.txt --new ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=4
