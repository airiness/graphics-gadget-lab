<# Checks submitted-frame warmup without launching a renderer. #>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
Import-Module "$PSScriptRoot/GIProbeViewerSupport.psm1"
$module = Get-Module GIProbeViewerSupport
if (-not $OutputDirectory) { $OutputDirectory = "$PSScriptRoot/../../Build/Validation/GIProbeViewerWarmup" }
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $output -Force
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Sample([uint64]$Serial, [uint32]$Settled) {
    return [pscustomobject]@{
        environment=[pscustomobject]@{process=[pscustomobject]@{runIdentity='fixture-run'}}
        session=[pscustomobject]@{width=1280;height=720}
        parameterControl=[pscustomobject]@{available=$true;revision=3}
        frame=[pscustomobject]@{frameSerial=$Serial;settledFrames=$Settled;ready=$true;outcome='submitted'
            labId='gglab.lab.gi_reference';backend='dx12';controlState=[pscustomobject]@{revision=1}
            giField=[pscustomobject]@{summary=[pscustomobject]@{initialized=$true
                field=[pscustomobject]@{generation=1};probeVisualization=[pscustomobject]@{
                    view=[pscustomobject]@{revision=1};settings=[pscustomobject]@{enabled=$false;budget=2048}}}}}
    }
}
& $module {
    function script:Invoke-VerificationSession($Command, $Session, $Parameters) {
        if ($Command -eq 'capture') {
            $script:captureParameters = $Parameters
            return [pscustomobject]@{status='completed';publication='Published'}
        }
        if ($Command -ne 'status') { throw 'Warmup must observe submitted frames through status.' }
        if ($script:queue.Count -eq 0) { throw 'No submitted warmup progress.' }
        return $script:queue.Dequeue()
    }
}
function Run-Trace($Samples, $Name, [int]$Frames = 120) {
    & $module { param($Values)
        $script:queue = New-Object 'Collections.Generic.Queue[object]'
        foreach ($value in $Values) { $script:queue.Enqueue($value) }
    } -Values $Samples
    return Wait-ProbeViewerWarmup -Session fixture -Run fixture-run -Frames $Frames -Directory "$output/$Name" -TimeoutSeconds 2
}
function Reject($Samples, [string]$Pattern, [string]$Name) {
    try { $null = Run-Trace $Samples $Name } catch {
        Check ($_.Exception.Message -match $Pattern) $Name
        Check (-not (Test-Path "$output/$Name/submitted-frames.json")) "$Name-no-success-proof"
        return
    }
    throw "Expected rejection: $Name"
}
# NextFrame can apply the view while loading and leave history permanently unready.
# AfterReady owns the ready-frame cut/recovery before these allocation captures.
foreach ($view in @('', 'F4_Floor')) {
    $null = Invoke-ProbeViewerCapture fixture fixture-run "$output/allocation-$view" $false 32 $false 2 $view
    $capture = & $module { $script:captureParameters }
    Check ($capture.Timing -eq 'after-ready' -and $capture.SettleFrames -eq 32 -and
        $capture.View -eq $view -and $capture.ExpectedRunIdentity -eq 'fixture-run') "allocation-ready-history-$view"
}
$proof = Run-Trace @((Sample 1000 10),(Sample 1060 70),(Sample 1120 130)) exact
Check ($proof.submittedFrames -eq 120 -and $proof.samples.Count -eq 3) 'counts-additional-submitted-frames'
$saved = Get-Content "$output/exact/submitted-frames.json" -Raw | ConvertFrom-Json
Check ($saved.start.frameSerial -eq 1000 -and $saved.end.frameSerial -eq 1120) 'persists-endpoint-serials'
$proof = Run-Trace @((Sample 1000 10),(Sample 1125 135)) overshoot
Check ($proof.submittedFrames -eq 125) 'polling-overshoot-is-an-observed-minimum'
Reject @([pscustomobject]@{status='completed';publication='Published';timing=[pscustomobject]@{
    mode='next-frame';settleFrames=0;settledFrames=0}}) 'not a capture receipt' 'immediate-next-frame-is-not-warmup'
Reject @((Sample 1000 10),(Sample 5000 10)) 'No submitted warmup progress' 'serial-gap-alone-is-not-warmup'
Reject @((Sample 1000 10),(Sample 1119 130)) 'contradicted frame serials' 'counter-cannot-exceed-serial-progress'
Reject @((Sample 1000 10),(Sample 1120 9)) 'counter reset' 'counter-reset'
Reject @((Sample 1000 10),(Sample 999 10)) 'contradicted frame serials' 'serial-rewind'
foreach ($change in @('revision','arm','field','view','run','readiness','outcome')) {
    $next = Sample 1120 130
    switch ($change) {
        revision { $next.parameterControl.revision = 4 }
        arm { $next.frame.giField.summary.probeVisualization.settings.enabled = $true }
        field { $next.frame.giField.summary.field.generation = 2 }
        view { $next.frame.giField.summary.probeVisualization.view.revision = 2 }
        run { $next.environment.process.runIdentity = 'replacement-run' }
        readiness { $next.frame.ready = $false }
        outcome { $next.frame.outcome = 'aborted' }
    }
    Reject @((Sample 1000 10),$next) 'binding changed|identity changed|lost a submitted Ready frame' "$change-drift"
}
$cold = Sample 900 0
$cold.frame.ready = $false
$proof = Run-Trace @($cold,(Sample 1000 10),(Sample 1120 130)) cold
Check ($proof.start.frameSerial -eq 1000) 'baseline-starts-only-after-submitted-ready'
$result = @{ok=$true;directory=$output;checks=$checks}
$json = $result | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText("$output/result.json", $json, (New-Object Text.UTF8Encoding($false)))
[Console]::Out.WriteLine($json)
