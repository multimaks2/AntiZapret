@echo off
set "LOCAL_VERSION=9.3.0"
chcp 65001 >nul

:: External commands for use in bat files
if "%~1"=="enable_tcp_timestamps" (
    netsh interface tcp set global timestamps=enabled >nul 2>&1
    exit /b
)

if "%~1"=="load_game_filter" (
    call :game_switch_status
    exit /b
)

if "%~1"=="status_zapret" (
    call :test_service zapret soft
    call :tcp_enable
    exit /b
)

if "%~1"=="check_updates" (
    if exist "%~dp0utils\check_updates.enabled" (
        if not "%~2"=="soft" (
            start /b service check_updates soft
        ) else (
            call :service_check_updates soft
        )
    )
    exit /b
)

:: Check if admin
if "%1"=="admin" (
    call :check_command chcp
    call :check_command find
    call :check_command findstr
    call :check_command netsh
    echo Started with admin rights
) else (
    call :check_extracted
    call :check_command powershell
    echo Requesting admin rights...
    powershell -Command "Start-Process 'cmd.exe' -ArgumentList '/c \"\"%~f0\" admin\"' -Verb RunAs" 2>nul || (
        echo PowerShell not available, trying VBS elevation...
        cscript //nologo "%~dp0bin\tools\elevator.vbs" "%~f0" admin
    )
    exit
)

:: MENU
setlocal EnableDelayedExpansion
:menu
cls
call :game_switch_status
call :check_updates_switch_status
call :ipset_switch_status

set "menu_choice=null"
echo =========  DiscordFix v%LOCAL_VERSION%  =========
echo:
echo 1. Install as Service
echo 2. Remove Services
echo 3. Check Status
echo 4. Run Diagnostics
echo 5. Check Updates
echo 6. Switch Check Updates (%CheckUpdatesStatus%)
echo 7. Switch Game Filter (%GameFilterStatus%)
echo 8. Switch ipset mode (%IPsetStatus%)
echo 9. Update IP Lists
echo 10. Update hosts file (for Discord voice)
echo 11. Run Tests
echo 12. Clear Discord Cache
echo 0. Exit
echo:
set /p menu_choice=Enter choice (0-12): 

if "%menu_choice%"=="1" goto service_install
if "%menu_choice%"=="2" goto service_remove
if "%menu_choice%"=="3" goto service_status
if "%menu_choice%"=="4" goto service_diagnostics
if "%menu_choice%"=="5" goto service_check_updates
if "%menu_choice%"=="6" goto check_updates_switch
if "%menu_choice%"=="7" goto game_switch
if "%menu_choice%"=="8" goto ipset_switch
if "%menu_choice%"=="9" goto update_ipset
if "%menu_choice%"=="10" goto hosts_update
if "%menu_choice%"=="11" goto run_tests
if "%menu_choice%"=="12" goto clear_discord_cache
if "%menu_choice%"=="0" exit /b
goto menu


:: TCP ENABLE
:tcp_enable
netsh interface tcp show global | findstr /i "timestamps" | findstr /i "enabled" > nul || netsh interface tcp set global timestamps=enabled > nul 2>&1
exit /b


:: STATUS
:service_status
cls
chcp 437 > nul

sc query "zapret" >nul 2>&1
if !errorlevel!==0 (
    for /f "tokens=2*" %%A in ('reg query "HKLM\System\CurrentControlSet\Services\zapret" /v zapret-discordfix 2^>nul') do echo Service strategy installed from "%%B"
)

call :test_service zapret
call :test_service WinDivert

set "BIN_PATH=%~dp0bin\"
if not exist "%BIN_PATH%\*.sys" (
    call :PrintRed "WinDivert64.sys file NOT found."
)
echo:

tasklist /FI "IMAGENAME eq winws.exe" | find /I "winws.exe" > nul
if !errorlevel!==0 (
    call :PrintGreen "Bypass (winws.exe) is RUNNING."
) else (
    call :PrintRed "Bypass (winws.exe) is NOT running."
)

pause
goto menu

:test_service
set "ServiceName=%~1"
set "ServiceStatus="

for /f "tokens=3 delims=: " %%A in ('sc query "%ServiceName%" ^| findstr /i "STATE"') do set "ServiceStatus=%%A"
set "ServiceStatus=%ServiceStatus: =%"

