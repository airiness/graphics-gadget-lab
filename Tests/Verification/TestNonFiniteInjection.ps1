<#
.SYNOPSIS
Checks that non-finite watch catches values injected at registered injection points.

.DESCRIPTION
Injection is a Debug/Develop watch request ('watch -Inject'), carried to the GPU with
the frame's production counters, so this script starts with --watch-nonfinite.
Each requested backend gets one hidden session per Lab; every case reconfigures watch.

RenderGraph Compute Lab (animation speed 0, so every frame is identical):

- control: no injection; production passes, the graph has no probe and evidence
  carries no injection.
- boundary-*: NaN, +Inf and -Inf injected at the probe point are reported at exactly
  that probe texel and channel, and fail captures in fail mode; a finite overflow is
  only a finite extreme.
- sanitized-NaN: a register-only NaN replaced by a sanitizing write leaves the PNG
  pixels equal to the control while production fails at the site.
- sanitized-Negative / sanitized-Overflow: finite clamps at a site whose negative
  policy is expected and whose FP16-maximum policy is error.

Captures in this Lab use next-frame timing: it renders no IBL, so its after-ready IBL
gate never opens.

Temporal AA Lab (object animation off): a NaN or infinity replaces one component of the
history read for one display pixel. The recovery branch replaces it, so only the
recovery site's counter can observe it: RGB reaches the color guard and alpha the
accumulation-state guard. Each TAA session starts with an after-ready capture, which
also waits out the Debug DX12 GPU-based-validation pipeline stall.

