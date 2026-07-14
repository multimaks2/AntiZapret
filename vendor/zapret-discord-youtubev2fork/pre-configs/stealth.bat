@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Stealth Mode
:: Maximum evasion, traffic looks as normal as possible
:: Uses real-looking fakes, minimal repeats, autottl

start "" /b winws.exe ^
--wf-tcp=443 --wf-udp=443,19294-19344 ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-autottl --dpi-desync-repeats=3 --dpi-desync-fake-tls=! --dpi-desync-fake-tls-mod=rnd,sni=www.microsoft.com --ip-id=rnd --hostlist=..\lists\list-ultimate.txt --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-autottl --dpi-desync-repeats=3 --new ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-autottl --dpi-desync-repeats=3
