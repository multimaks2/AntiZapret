@echo off

cd /d "%~dp0"

rem Generate solutions
utils\premake5.exe vs2026

if %0 == "%~0" pause