if "%ServiceStatus%"=="RUNNING" (
    if "%~2"=="soft" (
        echo "%ServiceName%" is ALREADY RUNNING as service. Use "service.bat" and choose "Remove Services" first if you want to run standalone bat.
        pause
        exit /b
    ) else (
        echo "%ServiceName%" service is RUNNING.
    )
) else if "%ServiceStatus%"=="STOP_PENDING" (
    call :PrintYellow "!ServiceName! is STOP_PENDING. Run Diagnostics to try to fix conflicts"
) else if not "%~2"=="soft" (
    echo "%ServiceName%" service is NOT running.
)

exit /b


:: REMOVE
:service_remove
cls
chcp 65001 > nul

set SRVCNAME=zapret
sc query "!SRVCNAME!" >nul 2>&1
if !errorlevel!==0 (
    net stop %SRVCNAME%
    sc delete %SRVCNAME%
) else (
    echo Service "%SRVCNAME%" is not installed.
)

tasklist /FI "IMAGENAME eq winws.exe" | find /I "winws.exe" > nul
if !errorlevel!==0 (
    taskkill /IM winws.exe /F > nul
)

sc query "WinDivert" >nul 2>&1
if !errorlevel!==0 (
    net stop "WinDivert"
    sc query "WinDivert" >nul 2>&1
    if !errorlevel!==0 (
        sc delete "WinDivert"
    )
)
net stop "WinDivert14" >nul 2>&1
sc delete "WinDivert14" >nul 2>&1

pause
goto menu


:: INSTALL
:service_install
cls
chcp 65001 > nul

cd /d "%~dp0"
set "BIN_PATH=%~dp0bin\"
set "LISTS_PATH=%~dp0lists\"

:: Search for .bat files in pre-configs
echo Pick one of the configurations:
set "count=0"
for %%f in (pre-configs\*.bat) do (
    set "filename=%%~nxf"
    if /i not "!filename:~0,7!"=="service" (
        set /a count+=1
        echo !count!. %%~nf
        set "file!count!=%%f"
    )
)

:: Choose file
set "choice="
set /p "choice=Input configuration number: "
if "!choice!"=="" goto :eof

set "selectedFile=!file%choice%!"
if not defined selectedFile (
    echo Invalid choice, exiting...
    pause
    goto menu
)

:: Parse args
set "args="
set "capture=0"
set "mergeargs=0"
set QUOTE="

for /f "tokens=*" %%a in ('type "!selectedFile!"') do (
    set "line=%%a"
    call set "line=%%line:^!=EXCL_MARK%%"

    echo !line! | findstr /i "winws.exe" >nul
    if not errorlevel 1 (
        set "capture=1"
    )

    if !capture!==1 (
        if not defined args (
            set "line=!line:*winws.exe"=!"
        )

        set "temp_args="
        for %%i in (!line!) do (
            set "arg=%%i"

            if not "!arg!"=="^" (
                if "!arg:~0,2!" EQU "--" if not !mergeargs!==0 (
                    set "mergeargs=0"
                )

                if "!arg:~0,1!" EQU "!QUOTE!" (
                    set "arg=!arg:~1,-1!"

                    echo !arg! | findstr ":" >nul
                    if !errorlevel!==0 (
                        set "arg=\!QUOTE!!arg!\!QUOTE!"
                    ) else if "!arg:~0,1!"=="@" (
                        set "arg=\!QUOTE!@%~dp0!arg:~1!\!QUOTE!"
                    ) else if "!arg:~0,5!"=="%%BIN%%" (
                        set "arg=\!QUOTE!!BIN_PATH!!arg:~5!\!QUOTE!"
                    ) else if "!arg:~0,7!"=="%%LISTS%%" (
                        set "arg=\!QUOTE!!LISTS_PATH!!arg:~7!\!QUOTE!"
                    ) else (
                        set "arg=\!QUOTE!%~dp0!arg!\!QUOTE!"
                    )
                ) else if "!arg:~0,12!" EQU "%%GModeRange%%" (
                    set "arg=%GModeRange%"
                )

                if !mergeargs!==1 (
                    set "temp_args=!temp_args!,!arg!"
                ) else if !mergeargs!==3 (
                    set "temp_args=!temp_args!=!arg!"
                    set "mergeargs=1"
                ) else (
                    set "temp_args=!temp_args! !arg!"
                )

                if "!arg:~0,2!" EQU "--" (
                    set "mergeargs=2"
                ) else if !mergeargs! GEQ 1 (
                    if !mergeargs!==2 set "mergeargs=1"
                )
            )
        )

        if not "!temp_args!"=="" (
            set "args=!args! !temp_args!"
        )
    )
)

