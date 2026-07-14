@echo off
chcp 65001 >nul
:: 65001 - UTF-8
:: DiscordFix 9.2.0 - Updated with zapret-discord-youtube 1.9.1 techniques

cd /d "%~dp0..\"
set BIN=%~dp0..\bin\
set LISTS=%~dp0..\lists\

set LIST_TITLE=ZAPRET: General v9.2
set LIST_PATH=%LISTS%list-ultimate.txt
set GMODE_FLAG_FILE=%BIN%gmode.flag

:: enable tcp timestamps for better compatibility
netsh interface tcp set global timestamps=enabled >nul 2>&1

if exist "%GMODE_FLAG_FILE%" (
    set "GModeStatus=enabled"
    set "GModeRange=1024-65535"
) else (
    set "GModeStatus=disabled"
    set "GModeRange=12"
)

start "%LIST_TITLE%" /min "%BIN%winws.exe" ^
--wf-tcp=80,443,2053,2083,2087,2096,8443,%GModeRange% ^
--wf-udp=443,19294-19344,50000-50100,%GModeRange% ^
--filter-udp=443 --hostlist="%LIST_PATH%" --hostlist-exclude="%LISTS%list-exclude.txt" --ipset-exclude="%LISTS%ipset-exclude.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-udp=19294-19344,50000-50100 --filter-l7=discord,stun --dpi-desync=fake --dpi-desync-fake-discord="%BIN%quic_initial_www_google_com.bin" --dpi-desync-fake-stun="%BIN%quic_initial_www_google_com.bin" --dpi-desync-repeats=6 --new ^
--filter-tcp=2053,2083,2087,2096,8443 --hostlist-domains=discord.media --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-tcp=443 --hostlist="%LISTS%list-google.txt" --ip-id=zero --dpi-desync=multisplit --dpi-desync-split-seqovl=681 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_www_google_com.bin" --new ^
--filter-tcp=80,443 --hostlist="%LIST_PATH%" --hostlist-exclude="%LISTS%list-exclude.txt" --ipset-exclude="%LISTS%ipset-exclude.txt" --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-udp=443 --ipset="%LISTS%ipset-discord.txt" --hostlist-exclude="%LISTS%list-exclude.txt" --ipset-exclude="%LISTS%ipset-exclude.txt" --dpi-desync=fake --dpi-desync-repeats=6 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-tcp=80,443,%GModeRange% --ipset="%LISTS%ipset-discord.txt" --hostlist-exclude="%LISTS%list-exclude.txt" --ipset-exclude="%LISTS%ipset-exclude.txt" --dpi-desync=multisplit --dpi-desync-split-seqovl=568 --dpi-desync-split-pos=1 --dpi-desync-split-seqovl-pattern="%BIN%tls_clienthello_4pda_to.bin" --new ^
--filter-udp=%GModeRange% --ipset="%LISTS%ipset-discord.txt" --ipset-exclude="%LISTS%ipset-exclude.txt" --dpi-desync=fake --dpi-desync-autottl=2 --dpi-desync-repeats=12 --dpi-desync-any-protocol=1 --dpi-desync-fake-unknown-udp="%BIN%quic_initial_www_google_com.bin" --dpi-desync-cutoff=n2
