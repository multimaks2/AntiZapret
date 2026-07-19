# Pack AntiZapret release zip without user settings or cache.
# Usage:
#   .\utils\buildactions\pack-release.ps1
#   .\utils\buildactions\pack-release.ps1 -Version 1.2.0

param(
	[string]$Version = "",
	[string]$Configuration = "Release",
	[string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not (Test-Path (Join-Path $root "premake5.lua"))) {
	$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

$bin = Join-Path $root "bin\x32"
$exe = Join-Path $bin "AntiZapret.exe"
if (-not (Test-Path $exe)) {
	throw "Build output missing: $exe"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
	$versionHeader = Get-Content (Join-Path $root "source\version.h") -Raw
	if ($versionHeader -match 'ANTIZAPRET_VERSION\s+"([^"]+)"') {
		$Version = $Matches[1]
	} else {
		$Version = "0.0.0"
	}
}

if ([string]::IsNullOrWhiteSpace($OutDir)) {
	$OutDir = $env:TEMP
}

$stageName = "AntiZapret-$Version-win32"
$stage = Join-Path $OutDir $stageName
$zip = Join-Path $OutDir "$stageName.zip"

Write-Host "Packing $stageName ..."
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $zip -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $stage | Out-Null

Copy-Item $exe $stage

$updater = Join-Path $bin "AntiZapret-Updater.exe"
if (Test-Path $updater) {
	Copy-Item $updater $stage
} else {
	Write-Warning "Missing updater: $updater"
}

$versionTxt = Join-Path $bin "version.txt"
if (Test-Path $versionTxt) {
	Copy-Item $versionTxt $stage
} else {
	Set-Content -LiteralPath (Join-Path $stage "version.txt") -Value $Version -NoNewline -Encoding ascii
}

foreach ($folder in @("anti-zapret", "tg-ws-proxy")) {
	$src = Join-Path $bin $folder
	if (-not (Test-Path $src)) {
		Write-Warning "Missing runtime folder: $src"
		continue
	}
	$dst = Join-Path $stage $folder
	Copy-Item $src $dst -Recurse
}

# Strip Python caches from tg-ws-proxy
$tgDst = Join-Path $stage "tg-ws-proxy"
if (Test-Path $tgDst) {
	Get-ChildItem $tgDst -Recurse -Directory -Filter "__pycache__" -ErrorAction SilentlyContinue |
		Remove-Item -Recurse -Force
}

# vpn/: binaries only (no cache/, config.yaml, settings)
$vpnSrc = Join-Path $bin "vpn"
$vpnDst = Join-Path $stage "vpn"
New-Item -ItemType Directory -Path $vpnDst | Out-Null
if (Test-Path $vpnSrc) {
	$allowed = @("mihomo.exe", "wintun.dll", "EnableLoopback.exe")
	Get-ChildItem $vpnSrc -File -Force | Where-Object { $allowed -contains $_.Name } | ForEach-Object {
		Copy-Item $_.FullName (Join-Path $vpnDst $_.Name) -Force
	}
}

# Explicitly never ship user data
foreach ($blocked in @("settings.ini", "cache", "result.ini", "smart_strategy.ini", "ai_strategy.ini")) {
	$p = Join-Path $stage $blocked
	if (Test-Path $p) {
		Remove-Item $p -Recurse -Force
	}
}

Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

# Verify zip has no sensitive payloads
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
	$bad = @($archive.Entries | Where-Object {
		$_.FullName -match '(^|[/\\])(settings\.ini|cache([/\\]|$)|config\.yaml|cache\.db|nodes\.txt|state\.ini|result\.ini|smart_strategy\.ini)'
	})
	if ($bad.Count -gt 0) {
		$bad | ForEach-Object { Write-Host "BAD: $($_.FullName)" }
		throw "Release zip contains forbidden user/cache files."
	}
	Write-Host "OK entries=$($archive.Entries.Count) zip=$zip"
} finally {
	$archive.Dispose()
}

Write-Output $zip
