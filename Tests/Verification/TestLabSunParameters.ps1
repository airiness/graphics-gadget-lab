<#
.SYNOPSIS
Verifies the Atmosphere Range authored sun-elevation binding and submitted-frame evidence.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/LabSun/Native' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
$sunId = 'atmosphere_range.sun.elevation_degrees'
function Check([bool]$Condition, [string]$Name) {
    $script:checks += @{name=$Name;passed=$Condition}
    if (-not $Condition) { throw "Failed: $Name" }
}
function Set-Values($Values, [switch]$AllowFailure) {
    Invoke-VerificationSession set $script:session @{Changes=(ConvertTo-Json -InputObject @($Values) -Compress -Depth 8);
        WaitConsumed=$true} -AllowFailure:$AllowFailure
}
function Elevation($Direction) {
    [Math]::Atan2(-[double]$Direction[1], [Math]::Sqrt([double]$Direction[0]*$Direction[0]+[double]$Direction[2]*$Direction[2]))*180/[Math]::PI
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $case = "$config-$backend"
            $directory = Join-Path $output $case
            $null = New-Item -ItemType Directory -Path $directory
            $script:session = "sun-$runId-$case"
            $start = $null; $failure = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.atmosphere_range';WindowSize='640x360';NoDevTools=$true;Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                    -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                $discovery = Invoke-VerificationSession parameters $session
                Write-VerificationJson "$directory/discovery.json" $discovery
                $desc = @($discovery.parameters | Where-Object id -eq $sunId)[0]
                Check ($desc.source -eq 'lab' -and $desc.type -eq 'float' -and $desc.declaredClass -eq 'Deferred' -and
                    $desc.setSupported -and $desc.min -eq -90 -and $desc.max -eq 90) "$case-descriptor"
                [double]$exposure = @($discovery.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current
                $samples = Set-Values @(@{id='atmosphere_range.ibl.sample_count';type='uint';value=32})
                Check ($samples.batch.succeeded) "$case-small-bake"
                $previousGeneration = $samples.batch.items[0].identity.generation
                $angleIndex = 0
                foreach ($degrees in @(0.0,0.01,-0.01)) {
                    $angleIndex++
                    $angle = Set-Values @(@{id=$sunId;type='float';value=$degrees})
                    Write-VerificationJson "$directory/angle-$angleIndex-receipt.json" $angle
                    $effect = $angle.batch.items[0]
                    Check ($angle.batch.succeeded -and $effect.state -eq 'Consumed' -and
                        $effect.identity.domain -eq 'lighting' -and $effect.identity.generation -gt $previousGeneration -and
                        $effect.applied -ge $effect.requested -and $effect.published -ge $effect.applied -and
                        $effect.firstConsumed -ge $effect.published) "$case-angle-$angleIndex-new-target"
                    $current = Invoke-VerificationSession parameters $session
                    Check ([Math]::Abs([double]@($current.parameters | Where-Object id -eq $sunId)[0].current-$degrees) -lt 0.000001) "$case-angle-$angleIndex-discovery"
                    $repeat = Set-Values @(@{id=$sunId;type='float';value=$degrees})
                    Write-VerificationJson "$directory/angle-$angleIndex-repeat.json" $repeat
                    Check ($repeat.batch.succeeded -and $repeat.batch.items[0].state -eq 'NoChange' -and
                        $null -eq $repeat.batch.items[0].firstConsumed) "$case-angle-$angleIndex-nochange"
                    $capture = Invoke-VerificationSession capture $session @{SettleFrames=2;OutputDirectory=$directory;
                        Label="angle-$angleIndex";TimeoutSeconds=$TimeoutSeconds}
                    Check ($capture.status -eq 'completed') "$case-angle-$angleIndex-capture"
                    $metadata = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.json' 0) -Raw | ConvertFrom-Json
                    Check ([Math]::Abs((Elevation $metadata.sceneInputs.sun.photonDirection)-$degrees) -lt 0.00001 -and
                        $metadata.parameters.lighting.requested.generation -eq $effect.identity.generation -and
                        $metadata.parameters.lighting.active.generation -eq $effect.identity.generation) "$case-angle-$angleIndex-actual-direction-and-generation"
                    $previousGeneration = $effect.identity.generation
                }
                $baseline = Set-Values @(@{id=$sunId;type='float';value=45.0},
                    @{id='view.taa.enabled';type='bool';value=$true})
                Check ($baseline.batch.succeeded) "$case-baseline"
                $same = Set-Values @(@{id=$sunId;type='float';value=45.0})
                Check ($same.batch.items[0].state -eq 'NoChange' -and $null -eq $same.batch.items[0].firstConsumed) "$case-baseline-nochange"
                $before = Invoke-VerificationSession parameters $session
                $invalid = Set-Values @(@{id='view.exposure.ev100';type='float';value=($exposure+2)},
                    @{id=$sunId;type='float';value=91.0}) -AllowFailure
                $two = Set-Values @(@{id=$sunId;type='float';value=20.0},
                    @{id='atmosphere_range.ibl.sample_count';type='uint';value=64}) -AllowFailure
                $after = Invoke-VerificationSession parameters $session
                Check (-not $invalid.ok -and -not $two.ok -and $before.revision -eq $after.revision -and
                    @($after.parameters | Where-Object id -eq $sunId)[0].current -eq 45) "$case-atomic-rejection"
                $badEvents = @(@{frame=0;set=@{'view.exposure.ev100'=($exposure+2)}},
                    @{frame=10;set=@{$sunId=91}})
                $bad = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_AtmosphereRange_Static';
                    Events=(ConvertTo-Json -InputObject $badEvents -Compress -Depth 10);TimeoutSeconds=$TimeoutSeconds}
                Check ($bad.sequence.state -eq 'failed' -and $bad.sequence.submittedFrames -eq 0 -and
                    (Invoke-VerificationSession parameters $session).revision -eq $before.revision) "$case-whole-plan-rejection"
                $prime = Invoke-VerificationSession capture $session @{SettleFrames=4;OutputDirectory=$directory;Label='prime';TimeoutSeconds=$TimeoutSeconds}
                Check ($prime.status -eq 'completed') "$case-prime-history"
                $events = @(@{frame=0;set=@{$sunId=45}},
                    @{frame=10;set=@{$sunId=20;'view.gtao.radius'=1.25};expectEffects=@('lighting')},
                    @{frame=11;set=@{'view.exposure.ev100'=$exposure}},
                    @{frame=12;set=@{$sunId=20};expectEffects=@('lighting')},
                    @{frame=130;set=@{$sunId=60};expectEffects=@('lighting')},
                    @{frame=239;set=@{$sunId=60};expectEffects=@('lighting')})
                $done = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_AtmosphereRange_Static';
                    Events=(ConvertTo-Json -InputObject $events -Compress -Depth 12);CaptureFrames='9,10,11,80,130,200,239';
                    Exports='[{"frame":10,"targets":[{"resource":"TAA.ResolvedSceneColor"}],"graph":true}]';
                    GpuTiming=$true;OutputDirectory=$directory;Label='sun';TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/sequence.json" $done
                Check ($done.sequence.state -eq 'completed' -and $done.sequence.submittedFrames -eq 240 -and
                    $done.sequence.parameterFrameCount -eq 240 -and $done.sequence.parameterPublication.outcome -eq 'Published' -and
                    $done.sequence.parameterPublication.manifest -eq $done.sequence.timingPublication.manifest) "$case-complete-range"
                foreach ($event in $done.sequence.events) { Check ($event.batch.succeeded) "$case-event-$($event.frame)" }
                Check ($done.sequence.events[0].batch.items[0].state -eq 'NoChange' -and
                    $done.sequence.events[5].batch.items[0].state -eq 'NoChange') "$case-sequence-nochange"
                $frames = @{}
                foreach ($file in Get-ChildItem -LiteralPath $directory -Filter 'sun-*.json') {
                    $m = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
                    if ($m.PSObject.Properties['image']) { $frames[[int]$m.sequence.frame]=$m }
                }
                $range = Get-Content -LiteralPath $done.sequence.parameterPublication.files[0] -Raw | ConvertFrom-Json
                foreach ($frame in @(9,10,11,80,130,200,239)) {
                    Check ($frames.ContainsKey($frame) -and $frames[$frame].parameters.frameSerial -eq $range.frames[$frame].parameters.frameSerial -and
                        $frames[$frame].sceneInputs.exposure.manualEV100 -eq $exposure) "$case-frame-$frame-provenance"
                }
                foreach ($index in @(1,4)) {
                    $effect = @($done.sequence.events[$index].batch.items | Where-Object id -eq $sunId)[0]
                    Check ($effect.state -eq 'Consumed' -and $effect.requested -eq $effect.applied -and
                        $effect.published -gt $effect.applied -and $effect.firstConsumed -ge $effect.published -and
                        $effect.identity.domain -eq 'lighting') "$case-event-$index-four-stamps"
                    $frame = [int]$done.sequence.events[$index].frame
                    Check ($frames[$frame].parameters.lighting.requested.generation -eq $effect.identity.generation -and
                        $frames[$frame].parameters.lighting.active.generation -ne $effect.identity.generation -and
                        $null -eq $frames[$frame].parameters.lighting.requested.profileId) "$case-event-$index-pending-custom-sun"
                }
                Check ([Math]::Abs((Elevation $frames[9].sceneInputs.sun.photonDirection)-45) -lt 0.001 -and
                    [Math]::Abs((Elevation $frames[10].sceneInputs.sun.photonDirection)-45) -lt 0.001 -and
                    [Math]::Abs((Elevation $frames[80].sceneInputs.sun.photonDirection)-20) -lt 0.001 -and
                    [Math]::Abs((Elevation $frames[200].sceneInputs.sun.photonDirection)-60) -lt 0.001) "$case-active-sun-matches-generation"
                Check ($frames[9].temporal.status -eq 'active' -and $frames[10].temporal.status -eq 'active' -and
                    $frames[11].temporal.status -eq 'active') "$case-taa-continuity"
                $profile = Set-Values @(@{id='environment.profile';type='enum';value=1})
                $rebased = Invoke-VerificationSession parameters $session
                $obliqueElevation = 180/[Math]::PI*[Math]::Atan2(0.25,[Math]::Sqrt(2))
                [double]$rebasedElevation = @($rebased.parameters | Where-Object id -eq $sunId)[0].current
                Check ($profile.batch.succeeded -and [Math]::Abs($rebasedElevation-$obliqueElevation) -lt 0.001) "$case-follows-foreign-profile"
                $reasserted = Set-Values @(@{id=$sunId;type='float';value=60.0})
                $repeated = Set-Values @(@{id=$sunId;type='float';value=60.0})
                Check ($reasserted.batch.succeeded -and $repeated.batch.items[0].state -eq 'NoChange') "$case-rebased-azimuth-nochange"
                $oblique = Invoke-VerificationSession capture $session @{SettleFrames=2;OutputDirectory=$directory;Label='oblique';TimeoutSeconds=$TimeoutSeconds}
                $image = Get-VerificationCaptureFile $oblique '.json' 0
                $m = Get-Content -LiteralPath $image -Raw | ConvertFrom-Json
                Check ([Math]::Abs((Elevation $m.sceneInputs.sun.photonDirection)-60) -lt 0.001 -and
                    $m.sceneInputs.sun.photonDirection[0] -lt -0.3 -and $m.sceneInputs.sun.photonDirection[2] -gt 0.3 -and
                    $m.sceneInputs.sun.toaIlluminanceLux -eq 120000) "$case-preserves-azimuth-and-sun-units"
                $lastEvent = @(@{frame=239;set=@{$sunId=20};expectEffects=@('lighting')})
                $unreached = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_AtmosphereRange_Static';
                    Events=(ConvertTo-Json -InputObject $lastEvent -Compress -Depth 10);TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/unreached.json" $unreached
                Check ($unreached.sequence.state -eq 'failed' -and
                    $unreached.sequence.events[0].batch.items[0].state -eq 'EffectNotReached' -and
                    $null -eq $unreached.sequence.events[0].batch.items[0].firstConsumed) "$case-last-frame-not-consumed"
                $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
            } catch { $failure=$_; throw }
            finally {
                if ($start) {
                    try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                    catch { if ($null -eq $failure) { throw }; [Console]::Error.WriteLine($_.Exception.Message) }
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                }
            }
            $log = Get-Content -LiteralPath "$directory/output.log" -Raw
            Check ($log -notmatch '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') "$case-native-validation"
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