:: Create service
call :tcp_enable

set ARGS=%args%
call set "ARGS=%%ARGS:EXCL_MARK=^!%%"
echo Final args: !ARGS!
set SRVCNAME=zapret

net stop %SRVCNAME% >nul 2>&1
sc delete %SRVCNAME% >nul 2>&1
sc create %SRVCNAME% binPath= "\"%BIN_PATH%winws.exe\" !ARGS!" DisplayName= "zapret" start= auto
sc description %SRVCNAME% "Zapret DPI bypass (DiscordFix)"
sc start %SRVCNAME%
for %%F in ("!file%choice%!") do (
    set "filename=%%~nF"
)
reg add "HKLM\System\CurrentControlSet\Services\zapret" /v zapret-discordfix /t REG_SZ /d "!filename!" /f

pause
goto menu


:: DIAGNOSTICS
:service_diagnostics
cls
chcp 65001 > nul

echo Running diagnostics...
echo:

:: Check WinDivert driver
sc query "WinDivert" >nul 2>&1
if !errorlevel!==0 (
    call :PrintYellow "WinDivert service exists, attempting cleanup..."
    net stop "WinDivert" >nul 2>&1
    sc delete "WinDivert" >nul 2>&1
    if !errorlevel!==0 (
        call :PrintGreen "WinDivert successfully removed"
    ) else (
        call :PrintRed "Failed to remove WinDivert"
    )
)

:: Check for conflicting services
set "conflicting_services=GoodbyeDPI discordfix_zapret winws1 winws2"
set "found_conflicts="

for %%s in (!conflicting_services!) do (
    sc query "%%s" >nul 2>&1
    if !errorlevel!==0 (
        if "!found_conflicts!"=="" (
            set "found_conflicts=%%s"
        ) else (
            set "found_conflicts=!found_conflicts! %%s"
        )
    )
)

if not "!found_conflicts!"=="" (
    call :PrintRed "Conflicting services found: !found_conflicts!"
    
    set "CHOICE="
    set /p "CHOICE=Remove conflicting services? (Y/N, default: N) "
    if "!CHOICE!"=="" set "CHOICE=N"
    if /i "!CHOICE!"=="Y" (
        for %%s in (!found_conflicts!) do (
            call :PrintYellow "Removing service: %%s"
            net stop "%%s" >nul 2>&1
            sc delete "%%s" >nul 2>&1
        )
        net stop "WinDivert" >nul 2>&1
        sc delete "WinDivert" >nul 2>&1
    )
) else (
    call :PrintGreen "No conflicting services found"
)

echo:
pause
goto menu


:: GAME FILTER
:game_switch_status
chcp 437 > nul

set "gameFlagFile=%~dp0utils\game_filter.enabled"

if exist "%gameFlagFile%" (
    set "GameFilterStatus=enabled"
    set "GameFilter=1024-65535"
) else (
    set "GameFilterStatus=disabled"
    set "GameFilter=12"
)
exit /b


:game_switch
chcp 437 > nul
cls

set "gameFlagFile=%~dp0utils\game_filter.enabled"

if not exist "%gameFlagFile%" (
    echo Enabling game filter...
    echo ENABLED > "%gameFlagFile%"
    call :PrintYellow "Restart zapret to apply changes"
) else (
    echo Disabling game filter...
    del /f /q "%gameFlagFile%"
    call :PrintYellow "Restart zapret to apply changes"
)

pause
goto menu


:: CHECK UPDATES SWITCH
:check_updates_switch_status
chcp 437 > nul

set "checkUpdatesFlag=%~dp0utils\check_updates.enabled"

if exist "%checkUpdatesFlag%" (
    set "CheckUpdatesStatus=enabled"
) else (
    set "CheckUpdatesStatus=disabled"
)
exit /b


:check_updates_switch
chcp 437 > nul
cls

set "checkUpdatesFlag=%~dp0utils\check_updates.enabled"