Every injected report and capture must carry the injection stamp.
#>
[CmdletBinding()]
param(
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [ValidateSet('render-graph-compute', 'temporal-aa')][string[]]$Lab = @('render-graph-compute', 'temporal-aa'),
    [string]$OutputDirectory,
    [int]$TimeoutSeconds = 180,
    [ValidateSet('Debug', 'Develop')][string]$Configuration = 'Debug',
    [string]$RunDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteInjection' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$cases = @()

$computeLabId = 'gglab.lab.render_graph_compute'
$computeProgram = 'gglab.shader.render-graph-compute'
$sanitizeSite = 'pass-render-graph-compute-smoke.psmain.sanitize-hdrchannel'
$probePoint = 'pass-render-graph-compute-smoke.csnonfiniteprobe.probe'
$previewPoint = 'pass-render-graph-compute-smoke.psmain.preview'
$probe = 'RenderGraphCompute.NonFiniteProbe'
$probeTargets = '[{"resource":"' + $probe + '"}]'
# Inside the 320x180 display and the 512x512 probe.
$x = 137; $y = 91; $component = 2

$taaLabId = 'gglab.lab.temporal_aa'
$taaProgram = 'gglab.shader.temporal-aa'
$taaPoint = 'pass-temporal-aa.csmain.history'
$taaX = 161; $taaY = 93
$taaColorSite = 'pass-temporal-aa.csmain.is-temporal-color-finite'
$taaAccumulationSite = 'pass-temporal-aa.csmain.is-temporal-history-accumulation-valid'
$taaInjections = @(
    @{ Value = 'NaN'; Component = 1; Site = $taaColorSite },
    @{ Value = 'NegativeInfinity'; Component = 0; Site = $taaColorSite },
    @{ Value = 'PositiveInfinity'; Component = 3; Site = $taaAccumulationSite }
)

function Wait-Report([string]$Session, [scriptblock]$Done, [string]$What) {
    $poll = { Invoke-VerificationSession status $Session }.GetNewClosure()
    $until = { param($status) & $Done $status.watch.report $status }.GetNewClosure()
    $timeout = {
        param($status)
        throw "Timed out waiting for $What`: $($status.watch | ConvertTo-Json -Compress -Depth 12)"
    }.GetNewClosure()
    Wait-VerificationCondition -Poll $poll -Until $until -TimeoutSeconds $TimeoutSeconds -OnTimeout $timeout
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
function Find-Site($Status, [string]$Site, [string]$Program) {
    @($Status.watch.sites | Where-Object { $_.site -eq $Site -and $_.program -eq $Program })
}
function Inject([string]$Point, [string]$Value, [int]$X, [int]$Y, [int]$Component) {
    (@{ point = $Point; value = $Value; x = $X; y = $Y; component = $Component } | ConvertTo-Json -Compress)
}
# The report, and a capture's coverage when present, must name the configured injection.
function Assert-Stamp($Report, $Coverage, [string]$Point, [string]$Value) {
    Assert ($null -ne $Report.injection -and $Report.injection.point -eq $Point -and $Report.injection.value -eq $Value -and
        $Report.injection.appliedCount -ge 1) 'The watch report does not carry the applied injection' $Report.injection
    if ($null -ne $Coverage) {
        Assert ($null -ne $Coverage.injection -and $Coverage.injection.point -eq $Point -and $Coverage.injection.value -eq $Value -and
            $Coverage.injection.appliedFrames -ge 1 -and $Coverage.injection.appliedFrames -le $Coverage.injection.injectedFrames) 'Capture coverage does not carry the injection stamp' $Coverage
    }
}
function Get-Coverage($Response) { if ($Response.PSObject.Properties['watch']) { $Response.watch } else { $null } }
function Invoke-Session([string]$Backend, [string]$Name, [string]$LabId, [string[]]$Assignments, [scriptblock]$Body) {
    $directory = Join-Path $output "$Backend-$Name"
    New-Item -ItemType Directory -Path $directory | Out-Null
    $session = "inject-$runId-$Backend-$Name"
    $start = $null
    try {
        $start = Invoke-VerificationSession start $session @{ Configuration = $Configuration; Rhi = $Backend; Lab = $LabId; NoDevTools = $true;
            WindowSize = '320x180'; StateRoot = "$output/$Backend-state"; WatchNonFinite = $true;
            LabParameters = $Assignments; StartTimeoutSeconds = $TimeoutSeconds }
        Write-VerificationJson "$directory/start.json" $start
        & $Body $session $directory
        # Disabling watch clears the injection and leaves the process usable.
        $null = Invoke-VerificationSession watch $session @{ WatchOff = $true }
        $off = Invoke-VerificationSession status $session
        Assert ($null -eq $off.watch.report -or $null -eq $off.watch.report.injection) 'Disabling watch kept the injection' $off.watch
        $recovered = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/recovered"; TimeoutSeconds = $TimeoutSeconds }
        Assert ($recovered.status -eq 'completed') 'The process did not recover after injection' $recovered
    } finally {
        if ($null -ne $start) {
            $null = Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit
        }
    }
    $log = Get-Content -Raw $start.outputLog
    if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native errors in $Backend $Name" }
}
function Add-Case([string]$Backend, [string]$Name) {
    $script:cases += @{ backend = $Backend; case = $Name; passed = $true }
}

try {
    foreach ($backend in $Rhi) {
        if ($Lab -contains 'render-graph-compute') {
            Invoke-Session $backend 'render-graph-compute' $computeLabId @('render_graph_compute.animation.speed=0') {
                param($session, $directory)
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
                $status = Wait-Report $session { param($r) $r.production -eq 'Passed' } 'clean production evidence'
                Assert ($null -eq $status.watch.report.injection -and $null -eq $status.watch.injection) 'Uninjected evidence carries an injection' $status.watch
                $image = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/control"; Label = 'control'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/control.json" $image
                Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Passed' -and $null -eq $image.watch.injection) 'Control capture did not pass' $image
                $script:controlHash = Get-PixelHash (Get-VerificationCaptureFile $image '.png')
                $graph = Invoke-VerificationSession graph $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/control"; Label = 'control'; TimeoutSeconds = $TimeoutSeconds }
                Assert ($graph.status -eq 'completed' -and -not (Select-String -LiteralPath (Get-VerificationCaptureFile $graph '.json') -SimpleMatch 'NonFiniteProbe' -Quiet)) 'Probe work exists without injection' $graph
                Add-Case $backend 'compute-control'

                foreach ($value in @(@{ Name = 'NaN'; Field = 'nan' }, @{ Name = 'PositiveInfinity'; Field = 'positiveInf' }, @{ Name = 'NegativeInfinity'; Field = 'negativeInf' })) {
                    $inject = Inject $probePoint $value.Name $x $y $component
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'report'; Targets = $probeTargets; Inject = $inject }
                    $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly -and $r.production -eq 'Passed' -and $null -ne $r.injection -and $r.injection.appliedCount -ge 1 } "a boundary $($value.Name)"
                    Write-VerificationJson "$directory/boundary-$($value.Name).json" $status
                    $anomaly = $status.watch.report.firstAnomaly
                    $target = $anomaly.target
                    Assert ($anomaly.reason -eq 'NonFiniteDetected' -and $target.address.resource -eq $probe) 'Boundary anomaly has the wrong reason or target' $anomaly
                    Assert ($target.location.x -eq $x -and $target.location.y -eq $y -and $target.location.slice -eq 0 -and $target.location.channel -eq $component) 'Boundary anomaly is not at the injected location' $target
                    Assert ($target.($value.Field) -eq 1 -and ($target.nan + $target.positiveInf + $target.negativeInf) -eq 1 -and $target.checked -eq 512 * 512 * 4) 'Boundary anomaly has wrong counts' $target
                    Assert ($status.watch.report.injection.appliedCount -eq 1) 'The probe point did not apply exactly once per frame' $status.watch.report.injection
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; Targets = $probeTargets; Inject = $inject }
                    $failed = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/boundary-$($value.Name)"; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/boundary-$($value.Name)-fail.json" $failed
                    Assert ($failed.status -eq 'failed' -and $failed.failure -match 'NonFiniteDetected' -and $failed.failure -match [regex]::Escape($probe) -and
                        $failed.failure -match 'under Debug injection') 'Fail mode did not fail at the probe under its injection' $failed
                    Add-Case $backend "compute-boundary-$($value.Name)"
                }

                $inject = Inject $probePoint 'Overflow' $x $y $component
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; Targets = $probeTargets; Inject = $inject }
                $status = Wait-Report $session { param($r) $r.boundary -eq 'Passed' -and $r.production -eq 'Passed' -and $null -ne $r.injection -and $r.injection.appliedCount -ge 1 } 'a finite boundary overflow'
                Write-VerificationJson "$directory/boundary-Overflow.json" $status
                Assert (@($status.watch.report.targets)[0].maximum -eq 131072 -and $null -eq $status.watch.report.firstAnomaly) 'Finite overflow was not only the finite maximum' $status.watch.report
                $image = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/boundary-Overflow"; Label = 'overflow'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/boundary-Overflow-image.json" $image
                Assert ($image.status -eq 'completed') 'A finite boundary overflow failed the capture' $image
                Assert-Stamp $status.watch.report (Get-Coverage $image) $probePoint 'Overflow'
                Add-Case $backend 'compute-boundary-Overflow'

                $inject = Inject $previewPoint 'NaN' $x $y $component
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'report'; WatchClaims = @('production'); Inject = $inject }
                $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly } 'a sanitized NaN'
                Write-VerificationJson "$directory/sanitized-NaN.json" $status
                $anomaly = $status.watch.report.firstAnomaly
                Assert ($anomaly.reason -eq 'NonFiniteReplaced' -and $anomaly.site -eq $sanitizeSite -and $anomaly.program -eq $computeProgram) 'Production anomaly has the wrong site' $anomaly
                $counts = @(Find-Site $status $sanitizeSite $computeProgram)
                Assert ($counts.Count -eq 1 -and $counts[0].nonFiniteReplacements -eq 1 -and $counts[0].claim -eq 'Failed') 'Site counters do not show one replacement' $status.watch
                $image = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/sanitized-NaN"; Label = 'reported'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/sanitized-NaN-image.json" $image
                Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Failed' -and $image.watch.reason -eq 'NonFiniteReplaced') 'Report-mode capture lost the production failure' $image
                Assert ((Get-PixelHash (Get-VerificationCaptureFile $image '.png')) -eq $script:controlHash) 'The sanitized NaN changed the PNG pixels' $image
                Assert-Stamp $status.watch.report (Get-Coverage $image) $previewPoint 'NaN'
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production'); Inject = $inject }
                $failed = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/sanitized-NaN"; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/sanitized-NaN-fail.json" $failed
                Assert ($failed.status -eq 'failed' -and $failed.failure -match 'NonFiniteReplaced' -and $failed.failure -match [regex]::Escape($sanitizeSite) -and
                    $failed.failure -match 'under Debug injection') 'Fail mode did not fail at the sanitizing site under its injection' $failed
                Add-Case $backend 'compute-sanitized-NaN'

                $inject = Inject $previewPoint 'Negative' $x $y $component
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production'); Inject = $inject }
                $status = Wait-Report $session { param($r, $s) $r.production -eq 'Passed' -and @(Find-Site $s $sanitizeSite $computeProgram).Count -eq 1 } 'an expected clamp'
                Write-VerificationJson "$directory/sanitized-Negative.json" $status
                $counts = @(Find-Site $status $sanitizeSite $computeProgram)[0]
                Assert ($counts.negativeClamps -eq 1 -and $counts.mostNegative -eq -4 -and $counts.claim -eq 'Passed') 'Expected negative clamp was not counted as passing' $counts
                $image = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/sanitized-Negative"; Label = 'expected'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/sanitized-Negative-image.json" $image
                Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Passed') 'Expected clamp failed the capture' $image
                Assert ((Get-PixelHash (Get-VerificationCaptureFile $image '.png')) -eq $script:controlHash) 'The clamped negative value changed the PNG pixels' $image
                Assert-Stamp $status.watch.report (Get-Coverage $image) $previewPoint 'Negative'
                Add-Case $backend 'compute-sanitized-Negative'

                $inject = Inject $previewPoint 'Overflow' $x $y $component
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'report'; WatchClaims = @('production'); Inject = $inject }
                $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly } 'an error clamp'
                Write-VerificationJson "$directory/sanitized-Overflow.json" $status
                Assert ($status.watch.report.firstAnomaly.reason -eq 'ClampPolicyExceeded' -and $status.watch.report.firstAnomaly.site -eq $sanitizeSite) 'Overflow did not exceed the error policy' $status.watch.report
                $counts = @(Find-Site $status $sanitizeSite $computeProgram)[0]
                Assert ($counts.fp16MaximumClamps -eq 1 -and $counts.maximum -eq 131072 -and $counts.claim -eq 'Failed') 'FP16 clamp counters are wrong' $counts
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production'); Inject = $inject }
                $failed = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/sanitized-Overflow"; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/sanitized-Overflow-fail.json" $failed
                Assert ($failed.status -eq 'failed' -and $failed.failure -match 'ClampPolicyExceeded' -and $failed.failure -match [regex]::Escape($sanitizeSite)) 'Fail mode did not fail on the error clamp' $failed
                Add-Case $backend 'compute-sanitized-Overflow'
            }
        }

        if ($Lab -contains 'temporal-aa') {
            Invoke-Session $backend 'temporal-aa' $taaLabId @('temporal_aa.object.animate=false') {
                param($session, $directory)
                $startup = Invoke-VerificationSession capture $session @{ OutputDirectory = "$directory/startup"; Label = 'startup'; TimeoutSeconds = $TimeoutSeconds }
                Assert ($startup.status -eq 'completed') 'Temporal AA Lab did not become ready' $startup
                $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production') }
                $status = Wait-Report $session { param($r) $r.production -eq 'Passed' } 'clean Temporal AA production evidence'
                Write-VerificationJson "$directory/control.json" $status
                Assert (@($status.watch.sites | Where-Object { $_.claim -eq 'Failed' }).Count -eq 0) 'Uninjected Temporal AA has failing sites' $status.watch
                $image = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/control"; Label = 'control'; TimeoutSeconds = $TimeoutSeconds }
                Write-VerificationJson "$directory/control-image.json" $image
                Assert ($image.status -eq 'completed' -and $image.watch.production -eq 'Passed' -and $null -eq $image.watch.injection) 'Temporal AA control capture did not pass' $image
                Add-Case $backend 'taa-control'

                foreach ($injection in $taaInjections) {
                    $inject = Inject $taaPoint $injection.Value $taaX $taaY $injection.Component
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'report'; WatchClaims = @('production'); Inject = $inject }
                    $status = Wait-Report $session { param($r) $null -ne $r.firstAnomaly } "a Temporal AA $($injection.Value) recovery"
                    Write-VerificationJson "$directory/taa-$($injection.Value).json" $status
                    $anomaly = $status.watch.report.firstAnomaly
                    Assert ($anomaly.reason -eq 'NonFiniteReplaced' -and $anomaly.site -eq $injection.Site -and $anomaly.program -eq $taaProgram) 'Temporal AA anomaly is not at the recovery site' $anomaly
                    $counts = @(Find-Site $status $injection.Site $taaProgram)
                    Assert ($counts.Count -eq 1 -and $counts[0].nonFiniteReplacements -ge 1 -and $counts[0].claim -eq 'Failed') 'Recovery site counters do not show the replacement' $status.watch
                    # The recovery branch keeps the value from every later guard and sanitizer.
                    $escaped = @($status.watch.sites | Where-Object { ($_.claim -eq 'Failed' -or $_.nonFiniteReplacements -gt 0) -and $_.site -ne $injection.Site })
                    Assert ($escaped.Count -eq 0) 'The injected history reached another site' $status.watch
                    Assert-Stamp $status.watch.report $null $taaPoint $injection.Value
                    $null = Invoke-VerificationSession watch $session @{ WatchMode = 'fail'; WatchClaims = @('production'); Inject = $inject }
                    $failed = Invoke-VerificationSession capture $session @{ Timing = 'next-frame'; OutputDirectory = "$directory/taa-$($injection.Value)"; Label = 'fail'; TimeoutSeconds = $TimeoutSeconds }
                    Write-VerificationJson "$directory/taa-$($injection.Value)-fail.json" $failed
                    Assert ($failed.status -eq 'failed' -and $failed.failure -match 'NonFiniteReplaced' -and $failed.failure -match [regex]::Escape($injection.Site) -and
                        $failed.failure -match 'under Debug injection') 'Fail mode did not fail at the recovery site under its injection' $failed
                    Add-Case $backend "taa-$($injection.Value)"
                }
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
