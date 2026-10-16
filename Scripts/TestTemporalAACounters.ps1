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
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/TestTemporalAACounters.ps1 -Configuration Debug -Rhi dx12
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
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent $PSScriptRoot
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/TemporalAACounters' }
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$sessionScript = Join-Path $PSScriptRoot 'GGLabSession.ps1'
$comparisonScript = Join-Path $PSScriptRoot 'CompareCaptures.ps1'
$utf8 = New-Object Text.UTF8Encoding($false)
$results = @()

function Save-Receipt([string]$Path, $Receipt) {
    [IO.File]::WriteAllText($Path, ($Receipt | ConvertTo-Json -Depth 24), $utf8)
}

function Invoke-Session([string]$Verb, [string]$SessionId, [hashtable]$Options = @{}) {
    # A child shell contains GGLabSession's exit calls and preserves typed arrays/switches.
    $optionsJson = ($Options | ConvertTo-Json -Compress -Depth 12).Replace("'", "''")
    $quotedScript = $sessionScript.Replace("'", "''")
    $command = "`$ProgressPreference = 'SilentlyContinue'; `$options = @{}; `$data = '$optionsJson' | ConvertFrom-Json; " +
        "foreach (`$property in `$data.PSObject.Properties) { `$options[`$property.Name] = `$property.Value }; " +
        "& '$quotedScript' '$Verb' -Session '$SessionId' @options"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand $encoded
    if ($LASTEXITCODE -ne 0) { throw "Session $SessionId $Verb failed: $text" }
    $receipt = ($text -join "`n") | ConvertFrom-Json
    if (-not $receipt.ok) { throw "Session $SessionId $Verb failed: $text" }
    return $receipt
}

function Wait-CleanWatch([string]$SessionId, [bool]$Counters, [long]$MinimumSerial,
    [string[]]$ExpectedClaims = @('Passed', 'Passed', 'Passed', 'Passed')) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $status = Invoke-Session status $SessionId
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
        if ($clean) { return $status }
        if ((Get-Date) -ge $deadline) {
            throw "Session $SessionId watch did not complete: $($report | ConvertTo-Json -Compress -Depth 12)"
        }
        Start-Sleep -Milliseconds 50
    } while ($true)
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
                    $start = Invoke-Session start $sessionId $options
                    Save-Receipt (Join-Path $armDirectory 'start.json') $start
                    # Cold Debug PSO validation can exceed status's fixed 30-second
                    # response timeout. A readiness-gated capture uses our bounded timeout.
                    $warmup = Invoke-Session capture $sessionId @{ SettleFrames = 2; TimeoutSeconds = $TimeoutSeconds;
                        OutputDirectory = (Join-Path $armDirectory 'Startup'); Label = 'startup' }
                    Save-Receipt (Join-Path $armDirectory 'startup.json') $warmup
                    if ($warmup.status -ne 'completed') { throw "Session $sessionId startup capture failed" }
                    $claims = if ($counters) { @('boundary', 'production') } else { @('boundary') }
                    $null = Invoke-Session watch $sessionId @{ WatchClaims = $claims }
                    $null = Wait-CleanWatch $sessionId $counters 1
                    foreach ($preset in @('native', 'quality')) {
                        $exports = @(
                            @{ frame = 1; targets = @(@{ resource = 'DisplayView.SceneColor' }, @{ resource = 'TAA.ResolvedSceneColor' }); graph = $true },
                            @{ frame = 179; targets = @(@{ resource = 'DisplayView.SceneColor' }, @{ resource = 'TAA.ResolvedSceneColor' }); graph = $true }
                        ) | ConvertTo-Json -Compress -Depth 8
                        $sequence = Invoke-Session sequence $sessionId @{ CameraPath = 'SEQ_TemporalAALab_MovingObject';
                            CaptureFrames = '0,1,179'; TemporalAA = "enabled=true,resolutionPreset=$preset";
                            Exports = $exports; OutputDirectory = $armDirectory; Label = "taa-$preset"; TimeoutSeconds = $TimeoutSeconds }
                        Save-Receipt (Join-Path $armDirectory "$preset-sequence.json") $sequence
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
                        Save-Receipt (Join-Path $armDirectory "$preset-watch.json") $status
                    }
                    if ($counters) {
                        foreach ($temporalAO in @($true, $false)) {
                            $mode = if ($temporalAO) { 'temporal' } else { 'spatial' }
                            $transition = Invoke-Session sequence $sessionId @{ CameraPath = 'SEQ_TemporalAALab_LightChange';
                                CaptureFrames = '179'; TemporalAA = 'enabled=false'; GTAO = "temporal=$($temporalAO.ToString().ToLowerInvariant())";
                                NoWait = $true; OutputDirectory = (Join-Path $armDirectory 'Transitions'); Label = "taa-off-$mode" }
                            Save-Receipt (Join-Path $armDirectory "taa-off-$mode-sequence.json") $transition
                            $claims = if ($temporalAO) { @('Passed', 'Passed', 'NotApplicable', 'Passed') }
                                else { @('NotApplicable', 'Passed', 'NotApplicable', 'Passed') }
                            $status = Wait-CleanWatch $sessionId $true 1 $claims
                            Save-Receipt (Join-Path $armDirectory "taa-off-$mode-watch.json") $status
                            $null = Invoke-Session sequence-cancel $sessionId
                            $status = Wait-CleanWatch $sessionId $true 1
                            Save-Receipt (Join-Path $armDirectory "taa-restored-$mode-watch.json") $status
                        }
                    }
                } finally {
                    # A partially failed start can still own a live session; attempt cleanup too.
                    $stopText = & powershell -NoProfile -ExecutionPolicy Bypass -File $sessionScript stop -Session $sessionId
                    if ($LASTEXITCODE -ne 0) {
                        if ($null -ne $start) {
                            # Only force our own launch, never an unrelated/reused PID.
                            $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($start.pid)"
                            if ($null -ne $process -and $process.Name -eq 'GraphicsGadgetLab.exe' -and
                                $process.CommandLine -match [regex]::Escape("--session $sessionId")) {
                                Stop-Process -Id $start.pid -Force
                            }
                            throw "Session $sessionId could not be stopped: $stopText"
                        }
                    } else {
                        $stop = ($stopText -join "`n") | ConvertFrom-Json
                        Save-Receipt (Join-Path $armDirectory 'stop.json') $stop
                        if ($stop.exitCode -ne 0) { throw "Session $sessionId exited with $($stop.exitCode)" }
                    }
                }
                $errors = @(Select-String -Path $start.outputLog -Pattern '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed')
                if ($errors.Count -ne 0) { throw "Session $sessionId logged native errors: $($errors -join "`n")" }
            }
            $off = Join-Path $caseDirectory 'off'
            $on = Join-Path $caseDirectory 'on'
            $comparisonText = & powershell -NoProfile -ExecutionPolicy Bypass -File $comparisonScript -Reference $off -Candidate $on -Threshold 0 -MaxMeanError 0 -MaxDifferingPercent 0
            $comparison = ($comparisonText -join "`n") | ConvertFrom-Json
            Save-Receipt (Join-Path $caseDirectory 'images.json') $comparison
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
    Save-Receipt (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 8))
    if (-not $ok) { exit 1 }
} catch {
    $result = @{ ok = $false; directory = $output; error = $_.Exception.Message; cases = $results }
    Save-Receipt (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 8))
    exit 1
}
