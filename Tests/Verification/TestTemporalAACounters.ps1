<#
.SYNOPSIS
Checks production TAA with counter instrumentation off and on using hidden GPU sessions.

.DESCRIPTION
Runs the actual TAA depth-copy and resolve dispatches, including their different
physical binding layouts with counters enabled. Native and Quality replays cover
cold and accumulated history. Counter-on transitions also cover depth-only opaque
and alpha-tested draws with TAA and temporal GTAO disabled. Requires built WinApp
executables and local GPU support for the selected backends; this is not a
headless or performance test.

Checks boundary/production reports, native error logs and process exits, then
requires decoded PNGs and same-frame NPY exports to match exactly off/on within
each backend. Watch evidence is latest-completed-frame plus first observed
anomaly, not complete sequence-range acceptance. All started sessions are stopped.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/TestTemporalAACounters.ps1 -Configuration Debug -Rhi dx12
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')]
    [string[]]$Configuration = @('Debug', 'Release'),
    [ValidateSet('dx12', 'vulkan')]
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [ValidatePattern('^\d+[xX]\d+$')]
    [string]$WindowSize = '320x180',
    [string]$OutputDirectory,
    [ValidateRange(1, 1800)]
    [int]$TimeoutSeconds = 120,
    [string]$RunDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/TemporalAACounters' }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$comparisonScript = Join-Path $root 'Scripts/Verification/CompareCaptures.ps1'
$results = @()

