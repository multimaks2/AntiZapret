@echo off
chcp 65001 >nul
:: 65001 - UTF-8
:: DiscordFix 9.2.0 - YouTube Only

cd /d "%~dp0..\"
set BIN=%~dp0..\bin\
set LISTS=%~dp0..\lists\

set LIST_TITLE=ZAPRET: YouTube Only v9.2

netsh interface tcp set global timestamps=enabled >nul 2>&1

start "%LIST_TITLE%" /min "%BIN%winws.exe" ^
--wf-tcp=80,443 ^
--wf-udp=443 ^
--filter-udp=443 --hostlist="%LISTS%list-youtube.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-udp=443 --hostlist="%LISTS%list-google.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-tcp=443 --hostlist="%LISTS%list-google.txt" --ip-id=zero --dpi-desync=multisplit --dpi-desync-split-seqovl=681 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_www_google_com.bin" --new ^
--filter-tcp=80,443 --hostlist="%LISTS%list-youtube.txt" --dpi-desync=multisplit --dpi-desync-split-seqovl=681 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_www_google_com.bin"
