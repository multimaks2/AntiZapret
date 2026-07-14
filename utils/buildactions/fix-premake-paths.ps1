param(
	[Parameter(Mandatory = $true)]
	[string]$ProjectRoot,

	[Parameter(Mandatory = $true)]
	[string]$Drive
)

$root = $ProjectRoot.TrimEnd('\') + '\'
$drivePrefix = $Drive.TrimEnd('\') + '\'

$slnPath = Join-Path $root 'AntiZapret.slnx'
$projPath = Join-Path $root 'build\AntiZapret.vcxproj'

$sln = Get-Content $slnPath -Raw -Encoding UTF8
$sln = $sln.Replace($drivePrefix + 'build\', 'build\')
[System.IO.File]::WriteAllText($slnPath, $sln)

$proj = Get-Content $projPath -Raw -Encoding UTF8
$proj = $proj.Replace($drivePrefix + 'bin\', '$(SolutionDir)bin\')
$proj = $proj.Replace($drivePrefix + 'source\', '..\source\')
[System.IO.File]::WriteAllText($projPath, $proj)
