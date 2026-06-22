param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
	[string]$BuildRoot = "build-vs2026",
	[string]$ReleaseDir = "Release",
	[string]$OutRoot = "portable",
	[string]$Version = "1.0.3",
	[string]$Name = "",
	[switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path $RepoRoot).Path
$BuildPath = Join-Path $RepoRoot $BuildRoot
$ReleasePath = Join-Path $RepoRoot $ReleaseDir
$OutRootPath = Join-Path $RepoRoot $OutRoot
if([string]::IsNullOrWhiteSpace($Name))
{
	$Name = "AetherClient-v$Version"
}
$PortablePath = Join-Path $OutRootPath $Name
$ZipPath = "$PortablePath.zip"
$ExeNames = @("Aether.exe", "Vera.exe", "Via.exe", "Vex.exe")
$ServerExe = "Aether-Server.exe"
$BadgeNames = @("founder.png", "tester.png", "chess_winner.png")
$LogoNames = @(
	"aether_icon_small_256.png", "vera_icon_small_256.png", "via_icon_small_256.png", "vex_icon_small_256.png",
	"aether_lockup_1024.png", "vera_lockup_1024.png", "via_lockup_1024.png", "vex_lockup_1024.png"
)

function Remove-LegacyAetherData($Root)
{
	$LegacyAetherPath = Join-Path $Root "data\aether"
	if(Test-Path $LegacyAetherPath)
	{
		Remove-Item -LiteralPath $LegacyAetherPath -Recurse -Force
	}
}

if(!$SkipBuild)
{
	cmake -S $RepoRoot -B $BuildPath -DAUTOUPDATE=ON -DDISCORD=ON -DSTEAM=ON -DSERVER_EXECUTABLE=Aether-Server
	cmake --build $BuildPath --config Release --target game-client-family game-server
}

$DataPath = Join-Path $BuildPath "data"
if(!(Test-Path $DataPath))
{
	throw "Build data folder not found: $DataPath"
}

foreach($Exe in $ExeNames)
{
	$ExePath = Join-Path $BuildPath $Exe
	if(!(Test-Path $ExePath))
	{
		throw "Family executable missing: $ExePath"
	}
	$ExeBytes = [System.IO.File]::ReadAllBytes($ExePath)
	$ExeText = [System.Text.Encoding]::ASCII.GetString($ExeBytes)
	if(!$ExeText.Contains("AetherClientTeam/AetherClient") -or !$ExeText.Contains("Updating..."))
	{
		throw "AUTOUPDATE appears disabled or misconfigured in: $ExePath"
	}
}
$ServerPath = Join-Path $BuildPath $ServerExe
if(!(Test-Path $ServerPath))
{
	throw "Server executable missing: $ServerPath"
}
$DiscordDllPath = Join-Path $BuildPath "discord_game_sdk.dll"
if(!(Test-Path $DiscordDllPath))
{
	throw "Discord SDK DLL missing: $DiscordDllPath"
}
$SteamDllPath = Join-Path $BuildPath "steam_api.dll"
if(!(Test-Path $SteamDllPath))
{
	throw "Steam API DLL missing: $SteamDllPath"
}
$SteamAppIdPath = Join-Path $BuildPath "steam_appid.txt"
if(!(Test-Path $SteamAppIdPath))
{
	throw "Steam app id file missing: $SteamAppIdPath"
}

