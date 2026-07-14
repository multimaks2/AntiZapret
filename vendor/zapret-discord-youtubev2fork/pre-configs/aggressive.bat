@echo off
chcp 65001 >nul
pushd "%~dp0..\bin"

:: DiscordFix - Aggressive Mode
:: Maximum bypass, all techniques enabled
:: Use when nothing else works. Higher CPU usage.

start "" /b winws.exe ^
--wf-tcp=80,443,2053,2083,2087,2096,8443 --wf-udp=443,19294-19344,50000-65535 ^
--filter-tcp=80 --dpi-desync=fake,fakedsplit --dpi-desync-split-pos=2 --dpi-desync-fooling=md5sig,badseq,ts --dpi-desync-repeats=8 --dpi-desync-autottl=2 --dup=2 --dup-ttl=2 --new ^
--filter-tcp=443 --dpi-desync=fake,fakedsplit --dpi-desync-split-pos=1,host+1,midsld,endhost --dpi-desync-split-seqovl=1 --dpi-desync-fooling=md5sig,badseq,ts --dpi-desync-repeats=10 --dpi-desync-autottl=2 --dup=3 --dup-ttl=2 --dup-fooling=badseq --synack-split=syn --hostlist=..\lists\list-ultimate.txt --new ^
--filter-tcp=2053,2083,2087,2096,8443 --dpi-desync=fake,fakedsplit --dpi-desync-split-pos=1,midsld --dpi-desync-split-seqovl=1 --dpi-desync-fooling=md5sig,badseq,ts --dpi-desync-fake-tls=@tls_clienthello_4pda_to.bin --dpi-desync-repeats=10 --dup=2 --dup-ttl=2 --new ^
--filter-udp=443 --dpi-desync=fake --dpi-desync-fake-quic=@quic_initial_www_google_com.bin --dpi-desync-fake-quic=@quic_initial_rr2---sn-gvnuxaxjvh-o8ge_googlevideo_com.bin --dpi-desync-repeats=12 --new ^
--filter-udp=19294-19344 --dpi-desync=fake --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-fake-discord=@discord-ip-discovery-without-port.bin --dpi-desync-repeats=12 --new ^
--filter-udp=50000-65535 --dpi-desync=fake --dpi-desync-fake-stun=@stun.bin --dpi-desync-fake-discord=@discord-ip-discovery-with-port.bin --dpi-desync-repeats=10