function Wait-CleanWatch([string]$SessionId, [bool]$Counters, [long]$MinimumSerial,
    [string[]]$ExpectedClaims = @('Passed', 'Passed', 'Passed', 'Passed')) {
    $poll = { Invoke-VerificationSession status $SessionId }.GetNewClosure()
    $until = {
        param($status)
        $report = $status.watch.report
        if ($null -ne $report.firstAnomaly) {
            throw "Session $SessionId detected an anomaly: $($report.firstAnomaly | ConvertTo-Json -Compress -Depth 12)"
        }
        if ($status.watch.instrumentation -ne $(if ($Counters) { 'available' } else { 'unavailable' })) {
            throw "Session $SessionId has the wrong instrumentation capability"
        }
        $clean = $status.frame.ready -and $report.boundary -eq 'Passed' -and
            $report.boundaryFrameSerial -ge $MinimumSerial -and @($report.targets | Where-Object { $_.watchTier -eq 1 }).Count -eq 4
        $frameTargets = @($report.targets | Where-Object { $_.watchTier -eq 1 })
        for ($index = 0; $clean -and $index -lt 4; ++$index) {
            $clean = $frameTargets[$index].claim -eq $ExpectedClaims[$index]
        }
        if ($Counters) {
            $clean = $clean -and $report.production -eq 'Passed' -and $report.productionFrameSerial -ge $MinimumSerial
        }
        return $clean
    }.GetNewClosure()
    $timeout = {
        param($status)
        throw "Session $SessionId watch did not complete: $($status.watch.report | ConvertTo-Json -Compress -Depth 12)"
    }.GetNewClosure()
    Wait-VerificationCondition -Poll $poll -Until $until -TimeoutSeconds $TimeoutSeconds `
        -IntervalMilliseconds 50 -OnTimeout $timeout
}

try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $caseDirectory = Join-Path $output "$config-$backend"
            New-Item -ItemType Directory -Path $caseDirectory | Out-Null
            foreach ($counters in @($false, $true)) {
                $arm = if ($counters) { 'on' } else { 'off' }
                $armDirectory = Join-Path $caseDirectory $arm
                New-Item -ItemType Directory -Path $armDirectory | Out-Null
                $sessionId = "taa-counters-$runId-$config-$backend-$arm"
                $start = $null
                try {
                    $options = @{ Configuration = $config; Rhi = $backend; Lab = 'gglab.lab.temporal_aa';
                        NoDevTools = $true; WindowSize = $WindowSize; StartTimeoutSeconds = $TimeoutSeconds }
                    if ($counters) { $options.WatchNonFinite = $true }
                    $start = Invoke-VerificationSession start $sessionId $options
                    Write-VerificationJson -Depth 24 (Join-Path $armDirectory 'start.json') $start
                    # Cold Debug PSO validation can exceed status's fixed 30-second
                    # response timeout. A readiness-gated capture uses our bounded timeout.
                    $warmup = Invoke-VerificationSession capture $sessionId @{ SettleFrames = 2; TimeoutSeconds = $TimeoutSeconds;
                        OutputDirectory = (Join-Path $armDirectory 'Startup'); Label = 'startup' }
                    Write-VerificationJson -Depth 24 (Join-Path $armDirectory 'startup.json') $warmup
                    if ($warmup.status -ne 'completed') { throw "Session $sessionId startup capture failed" }
                    $claims = if ($counters) { @('boundary', 'production') } else { @('boundary') }
                    $null = Invoke-VerificationSession watch $sessionId @{ WatchClaims = $claims }
                    $null = Wait-CleanWatch $sessionId $counters 1
                    foreach ($preset in @('native', 'quality')) {
                        $exports = @(
                            @{ frame = 1; targets = @(@{ resource = 'DisplayView.SceneColor' }, @{ resource = 'TAA.ResolvedSceneColor' }); graph = $true },
                            @{ frame = 179; targets = @(@{ resource = 'DisplayView.SceneColor' }, @{ resource = 'TAA.ResolvedSceneColor' }); graph = $true }
                        ) | ConvertTo-Json -Compress -Depth 8
                        $sequence = Invoke-VerificationSession sequence $sessionId @{ CameraPath = 'SEQ_TemporalAALab_MovingObject';
                            CaptureFrames = '0,1,179'; TemporalAA = "enabled=true,resolutionPreset=$preset";
                            Exports = $exports; OutputDirectory = $armDirectory; Label = "taa-$preset"; TimeoutSeconds = $TimeoutSeconds }
                        Write-VerificationJson -Depth 24 (Join-Path $armDirectory "$preset-sequence.json") $sequence
                        if ($sequence.sequence.state -ne 'completed' -or $sequence.sequence.submittedFrames -ne 180 -or
                            $sequence.sequence.completedCaptures -ne 5) {
                            throw "Session $sessionId $preset sequence did not complete"
                        }
                        $captures = @(Get-ChildItem $armDirectory -Filter "taa-$preset-*.png")
                        if ($captures.Count -ne 3) { throw "Session $sessionId $preset is missing captures" }
                        $minimumSerial = 0L
                        foreach ($capture in $captures) {
                            $metadata = Get-Content -Raw ([IO.Path]::ChangeExtension($capture.FullName, '.json')) | ConvertFrom-Json
                            if ($metadata.temporal.status -ne 'active' -or $metadata.temporal.settings.resolutionPreset -ne $preset -or
                                ($preset -eq 'quality' -and $metadata.temporal.renderScale -ge 1)) {
                                throw "Session $sessionId did not render the requested $preset TAA domain"
                            }
                            $minimumSerial = [Math]::Max($minimumSerial, [long]$metadata.frame.serial)
                        }
                        $status = Wait-CleanWatch $sessionId $counters $minimumSerial
                        Write-VerificationJson -Depth 24 (Join-Path $armDirectory "$preset-watch.json") $status
                    }
                    if ($counters) {
                        foreach ($temporalAO in @($true, $false)) {
                            $mode = if ($temporalAO) { 'temporal' } else { 'spatial' }
                            $transition = Invoke-VerificationSession sequence $sessionId @{ CameraPath = 'SEQ_TemporalAALab_LightChange';
                                CaptureFrames = '179'; TemporalAA = 'enabled=false'; GTAO = "temporal=$($temporalAO.ToString().ToLowerInvariant())";
                                NoWait = $true; OutputDirectory = (Join-Path $armDirectory 'Transitions'); Label = "taa-off-$mode" }
                            Write-VerificationJson -Depth 24 (Join-Path $armDirectory "taa-off-$mode-sequence.json") $transition
                            $claims = if ($temporalAO) { @('Passed', 'Passed', 'NotApplicable', 'Passed') }
                                else { @('NotApplicable', 'Passed', 'NotApplicable', 'Passed') }
                            $status = Wait-CleanWatch $sessionId $true 1 $claims
                            Write-VerificationJson -Depth 24 (Join-Path $armDirectory "taa-off-$mode-watch.json") $status
                            $null = Invoke-VerificationSession sequence-cancel $sessionId
                            $status = Wait-CleanWatch $sessionId $true 1
                            Write-VerificationJson -Depth 24 (Join-Path $armDirectory "taa-restored-$mode-watch.json") $status
                        }
                    }
                } finally {
                    $null = Stop-VerificationSession $sessionId -StartedSession $start `
                        -ReceiptPath (Join-Path $armDirectory 'stop.json') -Depth 24 `
                        -RequireCleanExit -AllowMissing -ForceOwnedProcess
                }
                $errors = @(Select-String -Path $start.outputLog -Pattern '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed')
                if ($errors.Count -ne 0) { throw "Session $sessionId logged native errors: $($errors -join "`n")" }
            }
            $off = Join-Path $caseDirectory 'off'
            $on = Join-Path $caseDirectory 'on'
            $comparisonText = & powershell -NoProfile -ExecutionPolicy Bypass -File $comparisonScript -Reference $off -Candidate $on -Threshold 0 -MaxMeanError 0 -MaxDifferingPercent 0
            $comparison = ($comparisonText -join "`n") | ConvertFrom-Json
            Write-VerificationJson -Depth 24 (Join-Path $caseDirectory 'images.json') $comparison
            if ($comparison.compared -ne 6) { throw "Incomplete image comparison for $config/$backend" }
            $offGroups = @(Get-ChildItem $off -Filter '*.resource-export.json')
            $onGroups = @(Get-ChildItem $on -Filter '*.resource-export.json')
            if ($offGroups.Count -ne 4 -or $onGroups.Count -ne 4) { throw "Missing export groups for $config/$backend" }
            $arrays = 0
            $arrayDifferences = @()
            foreach ($referenceFile in $offGroups) {
                $reference = Get-Content -Raw $referenceFile.FullName | ConvertFrom-Json
                $matches = @($onGroups | Where-Object {
                    $candidate = Get-Content -Raw $_.FullName | ConvertFrom-Json
                    $candidate.frame.sequence.frame -eq $reference.frame.sequence.frame -and
                        $candidate.frame.temporal.settings.resolutionPreset -eq $reference.frame.temporal.settings.resolutionPreset
                })
                if ($matches.Count -ne 1) { throw "Ambiguous export pair for $($referenceFile.Name)" }
                $candidate = Get-Content -Raw $matches[0].FullName | ConvertFrom-Json
                if ($reference.targets.Count -ne 2 -or $candidate.targets.Count -ne 2) { throw 'Missing TAA export targets' }
                for ($index = 0; $index -lt 2; ++$index) {
                    $a = $reference.targets[$index]
                    $b = $candidate.targets[$index]
                    if ($a.address.resource -ne $b.address.resource) { throw 'Mismatched TAA export targets' }
                    if ((Get-FileHash (Join-Path $off $a.array) -Algorithm SHA256).Hash -ne
                        (Get-FileHash (Join-Path $on $b.array) -Algorithm SHA256).Hash) {
                        $arrayDifferences += @{ resource = $a.address.resource; sequenceFrame = $reference.frame.sequence.frame;
                            preset = $reference.frame.temporal.settings.resolutionPreset; reference = $a.array; candidate = $b.array }
                    }
                    ++$arrays
                }
            }
            # Retain strict failures but finish the backend/configuration matrix so
            # transport/production correctness is not confused with bit equivalence.
            $results += @{ configuration = $config; backend = $backend; images = $comparison.compared; arrays = $arrays;
                sessionChecksPassed = $true;
                equivalencePassed = $comparison.ok -and $arrayDifferences.Count -eq 0; imageFailures = $comparison.failed;
                arrayDifferences = $arrayDifferences }
        }
    }
    $ok = @($results | Where-Object { -not $_.equivalencePassed }).Count -eq 0
    $result = @{ ok = $ok; directory = $output; cases = $results }
    Write-VerificationJson -Depth 24 (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 8))
    if (-not $ok) { exit 1 }
} catch {
    $result = @{ ok = $false; directory = $output; error = $_.Exception.Message; cases = $results }
    Write-VerificationJson -Depth 24 (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 8))
    exit 1
}