if not exist "%checkUpdatesFlag%" (
    echo Enabling check updates...
    echo ENABLED > "%checkUpdatesFlag%"
) else (
    echo Disabling check updates...
    del /f /q "%checkUpdatesFlag%"
)

pause
goto menu


:: IPSET SWITCH
:ipset_switch_status
chcp 437 > nul

set "listFile=%~dp0lists\ipset-all.txt"
for /f %%i in ('type "%listFile%" 2^>nul ^| find /c /v ""') do set "lineCount=%%i"

if !lineCount!==0 (
    set "IPsetStatus=any"
) else (
    findstr /R "^203\.0\.113\.113/32$" "%listFile%" >nul 2>&1
    if !errorlevel!==0 (
        set "IPsetStatus=none"
    ) else (
        set "IPsetStatus=loaded"
    )
)
exit /b


:ipset_switch
chcp 437 > nul
cls

set "listFile=%~dp0lists\ipset-all.txt"
set "backupFile=%listFile%.backup"

echo Current ipset mode: %IPsetStatus%
echo:
echo Modes:
echo   any    - empty ipset (all traffic goes through zapret)
echo   none   - dummy IP (no traffic goes through zapret by IP)
echo   loaded - use ipset-all.txt.backup
echo:
echo 1. Switch to 'any' mode
echo 2. Switch to 'none' mode
echo 3. Restore from backup ('loaded' mode)
echo 0. Back to menu
echo:
set /p "ipset_mode_choice=Choose mode (0-3): "

if "%ipset_mode_choice%"=="0" goto menu
if "%ipset_mode_choice%"=="1" (
    if exist "%listFile%" copy /y "%listFile%" "%backupFile%" > nul
    echo. > "%listFile%"
    call :PrintGreen "Switched to 'any' mode (empty ipset)"
    call :PrintYellow "Restart zapret to apply changes"
)
if "%ipset_mode_choice%"=="2" (
    if exist "%listFile%" copy /y "%listFile%" "%backupFile%" > nul
    echo 203.0.113.113/32> "%listFile%"
    call :PrintGreen "Switched to 'none' mode (dummy IP)"
    call :PrintYellow "Restart zapret to apply changes"
)
if "%ipset_mode_choice%"=="3" (
    if exist "%backupFile%" (
        copy /y "%backupFile%" "%listFile%" > nul
        call :PrintGreen "Restored from backup"
        call :PrintYellow "Restart zapret to apply changes"
    ) else (
        call :PrintRed "Backup file not found"
    )
)

pause
goto menu


:: HOSTS UPDATE
:hosts_update
chcp 437 > nul
cls

set "hostsFile=%SystemRoot%\System32\drivers\etc\hosts"
set "hostsUrl=https://raw.githubusercontent.com/nickspaargaren/no-google/master/categories/discord_hosts.txt"
set "tempFile=%TEMP%\discordfix_hosts.txt"
set "needsUpdate=0"

echo Checking hosts file for Discord entries...
echo:

:: try to download discord hosts
if exist "%SystemRoot%\System32\curl.exe" (
    curl -L -s -o "%tempFile%" "%hostsUrl%" 2>nul
) else (
    powershell -Command ^
        "$url = '%hostsUrl%';" ^
        "$out = '%tempFile%';" ^
        "$res = Invoke-WebRequest -Uri $url -TimeoutSec 10 -UseBasicParsing;" ^
        "if ($res.StatusCode -eq 200) { $res.Content | Out-File -FilePath $out -Encoding UTF8 } else { exit 1 }"
)

if not exist "%tempFile%" (
    call :PrintYellow "Could not download Discord hosts, using built-in list..."
    
    :: create temp file with built-in Discord IPs
    (
        echo # Discord voice servers
        echo 66.22.196.0 voice.discord.com
        echo 66.22.197.0 voice.discord.com
        echo 66.22.198.0 voice.discord.com
        echo 66.22.199.0 voice.discord.com
        echo 66.22.200.0 voice.discord.com
        echo 66.22.201.0 voice.discord.com
        echo 66.22.202.0 voice.discord.com
        echo 66.22.203.0 voice.discord.com
        echo 66.22.204.0 voice.discord.com
        echo 66.22.205.0 voice.discord.com
        echo 66.22.206.0 voice.discord.com
        echo 66.22.207.0 voice.discord.com
        echo 66.22.208.0 voice.discord.com
        echo 66.22.209.0 voice.discord.com
        echo 66.22.210.0 voice.discord.com
        echo 66.22.211.0 voice.discord.com
        echo 66.22.212.0 voice.discord.com
        echo 66.22.213.0 voice.discord.com
        echo 66.22.214.0 voice.discord.com
        echo 66.22.215.0 voice.discord.com
    ) > "%tempFile%"
)

