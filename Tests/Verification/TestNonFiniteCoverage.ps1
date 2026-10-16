<#
.SYNOPSIS
Checks non-finite fail/report coverage of captures, exports and complete sequences.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')][string[]]$Configuration = @('Debug', 'Release'),
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sessionScript = Join-Path $root 'Scripts/Verification/GGLabSession.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteCoverage' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$cases = @()

function Save([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), $utf8)
}
function Call([string]$Verb, [string]$Session, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $json = ($Options | ConvertTo-Json -Compress -Depth 12).Replace("'", "''")
    $script = $sessionScript.Replace("'", "''")
    $command = "`$ProgressPreference='SilentlyContinue'; `$p=@{}; `$data='$json'|ConvertFrom-Json; " +
        "foreach (`$v in `$data.PSObject.Properties) {`$p[`$v.Name]=`$v.Value}; & '$script' '$Verb' -Session '$Session' @p"
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command)))
    if ($LASTEXITCODE -and -not $AllowFailure) { throw "Session $Session $Verb failed: $text" }
    return ($text -join "`n") | ConvertFrom-Json
}
function Wait-Clean([string]$Session, [bool]$Counters) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $status = Call status $Session
        $report = $status.watch.report
        if ($null -ne $report.firstAnomaly) { throw 'Unexpected native numerical anomaly' }
        if ($report.boundary -eq 'Passed' -and (-not $Counters -or $report.production -eq 'Passed')) { return $status }
        if ((Get-Date) -ge $deadline) { throw "Watch never became clean: $($report | ConvertTo-Json -Compress -Depth 12)" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Check-Coverage($Coverage, [bool]$Counters, [long]$Frames) {
    if (-not $Coverage.complete -or $Coverage.frames -ne $Frames -or $Coverage.completedFrames -ne $Frames -or
        $Coverage.boundary -ne 'Passed' -or $Coverage.mode -ne 'fail' -or $Coverage.failure) { throw 'Incomplete successful coverage range' }
    $production = if ($Counters) { 'Passed' } else { 'Unavailable' }
    if ($Coverage.production -ne $production) { throw 'Wrong production claim' }
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($counters in @($true, $false)) {
                $arm = if ($counters) { 'on' } else { 'off' }
                $directory = Join-Path $output "$config-$backend-$arm"
                New-Item -ItemType Directory -Path $directory | Out-Null
                $session = "coverage-$runId-$config-$backend-$arm"
                $start = $null
                try {
                    $start = Call start $session @{ Configuration = $config; Rhi = $backend; Lab = 'gglab.lab.temporal_aa';
                        NoDevTools = $true; WindowSize = '320x180'; StateRoot = "$output/$backend-state";
                        WatchNonFinite = $counters; StartTimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/start.json" $start
                    $null = Call capture $session @{ SettleFrames = 2; OutputDirectory = "$directory/Startup"; TimeoutSeconds = $TimeoutSeconds }
                    if (-not $counters) {
                        $rejected = Call watch $session @{ WatchMode = 'fail' } -AllowFailure
                        Save "$directory/requires-restart.json" $rejected
                        if ($rejected.ok -or $rejected.error -notmatch 'RequiresRestart') { throw 'Missing counter capability did not reject fail mode' }
                    }
                    $claims = if ($counters) { @('boundary', 'production') } else { @('boundary') }
                    $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = $claims }
                    Save "$directory/clean-watch.json" (Wait-Clean $session $counters)
                    $image = Call capture $session @{ SettleFrames = 2; OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/image.json" $image
                    if ($image.status -ne 'completed') { throw "Clean capture failed: $($image | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $image.watch $counters 1
                    $graph = Call graph $session @{ OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/graph.json" $graph
                    if ($graph.status -ne 'completed') { throw 'Clean graph capture failed' }
                    Check-Coverage $graph.watch $counters 1
                    $export = Call export $session @{ Targets = '[{"resource":"TAA.ResolvedSceneColor"}]';
                        IncludeGraph = $true; OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/export.json" $export
                    if ($export.status -ne 'completed') { throw "Clean resource export failed: $($export | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $export.watch $counters 1
                    $sequence = Call sequence $session @{ CameraPath = 'SEQ_TemporalAALab_MovingObject'; CaptureFrames = '0,179';
                        Exports = '[{"frame":179,"targets":[{"resource":"TAA.ResolvedSceneColor"}],"graph":true}]';
                        OutputDirectory = "$directory/Sequence"; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/sequence.json" $sequence
                    if ($sequence.sequence.state -ne 'completed') { throw "Clean sequence failed: $($sequence | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $sequence.sequence.watch $counters 180
                    $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = @('boundary'); Targets = '[{"resource":"Missing.Active.Target"}]' }
                    $bad = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/Rejected"; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/missing-capture.json" $bad
                    if ($bad.status -ne 'failed' -or $bad.failure -notmatch 'MissingCoverage.*Missing.Active.Target') { throw 'Missing target did not fail capture with its identity' }
                    $badSequence = Call sequence $session @{ CameraPath = 'SEQ_TemporalAALab_MovingObject'; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/missing-sequence.json" $badSequence
                    if ($badSequence.sequence.state -ne 'failed' -or $badSequence.sequence.failure -notmatch 'MissingCoverage') { throw 'Missing target did not fail sequence' }
                    $null = Call watch $session @{ WatchMode = 'report'; WatchClaims = @('boundary'); Targets = '[{"resource":"Missing.Active.Target"}]' }
                    $reported = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/Reported"; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/reported-capture.json" $reported
                    if ($reported.status -ne 'completed' -or -not $reported.watch.complete -or $reported.watch.boundary -ne 'Incomplete') { throw 'Report mode lost its incomplete claim' }
                    $null = Call watch $session @{ WatchOff = $true }
                    $recovered = Call capture $session @{ OutputDirectory = "$directory/Recovered"; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/recovered-capture.json" $recovered
                    if ($recovered.status -ne 'completed') { throw 'Process did not remain inspectable after fail mode detection' }
                } finally {
                    if ($null -ne $start) {
                        $stop = Call stop $session
                        Save "$directory/stop.json" $stop
                        if ($stop.exitCode -ne 0) { throw "Session exited with $($stop.exitCode)" }
                    }
                }
                $log = Get-Content -Raw $start.outputLog
                if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw 'Native validation errors in coverage session' }
                $cases += @{ configuration = $config; backend = $backend; counters = $counters; passed = $true }
            }
        }
    }
    $result = @{ ok = $true; cases = $cases; directory = $output }
    Save "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
} catch {
    $result = @{ ok = $false; cases = $cases; directory = $output; error = $_.Exception.Message }
    Save "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
    exit 1
}
