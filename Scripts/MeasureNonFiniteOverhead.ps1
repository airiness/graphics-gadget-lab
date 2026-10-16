<#
.SYNOPSIS
Measures the GPU cost of non-finite instrumentation and watch scans.

.DESCRIPTION
Plays the same Coastal Atrium camera paths with -GpuTiming in three hidden Release
sessions per backend:

- baseline: no counter variants, watch off;
- counters: --watch-nonfinite with watch off (counter-variant shaders plus the
  per-frame production clear and readback);
- watch: counters plus report-mode watch of semantic defaults (per-frame Tier A scans
  and Tier B generation scans).

Frame time is the mean of every timed sequence render. Observer time per frame sums
mean x count over 'Observation.*' scopes, divided by timed renders; pass time sums the
remaining scopes the same way. Writes result.json and summary.md under a new run
directory. Compare arms only within one backend and run; GPU timing has run-to-run
noise, so repeat runs before treating a small difference as real.
#>
[CmdletBinding()]
param(
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Release',
    [ValidatePattern('^\d+[xX]\d+$')][string]$WindowSize = '1920x1080',
    [string[]]$CameraPaths = @('SEQ_StaticRailings', 'SEQ_PanStopRailings', 'SEQ_LateralPanRailings', 'SEQ_StaticGlassTerrace',
        'SEQ_PanGlassTerrace', 'SEQ_DollyDoorway', 'SEQ_OrbitLounge', 'SEQ_HorizonPan', 'SEQ_CutCourtyardToStairs'),
    [int]$ReadyTimeoutSeconds = 180,
    [int]$TimeoutSeconds = 300,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent $PSScriptRoot
$sessionScript = Join-Path $PSScriptRoot 'GGLabSession.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteOverhead' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
# powershell -File passes a comma-separated list as one string.
$CameraPaths = @($CameraPaths | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

function Save([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), $utf8)
}
function Call([string]$Verb, [string]$Session, [hashtable]$Options = @{}) {
    $json = ($Options | ConvertTo-Json -Compress -Depth 12).Replace("'", "''")
    $script = $sessionScript.Replace("'", "''")
    $command = "`$ProgressPreference='SilentlyContinue'; `$p=@{}; `$data='$json'|ConvertFrom-Json; " +
        "foreach (`$v in `$data.PSObject.Properties) {`$p[`$v.Name]=`$v.Value}; & '$script' '$Verb' -Session '$Session' @p"
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command)))
    if ($LASTEXITCODE) { throw "Session $Session $Verb failed: $text" }
    return ($text -join "`n") | ConvertFrom-Json
}

