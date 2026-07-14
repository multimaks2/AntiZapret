@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix v9.3 (HOSTFAKESPLIT) - New desync mode for resistant DPIs
:: Uses: hostfakesplit, synack-split, ip-id=zero

start "" /b winws.exe ^
--wf-tcp=80,443,2053,2083,2087,2096,8443 --wf-udp=443,19294-19344,50000-50100 ^
--filter-tcp=80 --dpi-desync=fake,hostfakesplit --dpi-desync-hostfakesplit-midhost=midsld --dpi-desync-fooling=md5sig,ts --dpi-desync-repeats=6 --synack-split=syn --new ^
--filter-tcp=2053,2083,2087,2096,8443 --dpi-desync=fake,hostfakesplit --dpi-desync-hostfakesplit-midhost=midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig,ts --dpi-desync-fake-tls=@tls_clienthello_4pda_to.bin --dpi-desync-repeats=6 --synack-split=syn --new ^
--filter-tcp=443 --dpi-desync=fake,hostfakesplit --dpi-desync-hostfakesplit-midhost=midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig,ts --dpi-desync-repeats=6 --synack-split=syn --ip-id=zero --hostlist=..\lists\list-ultimate.txt --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=19294-19344 --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --new ^
--filter-udp=50000-50100 --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-repeats=8