:: check if hosts file needs update
findstr /C:"discord" "%hostsFile%" >nul 2>&1
if !errorlevel! neq 0 (
    echo Discord entries not found in hosts file
    set "needsUpdate=1"
)

if "%needsUpdate%"=="1" (
    echo:
    call :PrintYellow "Hosts file may need to be updated"
    call :PrintYellow "Please manually copy Discord entries to your hosts file"
    
    start notepad "%tempFile%"
    explorer /select,"%hostsFile%"
) else (
    call :PrintGreen "Hosts file already contains Discord entries"
    if exist "%tempFile%" del /f /q "%tempFile%"
)

echo:
pause
goto menu


:: RUN TESTS
:run_tests
chcp 65001 >nul
cls

:: check PowerShell version
powershell -NoProfile -Command "if ($PSVersionTable -and $PSVersionTable.PSVersion -and $PSVersionTable.PSVersion.Major -ge 3) { exit 0 } else { exit 1 }" >nul 2>&1
if %errorLevel% neq 0 (
    echo PowerShell 3.0 or newer is required.
    echo Please upgrade PowerShell and rerun this script.
    echo:
    pause
    goto menu
)

if not exist "%~dp0utils\test zapret.ps1" (
    call :PrintRed "Test script not found: utils\test zapret.ps1"
    pause
    goto menu
)

echo Starting configuration tests in PowerShell window...
echo:
start "" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0utils\test zapret.ps1"
pause
goto menu


:: CHECK UPDATES
:service_check_updates
cls
chcp 65001 > nul

echo Checking for updates...
echo:

set "GITHUB_REPO=https://api.github.com/repos/Flowseal/zapret-discord-youtube/releases/latest"

:: get latest version
for /f "tokens=*" %%a in ('powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { (Invoke-RestMethod -Uri '%GITHUB_REPO%' -TimeoutSec 10).tag_name } catch { 'error' }"') do set "REMOTE_VERSION=%%a"

if "%REMOTE_VERSION%"=="error" (
    call :PrintRed "Failed to check for updates"
    if not "%~1"=="soft" pause
    goto menu
)

echo Local version:  v%LOCAL_VERSION%
echo Remote version: %REMOTE_VERSION%
echo:

:: simple version compare (not perfect but works for most cases)
if "v%LOCAL_VERSION%"=="%REMOTE_VERSION%" (
    call :PrintGreen "You have the latest version!"
) else (
    call :PrintYellow "New version available: %REMOTE_VERSION%"
    call :PrintYellow "Visit: https://github.com/Flowseal/zapret-discord-youtube/releases"
)

if not "%~1"=="soft" pause
goto menu


:: CHECK COMMAND
:check_command
where %1 >nul 2>&1
if %errorlevel% neq 0 (
    echo WARNING: %1 not found in PATH
)
exit /b


:: DISCORD CACHE
:clear_discord_cache
cls
chcp 65001 > nul

tasklist /FI "IMAGENAME eq Discord.exe" | findstr /I "Discord.exe" > nul
if !errorlevel!==0 (
    echo Discord is running, closing...
    taskkill /IM Discord.exe /F > nul
    if !errorlevel! == 0 (
        call :PrintGreen "Discord was successfully closed"
    ) else (
        call :PrintRed "Unable to close Discord"
    )
)

set "discordCacheDir=%appdata%\discord"

for %%d in ("Cache" "Code Cache" "GPUCache") do (
    set "dirPath=!discordCacheDir!\%%~d"
    if exist "!dirPath!" (
        rd /s /q "!dirPath!"
        if !errorlevel!==0 (
            call :PrintGreen "Deleted !dirPath!"
        ) else (
            call :PrintRed "Failed to delete !dirPath!"
        )
    ) else (
        echo %%~d not found, skipping...
    )
)

echo:
pause
goto menu


:: UPDATE IPSET
:update_ipset
cls
chcp 65001 > nul

set "LISTS_PATH=%~dp0lists\"

