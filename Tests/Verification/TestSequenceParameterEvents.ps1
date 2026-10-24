<#
.SYNOPSIS
Verifies exact-frame Immediate parameter events on Demo and Lab content on both RHIs.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/SequenceEvents/Native' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @(); $succeeded = $false
function Check([bool]$ok, [string]$name) {
    $script:checks += [pscustomobject]@{ name=$name; passed=$ok }
    if (-not $ok) { throw "Check failed: $name" }
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($content in @('lab', 'demo')) {
                $case = "$config-$backend-$content"
                $directory = Join-Path $output $case
                $null = New-Item -ItemType Directory -Path $directory
                $session = "events-$runId-$case"
                $start = $null; $failure = $null
                try {
                    $options = @{Configuration=$config;Rhi=$backend;WindowSize='640x360';NoDevTools=$true;
                        Validation='standard';StateRoot="$output/state-$config-$backend";StartTimeoutSeconds=$TimeoutSeconds}
                    if ($content -eq 'lab') { $options.Lab='gglab.lab.gtao'; $path='SEQ_GTAOLab_Static' }
                    else { $options.Demo='atrium'; $path='SEQ_StaticRailings' }
                    $lastFrame = if ($content -eq 'lab') {179} else {95}
                    $start = Invoke-VerificationSession start $session $options
                    Write-VerificationJson "$directory/start.json" $start
                    $ready = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session} `
                        -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                    Check ($ready.capabilities.sequenceImmediateEvents) "$case-capability"
                    $discovery = Invoke-VerificationSession parameters $session
                    [double]$baseline = @($discovery.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current
                    $initial = @(@{id='view.taa.enabled';type='bool';value=$false},
                        @{id='view.gtao.temporal_accumulation';type='bool';value=$false},
                        @{id='view.exposure.compensation';type='float';value=0.0})
                    $configured = Invoke-VerificationSession set $session @{Changes=($initial | ConvertTo-Json -Depth 8 -Compress);WaitConsumed=$true}
                    Check ($configured.batch.succeeded) "$case-initial-settings"
                    $before = Invoke-VerificationSession parameters $session
                    $badPlan = @(@{frame=0;set=@{'view.exposure.ev100'=($baseline+1)}},
                        @{frame=10;set=@{'missing.sequence.parameter'=$true}})
                    $bad = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=($badPlan | ConvertTo-Json -Depth 10 -Compress);TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/rejected-plan.json" $bad
                    $after = Invoke-VerificationSession parameters $session
                    Check ($bad.sequence.state -eq 'failed' -and $bad.sequence.submittedFrames -eq 0 -and
                        $before.revision -eq $after.revision -and @($after.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current -eq $baseline) "$case-whole-plan-atomic-rejection"
                    $changes = @{'view.exposure.ev100'=($baseline+2);'view.gtao.radius'=1.25}
                    if ($content -eq 'lab') { $changes['gtao.radius']=1.25 }
                    $events = @(@{frame=0;set=@{'view.exposure.ev100'=$baseline};expectEffects=@()},
                        @{frame=10;set=$changes},
                        @{frame=20;set=@{'view.exposure.ev100'=($baseline+2)}},
                        @{frame=$lastFrame;set=@{'view.exposure.ev100'=$baseline}})
                    $done = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=($events | ConvertTo-Json -Depth 10 -Compress);
                        CaptureFrames="0,9,10,20,$lastFrame";OutputDirectory=$directory;Label='event';TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/sequence.json" $done
                    Check ($done.sequence.state -eq 'completed' -and $done.sequence.submittedFrames -eq ($lastFrame+1) -and
                        $done.sequence.events.Count -eq 4) "$case-completed"
                    foreach ($event in $done.sequence.events) {
                        Check ($event.state -eq 'Finished' -and $event.batch.succeeded) "$case-event-$($event.frame)-finished"
                        foreach ($item in $event.batch.items) {
                            Check ($item.state -eq 'NoChange' -or ($item.state -eq 'Consumed' -and
                                $item.requested -eq $item.applied -and $item.published -eq $item.applied -and
                                $item.firstConsumed -ge $item.published)) "$case-event-$($event.frame)-stamps"
                        }
                        $hidden = Invoke-VerificationSession parameter-result $session @{BatchId=$event.batch.batchId;Consume=$true} -AllowFailure
                        Check (-not $hidden.ok) "$case-event-$($event.frame)-internally-consumed"
                    }
                    Check ($done.sequence.events[2].batch.items[0].state -eq 'NoChange') "$case-event-nochange"
                    $frames = @{}
                    foreach ($file in Get-ChildItem -LiteralPath $directory -Filter '*.json') {
                        $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
                        if ($metadata.PSObject.Properties['sequence'] -and $metadata.sequence -and
                            $metadata.PSObject.Properties['image']) { $frames[[int]$metadata.sequence.frame]=$metadata }
                    }
                    foreach ($frame in @(0,9,10,20,$lastFrame)) {
                        Check ($frames.ContainsKey($frame)) "$case-frame-$frame-metadata"
                        $expected = if ($frame -in @(10,20)) { $baseline+2 } else { $baseline }
                        Check ($frames[$frame].sceneInputs.exposure.manualEV100 -eq $expected) "$case-frame-$frame-effective-exposure"
                    }
                    $serial = [long]$frames[10].frame.serial
                    foreach ($item in $done.sequence.events[1].batch.items) {
                        Check ($item.firstConsumed -eq $serial) "$case-change-consumed-on-frame-ten"
                    }
                    $same = @(@{id='view.exposure.ev100';type='float';value=$baseline})
                    $last = Invoke-VerificationSession set $session @{Changes=(ConvertTo-Json -InputObject $same -Compress);WaitConsumed=$true}
                    Check ($last.batch.succeeded -and $last.consumed) "$case-no-leaked-result-slot"
                    # Exercise the production history producer through an uncut exposure transition.
                    $taa = @(@{id='view.taa.enabled';type='bool';value=$true})
                    $enabled = Invoke-VerificationSession set $session @{Changes=(ConvertTo-Json -InputObject $taa -Compress);WaitConsumed=$true}
                    Check ($enabled.batch.succeeded) "$case-history-enabled"
                    $prime = Invoke-VerificationSession capture $session @{SettleFrames=4;OutputDirectory=$directory;Label='history-prime';TimeoutSeconds=$TimeoutSeconds}
                    Check ($prime.status -eq 'completed') "$case-history-ready"
                    $historyEvents = @(@{frame=10;set=@{'view.exposure.ev100'=($baseline+1)}},
                        @{frame=$lastFrame;set=@{'view.exposure.ev100'=$baseline}})
                    $history = Invoke-VerificationSession sequence $session @{CameraPath=$path;Events=($historyEvents | ConvertTo-Json -Depth 10 -Compress);
                        CaptureFrames='9,10';OutputDirectory=$directory;Label='history-event';TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/history-sequence.json" $history
                    Check ($history.sequence.state -eq 'completed' -and $history.sequence.submittedFrames -eq ($lastFrame+1)) "$case-history-continuity"
                    foreach ($event in $history.sequence.events) {
                        Check ($event.batch.succeeded -and $event.batch.items[0].state -eq 'Consumed') "$case-history-event-$($event.frame)"
                    }
                    $historyFrames = @{}
                    foreach ($file in Get-ChildItem -LiteralPath $directory -Filter 'history-event-*.json') {
                        $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
                        if ($metadata.PSObject.Properties['sequence'] -and $metadata.sequence -and
                            $metadata.PSObject.Properties['image']) { $historyFrames[[int]$metadata.sequence.frame]=$metadata }
                    }
                    Check ($historyFrames.ContainsKey(9) -and $historyFrames.ContainsKey(10) -and
                        $historyFrames[9].temporal.status -eq 'active' -and $historyFrames[10].temporal.status -eq 'active') "$case-real-history-active"
                    Check ($history.sequence.events[0].batch.items[0].firstConsumed -eq $historyFrames[10].frame.serial -and
                        $historyFrames[9].sceneInputs.exposure.manualEV100 -eq $baseline -and
                        $historyFrames[10].sceneInputs.exposure.manualEV100 -eq ($baseline+1)) "$case-history-frame-ten-consumption"
                    $cases += [pscustomobject]@{name=$case;passed=$true;session=$session;directory=$directory}
                } catch { $failure=$_.Exception.Message; throw }
                finally {
                    if ($null -ne $start) {
                        $stop = Stop-VerificationSession $session $start "$directory/stop.json" -RequireCleanExit
                        Check ($stop.exitCode -eq 0) "$case-clean-stop"
                        Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                        $log = Get-Content -LiteralPath $start.outputLog -Raw
                        Check ($log -notmatch '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') "$case-validation-log"
                    }
                    if ($failure) { $cases += [pscustomobject]@{name=$case;passed=$false;failure=$failure} }
                }
            }
        }
    }
    $succeeded = $true
} finally {
    $result = @{schemaVersion=1;ok=$succeeded;directory=$output;checks=$checks;cases=$cases}
    Write-VerificationJson "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
}
