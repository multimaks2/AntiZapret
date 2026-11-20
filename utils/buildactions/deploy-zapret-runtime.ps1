param(
	[Parameter(Mandatory = $true)]
	[string]$TargetDir,
	[string]$VpnRuntimeDir = $env:ANTIZAPRET_VPN_RUNTIME_DIR
)

$ErrorActionPreference = 'Stop'

$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Vendor = Join-Path $Root 'vendor\zapret-discord-youtube'
$TargetRoot = if ([System.IO.Path]::IsPathRooted($TargetDir)) { $TargetDir } else { Join-Path $Root $TargetDir }

foreach ($required in @('bin\winws.exe', 'general.bat')) {
	if (-not (Test-Path -LiteralPath (Join-Path $Vendor $required) -PathType Leaf)) {
		throw "Flowseal submodule is incomplete: missing $required in $Vendor. Run git submodule update --init --recursive."
	}
}

function Copy-RuntimeFile {
	param(
		[string]$Source,
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		throw "Missing runtime file: $Source"
	}

	$destinationDir = Split-Path -Parent $Destination
	if (-not (Test-Path -LiteralPath $destinationDir)) {
		New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
	}

	if ((Test-Path -LiteralPath $Destination) -and ((Get-Item -LiteralPath $Source).Length -eq (Get-Item -LiteralPath $Destination).Length)) {
		try {
			$sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
			$destinationHash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
			if ($sourceHash -eq $destinationHash) {
				return 'skipped'
			}
		}
		catch {
		}
	}

	for ($attempt = 1; $attempt -le 3; ++$attempt) {
		try {
			Copy-Item -LiteralPath $Source -Destination $Destination -Force
			return 'copied'
		}
		catch [System.IO.IOException] {
			if ($attempt -eq 3) {
				Write-Warning "Could not update locked file: $Destination"
				return 'locked'
			}
			Start-Sleep -Milliseconds 200
		}
	}

	return 'locked'
}

$TargetAntiZapret = Join-Path $TargetRoot 'zapret-discord-youtube'

$copied = 0
$skipped = 0
$locked = 0

# Keep the upstream runtime layout; Flowseal owns the asset list.
foreach ($directory in @('bin', 'lists', '.service', 'utils')) {
	$sourceDir = Join-Path $Vendor $directory
	if (-not (Test-Path -LiteralPath $sourceDir -PathType Container)) { continue }
	foreach ($file in (Get-ChildItem -LiteralPath $sourceDir -File -Recurse -Force)) {
		$relative = $file.FullName.Substring($sourceDir.Length + 1)
		$destination = Join-Path (Join-Path $TargetAntiZapret $directory) $relative
		if ($directory -eq 'lists' -and $file.Name -like '*-user.txt' -and (Test-Path -LiteralPath $destination -PathType Leaf)) {
			++$skipped
			continue
		}
		switch (Copy-RuntimeFile -Source $file.FullName -Destination $destination) {
			'copied' { ++$copied }
			'skipped' { ++$skipped }
			'locked' { ++$locked }
		}
	}
}

# Flowseal root: strategies + version (same layout as downloaded github repo).
Get-ChildItem -LiteralPath $Vendor -Filter 'general*.bat' -File -ErrorAction SilentlyContinue | ForEach-Object {
	switch (Copy-RuntimeFile -Source $_.FullName -Destination (Join-Path $TargetAntiZapret $_.Name)) {
		'copied' { ++$copied }
		'skipped' { ++$skipped }
		'locked' { ++$locked }
	}
}

foreach ($rootFile in @('service.bat', 'LICENSE.txt')) {
	$src = Join-Path $Vendor $rootFile
	if (Test-Path -LiteralPath $src) {
		switch (Copy-RuntimeFile -Source $src -Destination (Join-Path $TargetAntiZapret $rootFile)) {
			'copied' { ++$copied }
			'skipped' { ++$skipped }
			'locked' { ++$locked }
		}
	}
}