echo ========================================
echo    Update IP Lists (antifilter.download)
echo ========================================
echo:
echo Sources:
echo   1. ipsum.lst - aggregated IPs (recommended)
echo   2. ip.lst - all blocked IPs
echo   3. allyouneed.lst - IPs + subnets
echo   4. Update all lists
echo   0. Back to menu
echo:
set /p "ipset_choice=Choose source (0-4): "

if "!ipset_choice!"=="0" goto menu
if "!ipset_choice!"=="1" (
    set "IPSET_URL=https://antifilter.download/list/ipsum.lst"
    set "IPSET_FILE=ipset-russia.txt"
    goto do_download
)
if "!ipset_choice!"=="2" (
    set "IPSET_URL=https://antifilter.download/list/ip.lst"
    set "IPSET_FILE=ipset-russia.txt"
    goto do_download
)
if "!ipset_choice!"=="3" (
    set "IPSET_URL=https://antifilter.download/list/allyouneed.lst"
    set "IPSET_FILE=ipset-russia.txt"
    goto do_download
)
if "!ipset_choice!"=="4" goto do_download_all

goto update_ipset

:do_download
echo:
echo Downloading from !IPSET_URL!...

powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri '!IPSET_URL!' -OutFile '!LISTS_PATH!!IPSET_FILE!.new' -UseBasicParsing -TimeoutSec 30 } catch { exit 1 }"

if !errorlevel!==0 (
    for %%A in ("!LISTS_PATH!!IPSET_FILE!.new") do set "filesize=%%~zA"
    if !filesize! GTR 1000 (
        move /y "!LISTS_PATH!!IPSET_FILE!.new" "!LISTS_PATH!!IPSET_FILE!" > nul
        call :PrintGreen "Updated !IPSET_FILE! (!filesize! bytes)"
        
        for /f %%L in ('find /c /v "" ^< "!LISTS_PATH!!IPSET_FILE!"') do set "linecount=%%L"
        echo Total IPs: !linecount!
    ) else (
        del /f /q "!LISTS_PATH!!IPSET_FILE!.new" 2>nul
        call :PrintRed "Downloaded file too small, keeping old list"
    )
) else (
    del /f /q "!LISTS_PATH!!IPSET_FILE!.new" 2>nul
    call :PrintRed "Download failed, keeping old list"
)

echo:
pause
goto menu

:do_download_all
echo:
echo Downloading all IP lists...
echo:

:: ipsum (aggregated)
echo [1/3] Downloading ipsum.lst...
powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri 'https://antifilter.download/list/ipsum.lst' -OutFile '!LISTS_PATH!ipset-russia.txt.new' -UseBasicParsing -TimeoutSec 30 } catch { exit 1 }"
if !errorlevel!==0 (
    for %%A in ("!LISTS_PATH!ipset-russia.txt.new") do set "filesize=%%~zA"
    if !filesize! GTR 1000 (
        move /y "!LISTS_PATH!ipset-russia.txt.new" "!LISTS_PATH!ipset-russia.txt" > nul
        call :PrintGreen "Updated ipset-russia.txt"
    ) else (
        del /f /q "!LISTS_PATH!ipset-russia.txt.new" 2>nul
        call :PrintYellow "ipsum.lst too small, skipped"
    )
) else (
    del /f /q "!LISTS_PATH!ipset-russia.txt.new" 2>nul
    call :PrintRed "Failed to download ipsum.lst"
)

:: discord IPs
echo [2/3] Downloading Discord IPs...
powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/nickspaargaren/no-google/master/categories/discord.txt' -OutFile '!LISTS_PATH!ipset-discord.txt.new' -UseBasicParsing -TimeoutSec 30 } catch { exit 1 }"
if !errorlevel!==0 (
    for %%A in ("!LISTS_PATH!ipset-discord.txt.new") do set "filesize=%%~zA"
    if !filesize! GTR 100 (
        move /y "!LISTS_PATH!ipset-discord.txt.new" "!LISTS_PATH!ipset-discord.txt" > nul
        call :PrintGreen "Updated ipset-discord.txt"
    ) else (
        del /f /q "!LISTS_PATH!ipset-discord.txt.new" 2>nul
        call :PrintYellow "Discord IPs too small, skipped"
    )
) else (
    del /f /q "!LISTS_PATH!ipset-discord.txt.new" 2>nul
    call :PrintYellow "Discord IPs not updated (optional)"
)

