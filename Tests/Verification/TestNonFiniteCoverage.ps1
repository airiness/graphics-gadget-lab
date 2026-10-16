<#
.SYNOPSIS
Checks non-finite fail/report coverage of captures, exports and complete sequences.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')][string[]]$Configuration = @('Debug', 'Release'),
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [int]$TimeoutSeconds = 180,
    [string]$RunDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteCoverage' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$cases = @()

function Wait-Clean([string]$Session, [bool]$Counters) {
    $poll = { Invoke-VerificationSession status $Session }.GetNewClosure()
    $until = {
        param($status)
        $report = $status.watch.report
        if ($null -ne $report.firstAnomaly) { throw 'Unexpected native numerical anomaly' }
        return $report.boundary -eq 'Passed' -and (-not $Counters -or $report.production -eq 'Passed')
    }.GetNewClosure()
    Wait-VerificationCondition -Poll $poll -Until $until -TimeoutSeconds $TimeoutSeconds -OnTimeout {
        param($status)
        throw "Watch never became clean: $($status.watch.report | ConvertTo-Json -Compress -Depth 12)"
    }
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
                    $start = Invoke-VerificationSession start $session @{ Configuration = $config; Rhi = $backend; Lab = 'gglab.lab.temporal_aa';
                        NoDevTools = $true; WindowSize = '320x180'; StateRoot = "$output/$backend-state";
                        WatchNonFinite = $counters; StartTimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/start.json" $start
                    $null = Invoke-VerificationSession capture $session @{ SettleFrames = 2; OutputDirectory = "$directory/Startup"; TimeoutSeconds = $TimeoutSeconds }
                    if (-not $counters) {
                        $rejected = Invoke-VerificationSession watch $session @{ WatchMode = 'fail' } -AllowFailure
                        Write-VerificationJson "$directory/requires-restart.json" $rejected
                        if ($rejected.ok -or $rejected.error -notmatch 'RequiresRestart') { throw 'Missing counter capability did not reject fail mode' }
                    }
                    $claims = if ($counters) { @('boundary', 'production') } else { @('boundary') }
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = $claims }
                    Write-VerificationJson "$directory/clean-watch.json" (Wait-Clean $session $counters)
                    $image = Invoke-VerificationSession capture $session @{ SettleFrames = 2; OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/image.json" $image
                    if ($image.status -ne 'completed') { throw "Clean capture failed: $($image | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $image.watch $counters 1
                    $graph = Invoke-VerificationSession graph $session @{ OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/graph.json" $graph
                    if ($graph.status -ne 'completed') { throw 'Clean graph capture failed' }
                    Check-Coverage $graph.watch $counters 1
                    $export = Invoke-VerificationSession export $session @{ Targets = '[{"resource":"TAA.ResolvedSceneColor"}]';
                        IncludeGraph = $true; OutputDirectory = $directory; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/export.json" $export
                    if ($export.status -ne 'completed') { throw "Clean resource export failed: $($export | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $export.watch $counters 1
                    $sequence = Invoke-VerificationSession sequence $session @{ CameraPath = 'SEQ_TemporalAALab_MovingObject'; CaptureFrames = '0,179';
                        Exports = '[{"frame":179,"targets":[{"resource":"TAA.ResolvedSceneColor"}],"graph":true}]';
                        OutputDirectory = "$directory/Sequence"; Label = 'checked'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/sequence.json" $sequence
                    if ($sequence.sequence.state -ne 'completed') { throw "Clean sequence failed: $($sequence | ConvertTo-Json -Compress -Depth 12)" }
                    Check-Coverage $sequence.sequence.watch $counters 180
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('boundary'); Targets = '[{"resource":"Missing.Active.Target"}]' }
                    $bad = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/Rejected"; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/missing-capture.json" $bad
                    if ($bad.status -ne 'failed' -or $bad.failure -notmatch 'MissingCoverage.*Missing.Active.Target') { throw 'Missing target did not fail capture with its identity' }
                    $badSequence = Invoke-VerificationSession sequence $session @{ CameraPath = 'SEQ_TemporalAALab_MovingObject'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/missing-sequence.json" $badSequence
                    if ($badSequence.sequence.state -ne 'failed' -or $badSequence.sequence.failure -notmatch 'MissingCoverage') { throw 'Missing target did not fail sequence' }
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'report'; WatchClaims = @('boundary'); Targets = '[{"resource":"Missing.Active.Target"}]' }
                    $reported = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/Reported"; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/reported-capture.json" $reported
                    if ($reported.status -ne 'completed' -or -not $reported.watch.complete -or $reported.watch.boundary -ne 'Incomplete') { throw 'Report mode lost its incomplete claim' }
                    $null = Invoke-VerificationSession watch $session @{ WatchOff = $true }
                    $recovered = Invoke-VerificationSession capture $session @{ OutputDirectory = "$directory/Recovered"; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/recovered-capture.json" $recovered
                    if ($recovered.status -ne 'completed') { throw 'Process did not remain inspectable after fail mode detection' }
                } finally {
                    if ($null -ne $start) {
                        $null = Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit
                    }
                }
                $log = Get-Content -Raw $start.outputLog
                if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw 'Native validation errors in coverage session' }
                $cases += @{ configuration = $config; backend = $backend; counters = $counters; passed = $true }
            }
        }
    }
    $result = @{ ok = $true; cases = $cases; directory = $output }
    Write-VerificationJson "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
} catch {
    $result = @{ ok = $false; cases = $cases; directory = $output; error = $_.Exception.Message }
    Write-VerificationJson "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
    exit 1
}
