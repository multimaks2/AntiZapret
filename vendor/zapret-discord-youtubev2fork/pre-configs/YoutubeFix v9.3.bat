@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: YoutubeFix v9.3 - Enhanced with Kyber QUIC fakes for googlevideo
:: Uses: ip-id=zero for Google, new googlevideo kyber fakes

start "" /b winws.exe ^
--wf-tcp=443 --wf-udp=443 ^
--filter-tcp=443 --dpi-desync=fake,multisplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=681 --dpi-desync-fooling=md5sig --dpi-desync-fake-tls=@tls_clienthello_www_google_com.bin --dpi-desync-repeats=6 --ip-id=zero --hostlist=..\lists\list-google.txt --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-fake-quic=@quic_initial_rr2---sn-gvnuxaxjvh-o8ge_googlevideo_com.bin --dpi-desync-repeats=6 --hostlist=..\lists\list-google.txt
