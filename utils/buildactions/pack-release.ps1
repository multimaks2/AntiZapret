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
	$versionHeader = Get-Content (Join-Path $root "source\AntiZapret\version.h") -Raw
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

$fontsDst = Join-Path $stage "fonts"
New-Item -ItemType Directory -Path $fontsDst | Out-Null
foreach ($font in @('fa-brands-400.ttf', 'fa-solid-900.ttf')) {
	Copy-Item -LiteralPath (Join-Path $root "vendor/fontawesome/$font") -Destination $fontsDst
}

$versionTxt = Join-Path $bin "version.txt"
if (Test-Path $versionTxt) {
	Copy-Item $versionTxt $stage
} else {
	Set-Content -LiteralPath (Join-Path $stage "version.txt") -Value $Version -NoNewline -Encoding ascii
}

foreach ($folder in @('zapret-discord-youtube', 'tg-ws-proxy')) {
	$src = Join-Path $root "vendor/$folder"
	if (-not (Test-Path -LiteralPath (Join-Path $src '.git'))) {
		throw "Runtime submodule is not initialized: $folder"
	}
	& git -C $src diff --quiet HEAD --
	if ($LASTEXITCODE -ne 0) { throw "Runtime submodule has local changes: $folder" }
	$files = @(& git -C $src -c core.quotePath=false ls-tree -r --name-only HEAD)
	if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) { throw "Cannot list runtime files: $folder" }
	$allowedPaths = if ($folder -eq 'zapret-discord-youtube') {
		'^(?:(?:bin|lists|\.service|utils)/|general[^/]*\.bat$|service\.bat$|LICENSE\.txt$)'
	} else {
		'^(?:(?:proxy|ui|utils|docs)/|windows\.py$|pyproject\.toml$|icon\.ico$|LICENSE$)'
	}
	foreach ($file in ($files | Where-Object { $_ -match $allowedPaths })) {
		$destination = Join-Path $stage "$folder/$file"
		New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
		Copy-Item -LiteralPath (Join-Path $src $file) -Destination $destination
	}
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
foreach ($blocked in @("settings.ini", "cache", "result.ini", "ai_strategy.ini")) {
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
		$_.FullName -match '(^|[/\\])(?:settings\.ini|cache|__pycache__|config\.ya?ml|cache\.db|nodes\.txt|state\.ini|result\.ini|ai_strategy\.ini|subscriptions\.(?:json|ini|txt)|[^/\\]+\.py[co])(?:[/\\]|$)'
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
