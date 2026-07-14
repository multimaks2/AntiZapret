@echo off
chcp 65001 >nul
:: 65001 - UTF-8
:: DiscordFix 9.2.0 - Discord Only

cd /d "%~dp0..\"
set BIN=%~dp0..\bin\
set LISTS=%~dp0..\lists\

set LIST_TITLE=ZAPRET: Discord Only v9.2
set GMODE_FLAG_FILE=%BIN%gmode.flag

netsh interface tcp set global timestamps=enabled >nul 2>&1

if exist "%GMODE_FLAG_FILE%" (
    set "GModeStatus=enabled"
    set "GModeRange=1024-65535"
) else (
    set "GModeStatus=disabled"
    set "GModeRange=12"
)

start "%LIST_TITLE%" /min "%BIN%winws.exe" ^
--wf-tcp=443,2053,2083,2087,2096,8443 ^
--wf-udp=443,19294-19344,50000-50100 ^
--filter-udp=443 --hostlist="%LISTS%list-discord.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-udp=19294-19344,50000-50100 --filter-l7=discord,stun --dpi-desync=fake --dpi-desync-fake-discord="%BIN%quic_initial_www_google_com.bin" --dpi-desync-fake-stun="%BIN%quic_initial_www_google_com.bin" --dpi-desync-repeats=6 --new ^
--filter-tcp=2053,2083,2087,2096,8443 --hostlist-domains=discord.media --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-tcp=443 --hostlist="%LISTS%list-discord.txt" --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-udp=443 --ipset="%LISTS%ipset-discord.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-tcp=443 --ipset="%LISTS%ipset-discord.txt" --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-udp=50000-50100 --ipset="%LISTS%ipset-discord.txt" --dpi-desync=fake --dpi-desync-autottl=2 --dpi-desync-repeats=12 --dpi-desync-any-protocol=1 --dpi-desync-fake-unknown-udp="%BIN%quic_initial_www_google_com.bin" --dpi-desync-cutoff=n2
