param(
	[Parameter(Mandatory = $true)]
	[string]$TargetDir
)

$ErrorActionPreference = 'Stop'

$Root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Vendor = Join-Path $Root 'vendor\zapret-discord-youtube'
$TargetRoot = if ([System.IO.Path]::IsPathRooted($TargetDir)) { $TargetDir } else { Join-Path $Root $TargetDir }

$BinFiles = @(
	'winws.exe',
	'WinDivert.dll',
	'WinDivert64.sys',
	'cygwin1.dll',
	'quic_initial_dbankcloud_ru.bin',
	'quic_initial_www_google_com.bin',
	'stun.bin',
	'tls_clienthello_4pda_to.bin',
	'tls_clienthello_max_ru.bin',
	'tls_clienthello_www_google_com.bin'
)

$ListFiles = @(
	'list-general.txt',
	'list-general-user.txt',
	'list-exclude.txt',
	'list-exclude-user.txt',
	'list-google.txt',
	'list-youtube.txt',
	'list-discord.txt',
	'list-ultimate.txt',
	'ipset-all.txt',
	'ipset-cloudflare.txt',
	'ipset-discord.txt',
	'ipset-ubisoft.txt',
	'ipset-russia.txt',
	'ipset-exclude.txt',
	'ipset-exclude-user.txt'
)

function Copy-RuntimeFile {
	param(
		[string]$Source,
		[string]$Destination
	)

	if (-not (Test-Path -LiteralPath $Source)) {
		throw "Missing required zapret file: $Source"
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
$TargetBin = Join-Path $TargetAntiZapret 'bin'
$TargetLists = Join-Path $TargetAntiZapret 'lists'
$TargetService = Join-Path $TargetAntiZapret '.service'
New-Item -ItemType Directory -Force -Path $TargetBin, $TargetLists, $TargetService | Out-Null

$copied = 0
$skipped = 0
$locked = 0

foreach ($file in $BinFiles) {
	switch (Copy-RuntimeFile -Source (Join-Path $Vendor "bin\$file") -Destination (Join-Path $TargetBin $file)) {
		'copied' { ++$copied }
		'skipped' { ++$skipped }
		'locked' { ++$locked }
	}
}

foreach ($file in $ListFiles) {
	switch (Copy-RuntimeFile -Source (Join-Path $Vendor "lists\$file") -Destination (Join-Path $TargetLists $file)) {
		'copied' { ++$copied }
		'skipped' { ++$skipped }
		'locked' { ++$locked }
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

foreach ($rootFile in @('service.bat')) {
	$src = Join-Path $Vendor $rootFile
	if (Test-Path -LiteralPath $src) {
		switch (Copy-RuntimeFile -Source $src -Destination (Join-Path $TargetAntiZapret $rootFile)) {
			'copied' { ++$copied }
			'skipped' { ++$skipped }
			'locked' { ++$locked }
		}
	}
}

$versionSrc = Join-Path $Vendor '.service\version.txt'
if (Test-Path -LiteralPath $versionSrc) {
	switch (Copy-RuntimeFile -Source $versionSrc -Destination (Join-Path $TargetService 'version.txt')) {
		'copied' { ++$copied }
		'skipped' { ++$skipped }
		'locked' { ++$locked }
	}
}

# App version.txt next to AntiZapret.exe / AntiZapret-Updater.exe (from source/version.h).
$appVersion = '0.0.0'
$versionHeader = Join-Path $Root 'source\version.h'
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
