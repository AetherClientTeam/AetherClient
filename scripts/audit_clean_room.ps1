param(
	[string[]]$ForbiddenRoots = @()
)

$ErrorActionPreference = "Stop"
$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Violations = [System.Collections.Generic.List[string]]::new()

function Get-RelativePath([string]$BasePath, [string]$Path)
{
	$BaseUri = [System.Uri]::new(($BasePath.TrimEnd("\") + "\"))
	$PathUri = [System.Uri]::new($Path)
	return [System.Uri]::UnescapeDataString($BaseUri.MakeRelativeUri($PathUri).ToString()).Replace("/", "\")
}

$TextPatterns = @(
	"BestProject",
	"BestClient",
	"components/bestclient",
	"components/aetherclient",
	"\bbc_[A-Za-z0-9_]*",
	"\bac_[A-Za-z0-9_]*",
	"update\.tclient\.app",
	"sjrc6\.github\.io",
	"\bNETMSG_IAMTATER\b",
	"\bTATER_CHECKSUM_(REQUEST|RESPONSE)\b"
)

$SourceFiles = Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File |
	Where-Object {
		$_.FullName -notmatch "[\\/]\.git[\\/]" -and
		$_.FullName -notmatch "[\\/]ddnet-libs[\\/]" -and
		$_.FullName -notmatch "[\\/]build[^\\/]*[\\/]" -and
		$_.FullName -notmatch "[\\/]Release[\\/]" -and
		$_.FullName -notmatch "[\\/]docs[\\/]CLEAN_ROOM_POLICY\.md$" -and
		$_.FullName -notmatch "[\\/]docs[\\/]TCLIENT_INTEGRATION\.md$" -and
		$_.FullName -notmatch "[\\/]scripts[\\/]audit_clean_room\.ps1$" -and
		$_.FullName -notmatch "[\\/]src[\\/]engine[\\/]client[\\/]keynames\.cpp$" -and
		$_.Extension -in @(".c", ".cc", ".cpp", ".h", ".hpp", ".cmake", ".txt", ".md", ".py", ".ps1")
	}

foreach($Pattern in $TextPatterns)
{
	$Matches = $SourceFiles | Select-String -Pattern $Pattern -CaseSensitive:$false
	foreach($Match in $Matches)
	{
		$RelativePath = Get-RelativePath $RepositoryRoot $Match.Path
		$Violations.Add("Forbidden text '$Pattern' at ${RelativePath}:$($Match.LineNumber)")
	}
}

$MumblePatterns = @(
	"components/tclient/mumble",
	"engine/external/mumble",
	"\bCMumble\b",
	"\bmumble_reconnect\b"
)

foreach($Pattern in $MumblePatterns)
{
	$Matches = $SourceFiles | Select-String -Pattern $Pattern -CaseSensitive:$false
	foreach($Match in $Matches)
	{
		$RelativePath = Get-RelativePath $RepositoryRoot $Match.Path
		$Violations.Add("Excluded Mumble integration '$Pattern' at ${RelativePath}:$($Match.LineNumber)")
	}
}

$RepositoryHashes = @{}
Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -File |
	Where-Object {
		$_.FullName -notmatch "[\\/]\.git[\\/]" -and
		$_.FullName -notmatch "[\\/]ddnet-libs[\\/]" -and
		$_.FullName -notmatch "[\\/]build[^\\/]*[\\/]" -and
		$_.FullName -notmatch "[\\/]Release[\\/]"
	} |
	ForEach-Object {
		$Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
		if(-not $RepositoryHashes.ContainsKey($Hash))
		{
			$RepositoryHashes[$Hash] = Get-RelativePath $RepositoryRoot $_.FullName
		}
	}

foreach($ForbiddenRootInput in $ForbiddenRoots)
{
	$ForbiddenRoot = (Resolve-Path -LiteralPath $ForbiddenRootInput).Path
	if($ForbiddenRoot -eq $RepositoryRoot -or $ForbiddenRoot.StartsWith("$RepositoryRoot\"))
	{
		throw "Forbidden root must be outside the clean repository: $ForbiddenRoot"
	}

	Get-ChildItem -LiteralPath $ForbiddenRoot -Recurse -File |
		ForEach-Object {
			$Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
			if($RepositoryHashes.ContainsKey($Hash))
			{
				$ForbiddenRelative = Get-RelativePath $ForbiddenRoot $_.FullName
				$Violations.Add("Exact forbidden-file duplicate: $($RepositoryHashes[$Hash]) == $ForbiddenRelative")
			}
		}
}

if($Violations.Count -gt 0)
{
	$Violations | ForEach-Object { Write-Error $_ }
	exit 1
}

Write-Host "Clean-room audit passed."