# Stage the Windows Telegram proxy from its pinned submodule, without local state.
$tgSource = Join-Path $Root 'vendor\tg-ws-proxy'
$tgTarget = Join-Path $TargetRoot 'tg-ws-proxy'
if (-not (Test-Path -LiteralPath (Join-Path $tgSource 'proxy\tg_ws_proxy.py') -PathType Leaf)) {
	throw "Telegram proxy submodule is incomplete: $tgSource. Run git submodule update --init --recursive."
}
foreach ($entry in @('proxy', 'ui', 'utils', 'docs', 'windows.py', 'pyproject.toml', 'icon.ico', 'LICENSE')) {
	$source = Join-Path $tgSource $entry
	if (Test-Path -LiteralPath $source -PathType Leaf) {
		Copy-RuntimeFile -Source $source -Destination (Join-Path $tgTarget $entry) | Out-Null
	} elseif (Test-Path -LiteralPath $source -PathType Container) {
		foreach ($file in (Get-ChildItem -LiteralPath $source -File -Recurse)) {
			$relative = $file.FullName.Substring($tgSource.Length + 1)
			if ($relative -match '(^|\\)__pycache__(\\|$)|\.py[co]$') { continue }
			Copy-RuntimeFile -Source $file.FullName -Destination (Join-Path $tgTarget $relative) | Out-Null
		}
	}
}

# Seed missing VPN binaries only, never settings or existing local updates.
if (-not $VpnRuntimeDir -and ${env:ProgramFiles(x86)}) {
	$VpnRuntimeDir = Join-Path ${env:ProgramFiles(x86)} 'AntiZapret\vpn'
}
$vpnTarget = Join-Path $TargetRoot 'vpn'
foreach ($file in @('mihomo.exe', 'wintun.dll', 'EnableLoopback.exe')) {
	$destination = Join-Path $vpnTarget $file
	if (Test-Path -LiteralPath $destination -PathType Leaf) { continue }
	if ($VpnRuntimeDir) {
		$source = Join-Path $VpnRuntimeDir $file
		if (Test-Path -LiteralPath $source -PathType Leaf) {
			Copy-RuntimeFile -Source $source -Destination $destination | Out-Null
			continue
		}
	}
	Write-Warning "VPN runtime file not staged: $file. Set ANTIZAPRET_VPN_RUNTIME_DIR to a folder with the VPN binaries."
}

# App version.txt next to AntiZapret.exe / AntiZapret-Updater.exe (from source/AntiZapret/version.h).
$appVersion = '0.0.0'
$versionHeader = Join-Path $Root 'source\AntiZapret\version.h'
if (Test-Path -LiteralPath $versionHeader) {
	$headerText = Get-Content -LiteralPath $versionHeader -Raw
	if ($headerText -match 'ANTIZAPRET_VERSION\s+"([^"]+)"') {
		$appVersion = $Matches[1]
	}
}
$appVersionPath = Join-Path $TargetRoot 'version.txt'
Set-Content -LiteralPath $appVersionPath -Value $appVersion -NoNewline -Encoding ascii
Write-Host "Wrote app version.txt: $appVersion"

# Font Awesome webfonts (sidebar Solid + Brands for services).
$fontsSrc = Join-Path $Root 'vendor\fontawesome'
$fontsDst = Join-Path $TargetRoot 'fonts'
if (Test-Path -LiteralPath $fontsSrc) {
	New-Item -ItemType Directory -Force -Path $fontsDst | Out-Null
	foreach ($fontName in @('fa-brands-400.ttf', 'fa-solid-900.ttf')) {
		$src = Join-Path $fontsSrc $fontName
		if (Test-Path -LiteralPath $src) {
			Copy-Item -LiteralPath $src -Destination (Join-Path $fontsDst $fontName) -Force
		}
	}
}

Write-Host "Deployed zapret runtime to $TargetRoot (copied=$copied skipped=$skipped locked=$locked)"
if ($locked -gt 0) {
	Write-Host "Some files are in use. Close AntiZapret/winws.exe and rebuild to refresh runtime files."
}