New-Item -ItemType Directory -Force -Path $ReleasePath | Out-Null
foreach($Exe in $ExeNames)
{
	Copy-Item -LiteralPath (Join-Path $BuildPath $Exe) -Destination (Join-Path $ReleasePath $Exe) -Force
}
Copy-Item -LiteralPath $ServerPath -Destination (Join-Path $ReleasePath $ServerExe) -Force
Get-ChildItem -LiteralPath $BuildPath -Filter "*.dll" | ForEach-Object {
	Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $ReleasePath $_.Name) -Force
}
Copy-Item -LiteralPath $DataPath -Destination $ReleasePath -Recurse -Force
Remove-LegacyAetherData $ReleasePath
Copy-Item -LiteralPath $SteamAppIdPath -Destination (Join-Path $ReleasePath "steam_appid.txt") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "storage.cfg") -Destination (Join-Path $ReleasePath "storage.cfg") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "license.txt") -Destination (Join-Path $ReleasePath "license.txt") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "NOTICE-AETHER.txt") -Destination (Join-Path $ReleasePath "NOTICE-AETHER.txt") -Force

if(Test-Path $PortablePath)
{
	Remove-Item -LiteralPath $PortablePath -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PortablePath | Out-Null
foreach($Exe in $ExeNames)
{
	Copy-Item -LiteralPath (Join-Path $BuildPath $Exe) -Destination (Join-Path $PortablePath $Exe) -Force
}
Copy-Item -LiteralPath $ServerPath -Destination (Join-Path $PortablePath $ServerExe) -Force
Get-ChildItem -LiteralPath $BuildPath -Filter "*.dll" | ForEach-Object {
	Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $PortablePath $_.Name) -Force
}
Copy-Item -LiteralPath $DataPath -Destination $PortablePath -Recurse -Force
Remove-LegacyAetherData $PortablePath
Copy-Item -LiteralPath $SteamAppIdPath -Destination (Join-Path $PortablePath "steam_appid.txt") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "storage.cfg") -Destination (Join-Path $PortablePath "storage.cfg") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "license.txt") -Destination (Join-Path $PortablePath "license.txt") -Force
Copy-Item -LiteralPath (Join-Path $RepoRoot "NOTICE-AETHER.txt") -Destination (Join-Path $PortablePath "NOTICE-AETHER.txt") -Force

foreach($Root in @($ReleasePath, $PortablePath))
{
	foreach($Exe in $ExeNames)
	{
		$Path = Join-Path $Root $Exe
		if(!(Test-Path $Path))
		{
			throw "Packaged executable missing: $Path"
		}
	}
	if(!(Test-Path (Join-Path $Root $ServerExe)))
	{
		throw "Packaged server executable missing in $Root"
	}
	if(!(Test-Path (Join-Path $Root "discord_game_sdk.dll")))
	{
		throw "Packaged Discord SDK DLL missing in $Root"
	}
	if(!(Test-Path (Join-Path $Root "steam_api.dll")))
	{
		throw "Packaged Steam API DLL missing in $Root"
	}
	if(!(Test-Path (Join-Path $Root "steam_appid.txt")))
	{
		throw "Packaged steam_appid.txt missing in $Root"
	}
	foreach($Badge in $BadgeNames)
	{
		$Path = Join-Path $Root ("data\core\badges\" + $Badge)
		if(!(Test-Path $Path))
		{
			throw "Packaged badge missing: $Path"
		}
	}
	foreach($Logo in $LogoNames)
	{
		$Path = Join-Path $Root ("data\core\logos\" + $Logo)
		if(!(Test-Path $Path))
		{
			throw "Packaged logo missing: $Path"
		}
	}
	if(!(Test-Path (Join-Path $Root "storage.cfg")))
	{
		throw "Packaged storage.cfg missing in $Root"
	}
	if(!(Test-Path (Join-Path $Root "license.txt")) -or !(Test-Path (Join-Path $Root "NOTICE-AETHER.txt")))
	{
		throw "Packaged license/notice missing in $Root"
	}
	if(!(Test-Path (Join-Path $Root "data\shader")))
	{
		throw "Packaged shader folder missing in $Root"
	}
	if(Test-Path (Join-Path $Root "data\aether"))
	{
		throw "Legacy data\aether folder must not be packaged in $Root"
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
	ReleasePath = $ReleasePath
	Executables = $ExeNames
	ServerExecutable = $ServerExe
}
