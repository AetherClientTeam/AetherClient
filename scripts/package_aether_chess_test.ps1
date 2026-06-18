param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
	[string]$BuildRoot = "build-vs2026",
	[string]$ReleaseDir = "Release",
	[string]$OutRoot = "portable",
	[string]$Name = ("AetherChessTest-" + (Get-Date -Format "yyyyMMdd-HHmmss")),
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path $RepoRoot).Path
$BuildPath = Join-Path $RepoRoot $BuildRoot
$ReleasePath = Join-Path $RepoRoot $ReleaseDir
$OutRootPath = Join-Path $RepoRoot $OutRoot
$PortablePath = Join-Path $OutRootPath $Name
$ZipPath = "$PortablePath.zip"
$SourceBadgePath = Join-Path $RepoRoot "data\core\badges"

function Copy-AetherBadges
{
	param([string]$TargetDataRoot)
	$TargetBadgePath = Join-Path $TargetDataRoot "core\badges"
	New-Item -ItemType Directory -Force -Path $TargetBadgePath | Out-Null
	foreach($Badge in @("founder.png", "tester.png", "chess_winner.png"))
	{
		$SourceBadge = Join-Path $SourceBadgePath $Badge
		if(!(Test-Path $SourceBadge))
		{
			throw "Source badge missing: $SourceBadge"
		}
		Copy-Item -LiteralPath $SourceBadge -Destination (Join-Path $TargetBadgePath $Badge) -Force
	}
}

if(!$SkipBuild)
{
	cmake --build $BuildPath --config Release --target game-client
}

$ExePath = Join-Path $BuildPath "Aether.exe"
$DataPath = Join-Path $BuildPath "data"
if(!(Test-Path $ExePath))
{
	throw "Aether.exe not found: $ExePath"
}
if(!(Test-Path $DataPath))
{
	throw "Build data folder not found: $DataPath"
}

New-Item -ItemType Directory -Force -Path $ReleasePath | Out-Null
Copy-Item -LiteralPath $ExePath -Destination (Join-Path $ReleasePath "Aether.exe") -Force
Copy-Item -LiteralPath $DataPath -Destination $ReleasePath -Recurse -Force
Copy-AetherBadges -TargetDataRoot (Join-Path $ReleasePath "data")

$ReleaseBadgePath = Join-Path $ReleasePath "data\core\badges"
foreach($Badge in @("founder.png", "tester.png", "chess_winner.png"))
{
	$BadgePath = Join-Path $ReleaseBadgePath $Badge
	if(!(Test-Path $BadgePath))
	{
		throw "Release badge missing after data sync: $BadgePath"
	}
}

New-Item -ItemType Directory -Force -Path $PortablePath | Out-Null
Copy-Item -LiteralPath $ExePath -Destination (Join-Path $PortablePath "Aether.exe") -Force
Get-ChildItem -LiteralPath $BuildPath -Filter "*.dll" | ForEach-Object {
	Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $PortablePath $_.Name) -Force
}
Copy-Item -LiteralPath $DataPath -Destination $PortablePath -Recurse -Force
Copy-AetherBadges -TargetDataRoot (Join-Path $PortablePath "data")

$PortableBadgePath = Join-Path $PortablePath "data\core\badges"
foreach($Badge in @("founder.png", "tester.png", "chess_winner.png"))
{
	$BadgePath = Join-Path $PortableBadgePath $Badge
	if(!(Test-Path $BadgePath))
	{
		throw "Portable badge missing: $BadgePath"
	}
}

if(Test-Path $ZipPath)
{
	Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $PortablePath "*") -DestinationPath $ZipPath -Force

[pscustomobject]@{
	PortablePath = $PortablePath
	ZipPath = $ZipPath
	ReleaseExe = (Join-Path $ReleasePath "Aether.exe")
	ReleaseBadges = $ReleaseBadgePath
}