:: community hostlist
echo [3/3] Downloading community hostlist...
powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri 'https://antifilter.download/list/domains.lst' -OutFile '!LISTS_PATH!list-community.txt.new' -UseBasicParsing -TimeoutSec 30 } catch { exit 1 }"
if !errorlevel!==0 (
    for %%A in ("!LISTS_PATH!list-community.txt.new") do set "filesize=%%~zA"
    if !filesize! GTR 1000 (
        move /y "!LISTS_PATH!list-community.txt.new" "!LISTS_PATH!list-community.txt" > nul
        call :PrintGreen "Updated list-community.txt"
    ) else (
        del /f /q "!LISTS_PATH!list-community.txt.new" 2>nul
        call :PrintYellow "Community hostlist too small, skipped"
    )
) else (
    del /f /q "!LISTS_PATH!list-community.txt.new" 2>nul
    call :PrintYellow "Community hostlist not updated (optional)"
)

echo:
call :PrintGreen "IP lists update complete!"
call :PrintYellow "Restart zapret service to apply changes"
echo:
pause
goto menu


:: Utility functions
:PrintGreen
powershell -Command "Write-Host \"%~1\" -ForegroundColor Green" 2>nul || echo [OK] %~1
exit /b

:PrintRed
powershell -Command "Write-Host \"%~1\" -ForegroundColor Red" 2>nul || echo [ERROR] %~1
exit /b

:PrintYellow
powershell -Command "Write-Host \"%~1\" -ForegroundColor Yellow" 2>nul || echo [WARN] %~1
exit /b

:check_extracted
set "extracted=1"

if not exist "%~dp0bin\" set "extracted=0"

if "%extracted%"=="0" (
    echo Zapret must be extracted from archive first
    pause
    exit
)
exit /b 0


:: CHECK COMMAND
:check_command
where %~1 >nul 2>&1
if errorlevel 1 (
    call :PrintRed "Command '%~1' not found"
)
exit /b


:: CHECK UPDATES SWITCH
:check_updates_switch_status
chcp 437 > nul

set "checkUpdatesFlag=%~dp0utils\check_updates.enabled"

if exist "%checkUpdatesFlag%" (
    set "CheckUpdatesStatus=enabled"
) else (
    set "CheckUpdatesStatus=disabled"
)
exit /b


:check_updates_switch
chcp 437 > nul
cls

set "checkUpdatesFlag=%~dp0utils\check_updates.enabled"

if not exist "%~dp0utils\" mkdir "%~dp0utils"

if not exist "%checkUpdatesFlag%" (
    echo Enabling check updates...
    echo ENABLED > "%checkUpdatesFlag%"
) else (
    echo Disabling check updates...
    del /f /q "%checkUpdatesFlag%"
)

pause
goto menu


:: IPSET SWITCH
:ipset_switch_status
chcp 437 > nul

set "listFile=%~dp0lists\ipset-cloudflare.txt"
for /f %%i in ('type "%listFile%" 2^>nul ^| find /c /v ""') do set "lineCount=%%i"

if !lineCount!==0 (
    set "IPsetStatus=any"
) else (
    findstr /R "^203\.0\.113\.113/32$" "%listFile%" >nul
    if !errorlevel!==0 (
        set "IPsetStatus=none"
    ) else (
        set "IPsetStatus=loaded"
    )
)
exit /b


:ipset_switch
chcp 437 > nul
cls

set "listFile=%~dp0lists\ipset-cloudflare.txt"
set "backupFile=%listFile%.backup"

if "%IPsetStatus%"=="loaded" (
    echo Switching to none mode...
    copy /y "%listFile%" "%backupFile%" > nul
    echo 203.0.113.113/32 > "%listFile%"
    call :PrintYellow "Restart zapret to apply changes"
) else if "%IPsetStatus%"=="none" (
    echo Switching to any mode...
    echo: > "%listFile%"
    call :PrintYellow "Restart zapret to apply changes"
) else (
    echo Switching to loaded mode...
    if exist "%backupFile%" (
        copy /y "%backupFile%" "%listFile%" > nul
        call :PrintYellow "Restored from backup. Restart zapret to apply changes"
    ) else (
        call :PrintRed "No backup found. Please update IP lists first."
    )
)

pause
goto menu


