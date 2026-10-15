<#
.SYNOPSIS
Checks that non-finite watch catches values injected by the RenderGraph Compute Lab.

.DESCRIPTION
Uses the Debug-only render_graph_compute.debug.nonfinite.* Lab parameters through
startup assignments, so it runs Debug builds only. Every case starts a hidden,
counter-capable session on each requested backend:

- control: no injection; the production claim passes and the graph has no probe.
- boundary-*: NaN, +Inf and -Inf written into one probe texel and channel are
  reported at exactly that location and fail captures in fail mode; a finite
  overflow is reported as a finite extreme without failing.
- sanitized-nan: a register-only NaN replaced by a sanitizing write leaves the PNG
  byte-identical to the control while the production claim fails at its site.
- sanitized-negative / sanitized-overflow: finite clamps at a site whose negative
  policy is expected and whose FP16-maximum policy is error.

Captures use next-frame timing: this Lab renders no IBL, so its after-ready IBL
gate never opens. Animation speed 0 keeps every frame identical.
#>
[CmdletBinding()]
param(
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent $PSScriptRoot
$sessionScript = Join-Path $PSScriptRoot 'GGLabSession.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteInjection' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$cases = @()

$labId = 'gglab.lab.render_graph_compute'
$prefix = 'render_graph_compute.debug.nonfinite'
$site = 'pass-render-graph-compute-smoke.psmain.sanitize-hdrchannel'
$program = 'gglab.shader.render-graph-compute'
$probe = 'RenderGraphCompute.NonFiniteProbe'
$probeTargets = '[{"resource":"' + $probe + '"}]'
# Inside the 320x180 display and the 512x512 probe.
$x = 137; $y = 91; $channel = 2

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
function Wait-Report([string]$Session, [scriptblock]$Done, [string]$What) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $status = Call status $Session
        if (& $Done $status.watch.report $status) { return $status }
        if ((Get-Date) -ge $deadline) { throw "Timed out waiting for $What`: $($status.watch | ConvertTo-Json -Compress -Depth 12)" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Assert([bool]$Condition, [string]$Message, $Evidence) {
    if (-not $Condition) { throw "$Message`: $($Evidence | ConvertTo-Json -Compress -Depth 12)" }
}
# Hash decoded pixels; PNG files also carry per-capture metadata.
function Get-PixelHash([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $bitmap = [Drawing.Bitmap]::new($Path)
    try {
        $rect = [Drawing.Rectangle]::new(0, 0, $bitmap.Width, $bitmap.Height)
        $data = $bitmap.LockBits($rect, [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            $bytes = New-Object byte[] ($data.Stride * $data.Height)
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        } finally { $bitmap.UnlockBits($data) }
    } finally { $bitmap.Dispose() }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($bytes)) } finally { $sha.Dispose() }
}
function Find-Site($Status) {
    @($Status.watch.sites | Where-Object { $_.site -eq $script:site -and $_.program -eq $script:program })
}
function Invoke-Case([string]$Backend, [string]$Name, [string[]]$Parameters, [scriptblock]$Body) {
    $directory = Join-Path $output "$Backend-$Name"
    New-Item -ItemType Directory -Path $directory | Out-Null
    $session = "inject-$runId-$Backend-$Name"
    $start = $null
    try {
        $assignments = @("render_graph_compute.animation.speed=0") + $Parameters
        $start = Call start $session @{ Configuration = 'Debug'; Rhi = $Backend; Lab = $labId; NoDevTools = $true;
            WindowSize = '320x180'; StateRoot = "$output/$Backend-state"; WatchNonFinite = $true;
            LabParameters = $assignments; StartTimeoutSeconds = $TimeoutSeconds }
        Save "$directory/start.json" $start
        & $Body $session $directory
    } finally {
        if ($null -ne $start) {
            $stop = Call stop $session
            Save "$directory/stop.json" $stop
            if ($stop.exitCode -ne 0) { throw "Session exited with $($stop.exitCode)" }
        }
    }
    $log = Get-Content -Raw $start.outputLog
    if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native errors in $Backend $Name" }
    $script:cases += @{ backend = $Backend; case = $Name; passed = $true }
}
function Injection([string]$Mode, [string]$Value) {
    @("$prefix.mode=$Mode", "$prefix.value=$Value", "$prefix.x=$x", "$prefix.y=$y", "$prefix.channel=$channel")
}

try {
    foreach ($backend in $Rhi) {
        Invoke-Case $backend 'control' @() {
            param($session, $directory)
            $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
            $null = Wait-Report $session { param($r) $r.production -eq 'Passed' } 'clean production evidence'
            $image = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'control'; TimeoutSeconds = $TimeoutSeconds }
            Save "$directory/image.json" $image
            Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Passed' -and $image.watch.complete) 'Control capture did not pass' $image
            $script:controlHash = Get-PixelHash $image.image
            $graph = Call graph $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'control'; TimeoutSeconds = $TimeoutSeconds }
            Assert ($graph.status -eq 'completed') 'Control graph export failed' $graph
            Assert (-not (Select-String -LiteralPath $graph.graph -SimpleMatch 'NonFiniteProbe' -Quiet)) 'Probe work exists without injection' $graph
        }
        foreach ($value in @(@{ Name = 'NaN'; Field = 'nan' }, @{ Name = 'PositiveInfinity'; Field = 'positiveInf' }, @{ Name = 'NegativeInfinity'; Field = 'negativeInf' })) {
            Invoke-Case $backend "boundary-$($value.Name)" (Injection 'Boundary' $value.Name) {
                param($session, $directory)
                $null = Call watch $session @{ WatchMode = 'report'; Targets = $probeTargets }
                $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly -and $r.production -eq 'Passed' } 'a boundary anomaly'
                Save "$directory/report.json" $status
                $anomaly = $status.watch.report.firstAnomaly
                $target = $anomaly.target
                Assert ($anomaly.reason -eq 'NonFiniteDetected' -and $target.address.resource -eq $probe) 'Boundary anomaly has the wrong reason or target' $anomaly
                Assert ($target.location.x -eq $x -and $target.location.y -eq $y -and $target.location.slice -eq 0 -and $target.location.channel -eq $channel) 'Boundary anomaly is not at the injected location' $target
                Assert ($target.($value.Field) -eq 1 -and ($target.nan + $target.positiveInf + $target.negativeInf) -eq 1) 'Boundary anomaly has wrong counts' $target
                Assert ($target.checked -eq 512 * 512 * 4) 'Boundary scan did not check the whole probe' $target
                $null = Call watch $session @{ WatchMode = 'fail'; Targets = $probeTargets }
                $failed = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
                Save "$directory/fail-capture.json" $failed
                Assert ($failed.status -eq 'failed' -and $failed.failure -match 'NonFiniteDetected' -and $failed.failure -match [regex]::Escape($probe)) 'Fail mode did not fail the capture at the probe' $failed
            }
        }

        Invoke-Case $backend 'boundary-Overflow' (Injection 'Boundary' 'Overflow') {
            param($session, $directory)
            $null = Call watch $session @{ WatchMode = 'fail'; Targets = $probeTargets }
            $status = Wait-Report $session { param($r) $r.boundary -eq 'Passed' -and $r.production -eq 'Passed' } 'a clean finite boundary scan'
            Save "$directory/report.json" $status
            $target = @($status.watch.report.targets)[0]
            Assert ($target.maximum -eq 131072 -and $null -eq $status.watch.report.firstAnomaly) 'Finite overflow was not reported as the finite maximum' $target
        }

        Invoke-Case $backend 'sanitized-NaN' (Injection 'Sanitized' 'NaN') {
            param($session, $directory)
            $null = Call watch $session @{ WatchMode = 'report'; WatchClaims = @('production') }
            $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly } 'a production anomaly'
            Save "$directory/report.json" $status
            $anomaly = $status.watch.report.firstAnomaly
            Assert ($anomaly.reason -eq 'NonFiniteReplaced' -and $anomaly.site -eq $site -and $anomaly.program -eq $program) 'Production anomaly has the wrong site' $anomaly
            $counts = @(Find-Site $status)
            Assert ($counts.Count -eq 1 -and $counts[0].nonFiniteReplacements -eq 1 -and $counts[0].claim -eq 'Failed') 'Site counters do not show one replacement' $status.watch
            $image = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'reported'; TimeoutSeconds = $TimeoutSeconds }
            Save "$directory/image.json" $image
            Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Failed' -and $image.watch.reason -eq 'NonFiniteReplaced') 'Report-mode capture lost the production failure' $image
            Assert ((Get-PixelHash $image.image) -eq $script:controlHash) 'The sanitized NaN changed the PNG pixels' $image
            $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
            $failed = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
            Save "$directory/fail-capture.json" $failed
            Assert ($failed.status -eq 'failed' -and $failed.failure -match 'NonFiniteReplaced' -and $failed.failure -match [regex]::Escape($site)) 'Fail mode did not fail at the sanitizing site' $failed
        }

        Invoke-Case $backend 'sanitized-Negative' (Injection 'Sanitized' 'Negative') {
            param($session, $directory)
            $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
            $status = Wait-Report $session { param($r, $s) $r.production -eq 'Passed' -and @(Find-Site $s).Count -eq 1 } 'an expected clamp'
            Save "$directory/report.json" $status
            $counts = @(Find-Site $status)[0]
            Assert ($counts.negativeClamps -eq 1 -and $counts.mostNegative -eq -4 -and $counts.claim -eq 'Passed') 'Expected negative clamp was not counted as passing' $counts
            $image = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'expected'; TimeoutSeconds = $TimeoutSeconds }
            Save "$directory/image.json" $image
            Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Passed') 'Expected clamp failed the capture' $image
            Assert ((Get-PixelHash $image.image) -eq $script:controlHash) 'The clamped negative value changed the PNG pixels' $image
        }

        Invoke-Case $backend 'sanitized-Overflow' (Injection 'Sanitized' 'Overflow') {
            param($session, $directory)
            $null = Call watch $session @{ WatchMode = 'report'; WatchClaims = @('production') }
            $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly } 'an error clamp'
            Save "$directory/report.json" $status
            Assert ($status.watch.report.firstAnomaly.reason -eq 'ClampPolicyExceeded' -and $status.watch.report.firstAnomaly.site -eq $site) 'Overflow did not exceed the error policy' $status.watch.report
            $counts = @(Find-Site $status)[0]
            Assert ($counts.fp16MaximumClamps -eq 1 -and $counts.maximum -eq 131072 -and $counts.claim -eq 'Failed') 'FP16 clamp counters are wrong' $counts
            $null = Call watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
            $failed = Call capture $session @{ Timing = 'next-frame'; OutputDirectory = $directory; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
            Save "$directory/fail-capture.json" $failed
            Assert ($failed.status -eq 'failed' -and $failed.failure -match 'ClampPolicyExceeded' -and $failed.failure -match [regex]::Escape($site)) 'Fail mode did not fail on the error clamp' $failed
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
