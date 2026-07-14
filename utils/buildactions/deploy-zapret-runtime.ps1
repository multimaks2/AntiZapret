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

$TargetAntiZapret = Join-Path $TargetRoot 'anti-zapret'
$TargetBin = Join-Path $TargetAntiZapret 'bin'
$TargetLists = Join-Path $TargetAntiZapret 'lists'
New-Item -ItemType Directory -Force -Path $TargetBin, $TargetLists | Out-Null

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

Write-Host "Deployed zapret runtime to $TargetRoot (copied=$copied skipped=$skipped locked=$locked)"
if ($locked -gt 0) {
	Write-Host "Some files are in use. Close AntiZapret/winws.exe and rebuild to refresh runtime files."
}
