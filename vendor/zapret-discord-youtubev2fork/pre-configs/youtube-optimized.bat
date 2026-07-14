@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - YouTube Optimized
:: Best settings for YouTube specifically
:: Handles both TCP (HTTPS) and UDP (QUIC) properly

start "" /b winws.exe ^
--wf-tcp=443 --wf-udp=443 ^
--filter-tcp=443 --filter-l7=tls --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,host+1,midsld,endsld --dpi-desync-split-seqovl=681 --dpi-desync-fooling=md5sig,ts --dpi-desync-fake-tls=@tls_clienthello_www_google_com.bin --dpi-desync-repeats=8 --ip-id=zero --dpi-desync-autottl=2 --hostlist=..\lists\list-google.txt --hostlist=..\lists\list-youtube.txt --new ^
--filter-udp=443 --filter-l7=quic --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-fake-quic=@quic_initial_rr2---sn-gvnuxaxjvh-o8ge_googlevideo_com.bin --dpi-desync-fake-quic=@quic_initial_rr1---sn-xguxaxjvh-n8me_googlevideo_com_kyber_1.bin --dpi-desync-fake-quic=@quic_initial_rr1---sn-xguxaxjvh-n8me_googlevideo_com_kyber_2.bin --dpi-desync-repeats=10 --hostlist=..\lists\list-google.txt --hostlist=..\lists\list-youtube.txt
