<#
.SYNOPSIS
Verifies generation-bound Deferred sequence events and transition-frame capture
on native texture and Physical Sky lighting paths, with standard validation.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/DeferredSequenceEvents/Native' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
function Check([bool]$Condition, [string]$Name) {
    $script:checks += [pscustomobject]@{ name=$Name; passed=$Condition }
    if (-not $Condition) { throw "Failed: $Name" }
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($content in @('samples', 'profiles')) {
                $case = "$config-$backend-$content"
                $directory = Join-Path $output $case
                $null = New-Item -ItemType Directory -Path $directory
                $session = "deferred-$runId-$case"
                $start = $null; $caseFailure = $null
                try {
                    $options = @{ Configuration=$config;Rhi=$backend;WindowSize='640x360';NoDevTools=$true;
                        Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds }
                    if ($content -eq 'samples') {
                        $options.Lab='gglab.lab.gtao'; $path='SEQ_GTAOLab_Static'; $lastFrame=179
                        $samplesId='gtao.ibl.sample_count'; $effectId=$samplesId; $baseline=32; $target=64
                    } else {
                        $options.Lab='gglab.lab.atmosphere_range'; $path='SEQ_AtmosphereRange_Static'; $lastFrame=239
                        $samplesId='atmosphere_range.ibl.sample_count'; $effectId='environment.profile'; $baseline=0; $target=1
                    }
                    $start = Invoke-VerificationSession start $session $options
                    Write-VerificationJson "$directory/start.json" $start
                    $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                        -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
                    Check ($status.capabilities.sequenceDeferredEvents -and
                        @($status.capabilities.sequenceEventDomains) -contains 'lighting') "$case-capability"
                    $discovery = Invoke-VerificationSession parameters $session
                    Write-VerificationJson "$directory/discovery.json" $discovery
                    [double]$exposure = @($discovery.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current
                    $initial = @(@{id=$samplesId;type='uint';value=32},
                        @{id='view.exposure.ev100';type='float';value=$exposure},
                        @{id='view.taa.enabled';type='bool';value=$true},
                        @{id='view.gtao.temporal_accumulation';type='bool';value=$false})
                    $configured = Invoke-VerificationSession set $session @{Changes=(ConvertTo-Json -InputObject $initial -Depth 8 -Compress);WaitConsumed=$true}
                    Check ($configured.batch.succeeded -and $configured.consumed) "$case-baseline-consumed"
                    if ($content -eq 'profiles') {
                        $selection = @(@{id='environment.profile';type='enum';value=0})
                        $selected = Invoke-VerificationSession set $session @{Changes=(ConvertTo-Json -InputObject $selection -Compress);WaitConsumed=$true}
                        Check ($selected.batch.succeeded -and $selected.consumed) "$case-profile-baseline"
                    }
                    $prime = Invoke-VerificationSession capture $session @{SettleFrames=4;OutputDirectory=$directory;Label='prime';TimeoutSeconds=$TimeoutSeconds}
                    Check ($prime.status -eq 'completed') "$case-ready-history"
                    $before = Invoke-VerificationSession parameters $session
                    $badPlan = @(@{frame=0;set=@{'view.exposure.ev100'=($exposure+2)}},
                        @{frame=10;set=@{$effectId=$target};expectEffects=@()})
                    $bad = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=(ConvertTo-Json -InputObject $badPlan -Depth 10 -Compress);TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/rejected-expectation.json" $bad
                    $after = Invoke-VerificationSession parameters $session
                    Check ($bad.sequence.state -eq 'failed' -and $bad.sequence.submittedFrames -eq 0 -and
                        $after.revision -eq $before.revision -and
                        @($after.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current -eq $exposure) "$case-whole-plan-expectation-rejection"
                    $events = @(@{frame=0;set=@{$effectId=$baseline};expectEffects=@('lighting')},
                        @{frame=10;set=@{$effectId=$target;'view.gtao.radius'=1.25};expectEffects=@('lighting')},
                        @{frame=11;set=@{'view.exposure.ev100'=($exposure+1)};expectEffects=@()},
                        @{frame=12;set=@{$effectId=$target};expectEffects=@('lighting')},
                        @{frame=100;set=@{'view.exposure.ev100'=$exposure}},
                        @{frame=$lastFrame;set=@{$effectId=$target};expectEffects=@('lighting')})
                    $done = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=(ConvertTo-Json -InputObject $events -Depth 10 -Compress);
                        CaptureFrames="9,10,11,80,$lastFrame";OutputDirectory=$directory;Label='timeline';TimeoutSeconds=$TimeoutSeconds;
                        GpuTiming=($content -eq 'samples');Exports='[{"frame":10,"targets":[{"resource":"TAA.ResolvedSceneColor"}],"graph":true}]'}
                    Write-VerificationJson "$directory/timeline.json" $done
                    Check ($done.sequence.state -eq 'completed' -and $done.sequence.submittedFrames -eq ($lastFrame+1)) "$case-complete-range"
                    Check (@($done.sequence.events).Count -eq 6) "$case-event-reports"
                    foreach ($event in $done.sequence.events) {
                        Check ($event.batch.terminal -and $event.batch.succeeded) "$case-event-$($event.frame)-success"
                    }
                    $effect = @($done.sequence.events[1].batch.items | Where-Object id -eq $effectId)[0]
                    $shared = $done.sequence.events[3].batch.items[0]
                    Check ($effect.state -eq 'Consumed' -and $effect.identity.domain -eq 'lighting' -and
                        $effect.requested -eq $effect.applied -and $effect.published -gt $effect.applied -and
                        $effect.firstConsumed -ge $effect.published) "$case-domain-four-stamps"
                    Check ($shared.state -in @('Consumed','NoChange') -and
                        ($shared.state -eq 'NoChange' -or $shared.identity.generation -eq $effect.identity.generation)) "$case-shared-target"
                    Check ($done.sequence.events[0].batch.items[0].state -eq 'NoChange' -and
                        $done.sequence.events[5].batch.items[0].state -eq 'NoChange' -and
                        $null -eq $done.sequence.events[5].batch.items[0].firstConsumed) "$case-nochange-no-transition"
                    $frames = @{}
                    foreach ($file in Get-ChildItem -LiteralPath $directory -Filter 'timeline-*.json') {
                        $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
                        if ($metadata.PSObject.Properties['sequence'] -and $metadata.sequence -and
                            $metadata.PSObject.Properties['image']) { $frames[[int]$metadata.sequence.frame]=$metadata }
                    }
                    foreach ($frame in @(9,10,11,80,$lastFrame)) { Check ($frames.ContainsKey($frame)) "$case-frame-$frame-captured" }
                    Check ($frames[10].frame.serial -eq $effect.applied -and
                        $frames[9].sceneInputs.environment.activeIBLGeneration -eq $frames[10].sceneInputs.environment.activeIBLGeneration -and
                        $frames[10].sceneInputs.environment.activeIBLGeneration -ne $effect.identity.generation) "$case-transition-image-retains-old-generation"
                    Check ($frames[80].sceneInputs.environment.activeIBLGeneration -eq $effect.identity.generation -and
                        $frames[80].frame.serial -ge $effect.firstConsumed) "$case-published-image-matches-receipt"
                    Check ($frames[9].temporal.status -eq 'active' -and $frames[10].temporal.status -eq 'active' -and
                        $frames[11].temporal.status -eq 'active' -and
                        $frames[11].frame.serial -eq $done.sequence.events[2].batch.items[0].firstConsumed) "$case-temporal-and-immediate-continuity"
                    $publication = $done.sequence.parameterPublication
                    Check ($publication.outcome -eq 'Published' -and @($publication.files).Count -eq 1 -and
                        $done.sequence.parameterFrameCount -eq ($lastFrame+1)) "$case-required-parameter-publication"
                    $range = Get-Content -LiteralPath $publication.files[0] -Raw | ConvertFrom-Json
                    Check ($range.schema -eq 'gglab.parameter-frames' -and $range.schemaVersion -eq 1 -and
                        @($range.frames).Count -eq ($lastFrame+1)) "$case-complete-parameter-range"
                    foreach ($frame in @(9,10,11,80,$lastFrame)) {
                        $parameters = $frames[$frame].parameters
                        Check ($parameters.frameSerial -eq $frames[$frame].frame.serial -and
                            (ConvertTo-Json -InputObject $parameters -Depth 32 -Compress) -ceq
                            (ConvertTo-Json -InputObject $range.frames[$frame].parameters -Depth 32 -Compress)) "$case-frame-$frame-frozen-provenance"
                    }
                    $pending = $range.frames[10].parameters
                    Check ($pending.lighting.requested.generation -eq $effect.identity.generation -and
                        $pending.lighting.active.generation -ne $effect.identity.generation -and
                        $range.frames[80].parameters.lighting.active.generation -eq $effect.identity.generation) "$case-requested-active-generations"
                    $pendingReceipt = @($pending.receipts | Where-Object { $_.id -eq $effectId })[0]
                    $consumedFrame = @($range.frames | Where-Object { $_.parameters.frameSerial -eq $effect.firstConsumed })[0]
                    $consumedReceipt = @($consumedFrame.parameters.receipts | Where-Object { $_.batchId -eq $pendingReceipt.batchId -and $_.id -eq $effectId })[0]
                    Check ($null -eq $pendingReceipt.firstConsumed -and $consumedReceipt.firstConsumed -eq $effect.firstConsumed -and
                        $consumedReceipt.state -eq 'Consumed') "$case-consumption-stays-on-submitted-frame"
                    if ($content -eq 'profiles') {
                        Check ($pending.lighting.requested.profileId -ne $pending.lighting.active.profileId -and
                            $pending.lighting.requested.profileHash -match '^[0-9a-f]{64}$' -and
                            $pending.lighting.active.profileHash -match '^[0-9a-f]{64}$' -and
                            $pending.lighting.requested.profileHashVersion -eq 2 -and
                            $pending.lighting.active.profileHashVersion -eq 2) "$case-frozen-profile-identities"
                    } else {
                        Check ($done.sequence.timingPublication.manifest -eq $publication.manifest) "$case-timing-shares-publication"
                    }
                    $exports = @(Get-ChildItem -LiteralPath $directory -Filter 'timeline*.json' | ForEach-Object {
                        Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
                    } | Where-Object { $_.PSObject.Properties['schema'] -and $_.schema -in @('gglab.render-graph','gglab.resource-export') })
                    Check ($exports.Count -eq 2) "$case-required-graph-resource-pair"
                    foreach ($export in $exports) {
                        Check ($export.frameSerial -eq $frames[10].frame.serial -and
                            $export.frame.parameterRevision -eq $pending.revision -and
                            (ConvertTo-Json -InputObject $export.frame.parameters -Depth 32 -Compress) -ceq
                            (ConvertTo-Json -InputObject $pending -Depth 32 -Compress)) "$case-$($export.schema)-provenance"
                    }
                    # A newly requested generation on the final frame cannot consume outside the measurement range.
                    $unreachedTarget = if ($content -eq 'profiles') {0} else {128}
                    $lastEvent = @(@{frame=$lastFrame;set=@{$effectId=$unreachedTarget};expectEffects=@('lighting')})
                    $unreached = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=(ConvertTo-Json -InputObject $lastEvent -Depth 10 -Compress);TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/unreached.json" $unreached
                    $item = $unreached.sequence.events[0].batch.items[0]
                    Check ($unreached.sequence.state -eq 'failed' -and $unreached.sequence.submittedFrames -eq ($lastFrame+1) -and
                        $item.state -eq 'EffectNotReached' -and $null -ne $item.applied -and
                        $null -eq $item.firstConsumed) "$case-last-frame-effect-not-reached"
                } catch { $caseFailure=$_; throw }
                finally {
                    if ($start) {
                        try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                        catch {
                            if ($null -eq $caseFailure) { throw }
                            [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                        }
                        Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                    }
                }
                $log = Get-Content -LiteralPath "$directory/output.log" -Raw
                Check ($log -notmatch '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') "$case-native-validation"
                $pattern = 'IBL bake generation '+$effect.identity.generation+' published atomically \('
                if ($content -eq 'profiles') { $pattern += 'cacheStages=0, gpuStages=[1-9]\d*,.*source=Physical Sky' }
                Check ([regex]::IsMatch($log, $pattern)) "$case-native-publication"
                $cases += @{configuration=$config;backend=$backend;content=$content;passed=$true;directory=$directory}
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
