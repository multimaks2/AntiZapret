@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Smart Multi-Profile
:: Uses different strategies for different sites:
:: - Profile 1: gentle approach for most sites
:: - Profile 2: aggressive for Google/YouTube  
:: - Profile 3: special handling for Discord
:: - Profile 4: Cloudflare alternative ports

start "" /b winws.exe ^
--wf-tcp=80,443,2053,2083,2087,2096,8443 --wf-udp=443,19294-19344,50000-65535 ^
--filter-tcp=80 --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --dpi-desync-autottl=2 --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=681 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@tls_clienthello_www_google_com.bin --dpi-desync-repeats=8 --ip-id=zero --hostlist=..\lists\list-google.txt --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-discord.txt --new ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-ultimate.txt --new ^
--filter-tcp=2053,2083,2087,2096,8443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@tls_clienthello_4pda_to.bin --dpi-desync-repeats=6 --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-fake-quic=@quic_initial_rr2---sn-gvnuxaxjvh-o8ge_googlevideo_com.bin --dpi-desync-repeats=8 --hostlist=..\lists\list-google.txt --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=19294-19344 --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --new ^
--filter-udp=50000-65535 --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=6
