@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Gaming Platforms
:: Steam, Epic Games, Ubisoft, EA, Rockstar
:: Optimized for game downloads and online play

start "" /b winws.exe ^
--wf-tcp=80,443,27015-27050,3478-3480 --wf-udp=443,27015-27050,3478-3480 ^
--filter-tcp=80 --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-steam.txt --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-nvidia.txt --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --ipset=..\lists\ipset-ubisoft.txt --new ^
--filter-tcp=27015-27050 --dpi-desync=fake --dpi-desync-repeats=4 --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=27015-27050 --dpi-desync=fake --dpi-desync-repeats=4 --new ^
--filter-udp=3478-3480 --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-repeats=6
