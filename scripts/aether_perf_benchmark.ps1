param(
	[int]$Seconds = 10,
	[string]$ExePath = "",
	[string]$OutPath = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if([string]::IsNullOrWhiteSpace($ExePath)) {
	$ExePath = Join-Path $RepoRoot "Release\Aether.exe"
}
if([string]::IsNullOrWhiteSpace($OutPath)) {
	$OutPath = Join-Path $RepoRoot "tools-output\aether_perf_latest.json"
}

$ExePath = [System.IO.Path]::GetFullPath($ExePath)
$OutPath = [System.IO.Path]::GetFullPath($OutPath)

if(!(Test-Path -LiteralPath $ExePath)) {
	throw "Aether.exe was not found: $ExePath"
}

$OutDir = Split-Path -Parent $OutPath
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
if(Test-Path -LiteralPath $OutPath) {
	Remove-Item -LiteralPath $OutPath -Force
}

$CfgPath = Join-Path $OutDir "aether_perf_benchmark.cfg"
$LogPath = [System.IO.Path]::ChangeExtension($OutPath, ".log")
@(
	"logfile `"$LogPath`"",
	"ae_perf_spikes 1",
	"ae_perf_benchmark $Seconds `"$OutPath`" 1"
) | Set-Content -LiteralPath $CfgPath -Encoding ASCII

$ArgumentLine = "-f `"$CfgPath`""
$Process = Start-Process -FilePath $ExePath -ArgumentList $ArgumentLine -PassThru -WindowStyle Hidden
$TimeoutMs = [Math]::Max(($Seconds + 45) * 1000, 60000)
if(!$Process.WaitForExit($TimeoutMs)) {
	$Process.Kill()
	$Process.WaitForExit()
	throw "Aether perf benchmark timed out after $([int]($TimeoutMs / 1000)) seconds"
}
if($Process.ExitCode -ne 0) {
	throw "Aether perf benchmark exited with code $($Process.ExitCode)"
}
if(!(Test-Path -LiteralPath $OutPath)) {
	throw "Aether perf benchmark did not create a report: $OutPath"
}

$Report = Get-Content -LiteralPath $OutPath -Raw | ConvertFrom-Json
Write-Host ("Aether perf: avg FPS {0:n1}, p95 {1:n2}ms, p99 {2:n2}ms, max {3:n2}ms, samples {4}" -f $Report.avg_fps, $Report.p95_frame_ms, $Report.p99_frame_ms, $Report.max_frame_ms, $Report.samples)
Write-Host "Top components:"
$Report.top_components | Select-Object -First 8 | ForEach-Object {
	Write-Host ("  {0}: total {1:n2}ms, max {2:n2}ms, calls {3}" -f $_.name, $_.total_ms, $_.max_ms, $_.calls)
}
Write-Host "Report: $OutPath"