$arms = @(
    @{ Name = 'baseline'; Counters = $false; Watch = $false },
    @{ Name = 'counters'; Counters = $true; Watch = $false },
    @{ Name = 'watch'; Counters = $true; Watch = $true }
)
$rows = @()
try {
    foreach ($backend in $Rhi) {
        foreach ($arm in $arms) {
            $name = "$backend-$($arm.Name)"
            $directory = Join-Path $output $name
            New-Item -ItemType Directory -Path $directory | Out-Null
            $session = "overhead-$runId-$name"
            $start = $null
            try {
                $start = Call start $session @{ Configuration = $Configuration; Rhi = $backend; Demo = 'atrium'; NoDevTools = $true;
                    WindowSize = $WindowSize; StateRoot = "$output/$backend-state"; WatchNonFinite = $arm.Counters;
                    StartTimeoutSeconds = $TimeoutSeconds }
                $deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
                do {
                    Start-Sleep -Milliseconds 500
                    $status = Call status $session
                } while ((Get-Date) -lt $deadline -and -not ($status.frame -and $status.frame.ready))
                if (-not ($status.frame -and $status.frame.ready)) { throw "$name never became ready" }
                if ($arm.Watch) { $null = Call watch $session @{ WatchMode = 'report' } }
                $frames = 0.0; $frameMs = 0.0; $observerMs = 0.0; $passMs = 0.0; $medians = @(); $observers = @{}
                foreach ($path in $CameraPaths) {
                    $sequence = Call sequence $session @{ CameraPath = $path; GpuTiming = $true; TimeoutSeconds = $TimeoutSeconds }
                    Save "$directory/sequence-$path.json" $sequence
                    if ($sequence.sequence.state -ne 'completed') { throw "$name $path ended $($sequence.sequence.state)" }
                    $timing = $sequence.sequence.gpuTiming
                    $frames += $timing.frames
                    $frameMs += $timing.frame.meanMs * $timing.frames
                    $medians += $timing.frame.medianMs
                    foreach ($scope in $timing.scopes) {
                        $total = $scope.meanMs * $scope.count
                        if ($scope.name.StartsWith('Observation.')) {
                            $observerMs += $total
                            # Scan pass names carry a per-frame index; group them by kind.
                            $kind = ($scope.name -replace '\.\d+', '.#')
                            $observers[$kind] = $(if ($observers.ContainsKey($kind)) { $observers[$kind] } else { 0.0 }) + $total
                        } else { $passMs += $total }
                    }
                }
                $sortedMedians = @($medians | Sort-Object)
                $observerTable = [ordered]@{}
                foreach ($kind in @($observers.Keys | Sort-Object)) { $observerTable[$kind] = [math]::Round($observers[$kind] / $frames, 4) }
                $rows += [ordered]@{ backend = $backend; arm = $arm.Name; frames = $frames;
                    frameMeanMs = [math]::Round($frameMs / $frames, 4);
                    medianOfSequenceMediansMs = [math]::Round($sortedMedians[[int][math]::Floor(($sortedMedians.Count - 1) / 2)], 4);
                    passMsPerFrame = [math]::Round($passMs / $frames, 4); observerMsPerFrame = [math]::Round($observerMs / $frames, 4);
                    observers = $observerTable }
                [Console]::Error.WriteLine("measured $name frames=$frames")
            } finally {
                if ($null -ne $start) { $null = Call stop $session }
            }
        }
    }
} catch {
    Save "$output/result.json" @{ ok = $false; error = $_.Exception.Message; rows = $rows }
    [Console]::Out.WriteLine((@{ ok = $false; error = $_.Exception.Message; directory = $output } | ConvertTo-Json -Compress))
    exit 1
}

$lines = @("# Non-finite overhead $runId", '', "Configuration $Configuration, window $WindowSize, Coastal Atrium paths: $($CameraPaths -join ', ').", '',
    '| Backend | Arm | Timed renders | Frame mean ms | Delta vs baseline ms | Median of sequence medians ms | Pass ms/frame | Observer ms/frame |',
    '| --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($row in $rows) {
    $baseline = @($rows | Where-Object { $_.backend -eq $row.backend -and $_.arm -eq 'baseline' })[0]
    $delta = [math]::Round($row.frameMeanMs - $baseline.frameMeanMs, 4)
    $lines += "| $($row.backend) | $($row.arm) | $($row.frames) | $($row.frameMeanMs) | $delta | $($row.medianOfSequenceMediansMs) | $($row.passMsPerFrame) | $($row.observerMsPerFrame) |"
}
$lines += '', '## Observer scopes (ms per timed render)', ''
foreach ($row in $rows | Where-Object { $_.observers.Count -gt 0 }) {
    $lines += "- $($row.backend)/$($row.arm): " + (($row.observers.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }) -join '; ')
}
[IO.File]::WriteAllText((Join-Path $output 'summary.md'), ($lines -join "`r`n") + "`r`n", $utf8)
Save "$output/result.json" @{ ok = $true; directory = $output; rows = $rows }
[Console]::Out.WriteLine((@{ ok = $true; directory = $output } | ConvertTo-Json -Compress))