:: CHECK UPDATES
:service_check_updates
cls
chcp 65001 > nul

echo Checking for updates...
echo:

set "UPDATE_URL=https://api.github.com/repos/Flowseal/zapret-discord-youtube/releases/latest"
set "TEMP_FILE=%TEMP%\zapret_update_check.txt"

powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { $r = Invoke-WebRequest -Uri '%UPDATE_URL%' -UseBasicParsing -TimeoutSec 10; $r.Content | Out-File -FilePath '%TEMP_FILE%' -Encoding UTF8 } catch { exit 1 }" 2>nul

if !errorlevel!==0 (
    for /f "tokens=2 delims=:," %%a in ('findstr /i "tag_name" "%TEMP_FILE%"') do (
        set "REMOTE_VERSION=%%~a"
        set "REMOTE_VERSION=!REMOTE_VERSION: =!"
        set "REMOTE_VERSION=!REMOTE_VERSION:"=!"
    )
    
    if defined REMOTE_VERSION (
        echo Local version:  v%LOCAL_VERSION%
        echo Remote version: !REMOTE_VERSION!
        echo:
        
        if "!REMOTE_VERSION!" NEQ "v%LOCAL_VERSION%" (
            call :PrintYellow "New version available!"
            echo Download from: https://github.com/Flowseal/zapret-discord-youtube/releases
        ) else (
            call :PrintGreen "You have the latest version"
        )
    ) else (
        call :PrintYellow "Could not parse version info"
    )
    
    del /f /q "%TEMP_FILE%" 2>nul
) else (
    call :PrintRed "Failed to check for updates"
)

echo:
if not "%~1"=="soft" pause
if not "%~1"=="soft" goto menu
exit /b


:: HOSTS UPDATE
:hosts_update
chcp 437 > nul
cls

set "hostsFile=%SystemRoot%\System32\drivers\etc\hosts"
set "hostsUrl=https://raw.githubusercontent.com/nickspaargaren/no-google/master/categories/discord-hosts.txt"
set "tempFile=%TEMP%\zapret_hosts.txt"
set "needsUpdate=0"

echo Checking hosts file for Discord entries...
echo:

:: Download Discord hosts
if exist "%SystemRoot%\System32\curl.exe" (
    curl -L -s -o "%tempFile%" "%hostsUrl%" 2>nul
) else (
    powershell -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; try { Invoke-WebRequest -Uri '%hostsUrl%' -OutFile '%tempFile%' -UseBasicParsing -TimeoutSec 10 } catch { exit 1 }" 2>nul
)

if not exist "%tempFile%" (
    call :PrintRed "Failed to download hosts file from repository"
    pause
    goto menu
)

:: Check if hosts already has Discord entries
findstr /i "discord" "%hostsFile%" >nul 2>&1
if !errorlevel! neq 0 (
    echo Discord entries not found in hosts file
    set "needsUpdate=1"
)

if "%needsUpdate%"=="1" (
    echo:
    call :PrintYellow "Hosts file may need Discord entries for voice to work"
    call :PrintYellow "Opening the downloaded file and hosts location..."
    
    start notepad "%tempFile%"
    explorer /select,"%hostsFile%"
    
    echo:
    echo Please manually copy the entries from the downloaded file to your hosts file
    echo as Administrator, then save and restart Discord.
) else (
    call :PrintGreen "Hosts file appears to have Discord entries already"
    if exist "%tempFile%" del /f /q "%tempFile%"
)

echo:
pause
goto menu


:: RUN TESTS
:run_tests
chcp 65001 >nul
cls

:: Check PowerShell version
powershell -NoProfile -Command "if ($PSVersionTable -and $PSVersionTable.PSVersion -and $PSVersionTable.PSVersion.Major -ge 3) { exit 0 } else { exit 1 }" >nul 2>&1
if %errorLevel% neq 0 (
    call :PrintRed "PowerShell 3.0 or newer is required"
    echo Please upgrade PowerShell and rerun this script.
    echo:
    pause
    goto menu
)

:: Check if test script exists
if not exist "%~dp0utils\test zapret.ps1" (
    call :PrintRed "Test script not found: utils\test zapret.ps1"
    pause
    goto menu
)

echo Starting configuration tests in PowerShell window...
echo:
start "" powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0utils\test zapret.ps1"
pause
goto menu
