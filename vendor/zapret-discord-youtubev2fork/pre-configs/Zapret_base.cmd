@echo off
chcp 65001 >nul
:: 65001 - UTF-8

cd /d "%~dp0..\"
set BIN=%~dp0..\bin\

set LIST_TITLE=ZAPRET: Ubisoft Fix
set LIST_PATH=%~dp0..\lists\list-ultimate.txt
set GMODE_FLAG_FILE=%BIN%gmode.flag
set DISCORD_IPSET_PATH=%~dp0..\lists\ipset-discord.txt
set CLOUDFLARE_IPSET_PATH=%~dp0..\lists\ipset-cloudflare.txt
set UBISOFT_IPSET_PATH=%~dp0..\lists\ipset-ubisoft.txt
set RUSSIA_IPSET_PATH=%~dp0..\lists\ipset-russia.txt

if exist "%GMODE_FLAG_FILE%" (
    set "GModeStatus=enabled"
    set "GModeRange=1024-65535"
) else (
    set "GModeStatus=disabled"
    set "GModeRange=0"
)

start "%LIST_TITLE%" /min "%BIN%winws.exe" ^
--wf-tcp=80,443 ^
--wf-raw-part=@"%~dp0..\lists\windivert.filter\windivert_part.discord_media.txt" ^
--wf-raw-part=@"%~dp0..\lists\windivert.filter\windivert_part.stun.txt" ^
--wf-raw-part=@"%~dp0..\lists\windivert.filter\windivert_part.wireguard.txt" ^
--wf-raw-part=@"%~dp0..\lists\windivert.filter\windivert_part.quic_initial_ietf.txt" ^
--filter-tcp=80 --dpi-desync=fake,fakedsplit --dpi-desync-autottl=2 --dpi-desync-fooling=md5sig --new ^
--filter-tcp=443 --hostlist="%LIST_PATH%" --dpi-desync=fake,multidisorder --dpi-desync-split-pos=1,midsld --dpi-desync-repeats=11 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls-mod=rnd,dupsid,sni=www.google.com --new ^
--filter-tcp=443 --dpi-desync=fake,multidisorder --dpi-desync-split-pos=midsld --dpi-desync-repeats=6 --dpi-desync-fooling=badseq,md5sig --new ^
--filter-l7=quic --hostlist="%LIST_PATH%" --dpi-desync=fake --dpi-desync-repeats=11 --dpi-desync-fake-quic="%BIN%quic_initial_www_google_com.bin" --new ^
--filter-l7=quic --dpi-desync=fake --dpi-desync-repeats=11
