@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Smart Auto-Learning
:: Automatically detects blocked domains and adds them to autohostlist.txt
:: The more you use it, the smarter it gets!

start "" /b winws.exe ^
--wf-tcp=80,443 --wf-udp=443,50000-65535 ^
--filter-tcp=80 --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --dpi-desync-autottl=2 --hostlist-auto=autohostlist.txt --hostlist-auto-fail-threshold=2 --hostlist-auto-fail-time=60 --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --dpi-desync-autottl=2 --hostlist-auto=autohostlist.txt --hostlist-auto-fail-threshold=2 --hostlist-auto-fail-time=60 --hostlist-auto-retrans-threshold=2 --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=50000-65535 --dpi-desync=fake --dpi-desync-fake-unknown-udp=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=6
