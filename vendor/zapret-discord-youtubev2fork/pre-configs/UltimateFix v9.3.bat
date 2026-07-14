@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Ultimate v9.3
:: Best combined settings for all services:
:: Discord, YouTube, Google, Telegram, Instagram, Twitter, etc.
:: Uses L7 filtering, proper fakes, and optimized repeats

start "" /b winws.exe ^
--wf-tcp=80,443,2053,2083,2087,2096,8443 --wf-udp=443,19294-19344,50000-65535 ^
--filter-tcp=80 --filter-l7=http --dpi-desync=fake,multisplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --dpi-desync-autottl=2 --new ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,host+1,midsld --dpi-desync-split-seqovl=681 --dpi-desync-fooling=md5sig,ts --dpi-desync-fake-tls=@tls_clienthello_www_google_com.bin --dpi-desync-repeats=8 --ip-id=zero --hostlist=..\lists\list-google.txt --new ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-discord.txt --new ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-repeats=6 --hostlist=..\lists\list-ultimate.txt --new ^
--filter-tcp=2053,2083,2087,2096,8443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=568 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@tls_clienthello_4pda_to.bin --dpi-desync-repeats=6 --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-fake-quic=@quic_initial_rr2---sn-gvnuxaxjvh-o8ge_googlevideo_com.bin --dpi-desync-repeats=8 --hostlist=..\lists\list-google.txt --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-repeats=6 --new ^
--filter-udp=19294-19344 --filter-l7=discord --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=8 --dup=2 --dup-ttl=3 --new ^
--filter-udp=50000-65535 --filter-l7=stun,discord --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=6
