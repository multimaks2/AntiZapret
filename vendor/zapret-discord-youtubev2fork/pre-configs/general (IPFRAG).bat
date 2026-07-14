@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - IP Fragmentation
:: Fragments IP packets to bypass DPI
:: May not work if ISP reassembles fragments

start "" /b winws.exe ^
--wf-tcp=80,443 --wf-udp=443,50000-65535 ^
--filter-tcp=80 --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --new ^
--filter-tcp=443 --dpi-desync=ipfrag2,fake --dpi-desync-ipfrag-pos-tcp=40 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-ultimate.txt --new ^
--filter-udp=443 --dpi-desync=ipfrag2,fake --dpi-desync-ipfrag-pos-udp=8 --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=50000-65535 --dpi-desync=fake --dpi-desync-fake-unknown-udp=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=6
