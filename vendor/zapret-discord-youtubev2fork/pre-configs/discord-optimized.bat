@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Discord Optimized
:: Best settings for Discord specifically
:: Handles: web, desktop app, voice, video, screen share

start "" /b winws.exe ^
--wf-tcp=443,2053,2083,2087,2096,8443 --wf-udp=443,19294-19344,50000-65535 ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-discord.txt --new ^
--filter-tcp=2053,2083,2087,2096,8443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@tls_clienthello_4pda_to.bin --dpi-desync-repeats=6 --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --hostlist=..\lists\list-discord.txt --new ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=10 --dup=2 --dup-ttl=3 --dup-fooling=badseq --new ^
--filter-udp=50000-65535 --filter-l7=stun,discord --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --dup=2 --dup-ttl=3
