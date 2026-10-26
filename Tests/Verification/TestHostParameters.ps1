<#
.SYNOPSIS
Verifies session Host Immediate parameters on Demo and Lab content, mixed-source
atomic admission, actual built settings, exposure response and result ownership,
guarded exact Host view override states (pinning, stale-stamp conflicts and exact
restoration), and capture predicates decided before issue.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$StateDirectory,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
Add-Type -AssemblyName System.Drawing
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/HostParameters' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
# Optional reuse is limited to caller-selected verification state; no user state is erased.
$stateParent = if ($StateDirectory) {[IO.Path]::GetFullPath($StateDirectory)} else {$output}
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Set-Values($Items, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $arguments = @{ Changes=(ConvertTo-Json -InputObject @($Items) -Compress -Depth 8) }
    foreach ($key in $Options.Keys) { $arguments[$key] = $Options[$key] }
    return Invoke-VerificationSession set $session $arguments -AllowFailure:$AllowFailure
}
function Get-OverrideState($Descriptor) {
    $state = [ordered]@{ id=$Descriptor.id; type=$Descriptor.type; present=[bool]$Descriptor.hostOverride.present }
    if ($state.present) { $state.value = $Descriptor.hostOverride.value }
    return $state
}
function Set-Overrides($Expected, $States, [switch]$AllowFailure) {
    return Invoke-VerificationSession host-view-overrides $session @{
        ExpectedState=(ConvertTo-Json -InputObject $Expected -Compress -Depth 4)
        Overrides=(ConvertTo-Json -InputObject @($States) -Compress -Depth 8); WaitConsumed=$true } -AllowFailure:$AllowFailure
}
function Capture-View([string]$Label, [double]$Ev) {
    $capture = Invoke-VerificationSession capture $session @{SettleFrames=8;OutputDirectory=$directory;Label=$Label;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$directory/$Label-receipt.json" $capture
    $metadata = Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
    Write-VerificationJson "$directory/$Label-settings.json" $metadata
    Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published' -and
        $metadata.sceneInputs.exposure.manualEV100 -eq $Ev -and
        $metadata.sceneInputs.exposure.compensationEV -eq 0 -and
        [Math]::Abs($metadata.temporal.gtao.radius - 3) -lt 0.00001 -and
        -not $metadata.temporal.gtao.temporalAccumulation -and -not $metadata.temporal.requested) "$case-$Label-built-settings"
    return Get-VerificationCaptureFile $capture '.png'
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($content in @('demo', 'lab')) {
                $case = "$config-$backend-$content"
                $directory = Join-Path $output $case
                $null = New-Item -ItemType Directory -Path $directory
                $session = "host-$runId-$case"
                $start = $null; $caseFailure = $null
                try {
                    $options = @{Configuration=$config;Rhi=$backend;WindowSize='640x360';NoDevTools=$true;
                        Validation='standard';StateRoot="$stateParent/state-$config-$backend";StartTimeoutSeconds=$TimeoutSeconds}
                    # Serial content cases share only this run's backend-specific state/cache.
                    # Host overrides are process-local; captures and receipts remain case-local.
                    if ($content -eq 'demo') { $options.Demo='atrium' } else { $options.Lab='gglab.lab.gtao' }
                    $start = Invoke-VerificationSession start $session $options
                    Write-VerificationJson "$directory/start.json" $start
                    $status = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session} `
                        -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                    Check ($status.capabilities.sessionParameterControl.sources -contains 'host' -and
                        $status.capabilities.sessionParameterControl.mixedSources -and
                        $status.parameterControl.available) "$case-capability"
                    # After-ready capture establishes clean ready history after startup loading.
                    $prime = Invoke-VerificationSession capture $session @{SettleFrames=8;OutputDirectory=$directory;Label='discovery-prime';TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/discovery-prime-receipt.json" $prime
                    $status = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session} `
                        -Until {param($r) $r.frame -and $r.frame.ready -and $r.frame.settledFrames -ge 8} -TimeoutSeconds $TimeoutSeconds
                    $settleOriginBeforeDiscovery = [long]$status.frame.frameSerial - [long]$status.frame.settledFrames
                    Write-VerificationJson "$directory/before-discovery.json" $status
                    $parameters = Invoke-VerificationSession parameters $session
                    Write-VerificationJson "$directory/parameters.json" $parameters
                    Check (@($parameters.parameters | Where-Object { $_.source -eq 'host' -and $_.id -like 'view.*' }).Count -eq 25 -and
                        @($parameters.parameters | Where-Object id -eq 'view.exposure.ev100')[0].setSupported -and
                        (@($parameters.parameters | Where-Object source -eq 'lab').Count -gt 0) -eq ($content -eq 'lab')) "$case-discovery"
                    $inheritedRadius = @($parameters.parameters | Where-Object id -eq 'view.gtao.radius')[0].current
                    $firstNoChange = Set-Values @(@{id='view.gtao.radius';type='float';value=$inheritedRadius}) @{WaitConsumed=$true}
                    $settleAfter = Invoke-VerificationSession status $session
                    Write-VerificationJson "$directory/after-first-nochange.json" $settleAfter
                    Check ($firstNoChange.batch.succeeded -and $firstNoChange.batch.items[0].state -eq 'NoChange' -and
                        $firstNoChange.batch.revision -eq $parameters.revision -and
                        ([long]$settleAfter.frame.frameSerial - [long]$settleAfter.frame.settledFrames) -eq $settleOriginBeforeDiscovery) "$case-discovery-and-first-nochange-preserve-settle-origin"
                    $exposure = @($parameters.parameters | Where-Object id -eq 'view.exposure.ev100')[0]
                    [double]$baselineEv = $exposure.current
                    [double]$darkerEv = $baselineEv + 2
                    Check ($baselineEv -ge $exposure.min -and $darkerEv -le $exposure.max) "$case-exposure-arms-fit-declared-range"
                    $valid = @(@{id='view.gtao.radius';type='float';value=3.0})
                    if ($content -eq 'lab') { $valid += @{id='gtao.radius';type='float';value=2.0} }
                    foreach ($bad in @(
                        @{id='view.exposure.ev100';type='float';value=25.0},
                        @{id='view.gtao.direction_count';type='uint';value=0},
                        @{id='view.taa.history_filter';type='enum';value=999},
                        @{id='unknown.host';type='bool';value=$true},
                        @{id='view.gtao.enabled';type='float';value=1.0}
                    )) {
                        $rejected = Set-Values ($valid + @($bad)) -AllowFailure
                        $after = Invoke-VerificationSession parameters $session
                        Check (-not $rejected.ok -and $after.revision -eq $parameters.revision -and
                            ($after.parameters | ConvertTo-Json -Depth 12 -Compress) -eq
                            ($parameters.parameters | ConvertTo-Json -Depth 12 -Compress)) "$case-atomic-rejection"
                    }
                    $values = $valid + @(
                        @{id='view.taa.enabled';type='bool';value=$false},
                        @{id='view.taa.history_rectification';type='enum';value=2},
                        @{id='view.taa.max_history_feedback';type='float';value=0.7},
                        @{id='view.gtao.temporal_accumulation';type='bool';value=$false},
                        @{id='view.exposure.ev100';type='float';value=$baselineEv},
                        @{id='view.exposure.compensation';type='float';value=0.0}
                    )
                    $done = Set-Values $values @{WaitConsumed=$true;KeepResult=$true}
                    Write-VerificationJson "$directory/mixed-result.json" $done
                    $expectedSource = if ($content -eq 'lab') {'mixed'} else {'host'}
                    Check ($done.batch.terminal -and $done.batch.succeeded -eq ($content -eq 'demo') -and -not $done.consumed -and
                        $done.batch.source -eq $expectedSource) "$case-terminal-result"
                    foreach ($item in $done.batch.items) {
                        if ($item.id -eq 'gtao.radius') {
                            Check ($item.state -eq 'Superseded' -and $null -eq $item.firstConsumed -and
                                $null -eq $item.published) "$case-masked-lab-has-no-consumption"
                            continue
                        }
                        Check ($item.state -eq 'NoChange' -or ($item.state -eq 'Consumed' -and
                            $item.requested -eq $item.applied -and $item.published -eq $item.applied -and
                            $item.firstConsumed -ge $item.published -and $item.identity.revision -eq $done.batch.revision)) "$case-four-stamps"
                    }
                    $busy = Set-Values $valid -AllowFailure
                    Check (-not $busy.ok -and $busy.error -match 'Busy') "$case-one-result-slot"
                    $null = Invoke-VerificationSession parameter-result $session @{BatchId=$done.batchId;Consume=$true}
                    $same = Set-Values $values @{WaitConsumed=$true}
                    Check ($same.consumed -and $same.batch.revision -eq $done.batch.revision -and
                        @($same.batch.items | Where-Object { ($_.id -eq 'gtao.radius' -and $_.state -ne 'Superseded') -or
                            ($_.id -ne 'gtao.radius' -and $_.state -ne 'NoChange') }).Count -eq 0) "$case-nochange"
                    $bright = Capture-View 'exposure-base' $baselineEv
                    $darker = Set-Values @(@{id='view.exposure.ev100';type='float';value=$darkerEv}) @{WaitConsumed=$true}
                    Check ($darker.batch.succeeded -and $darker.batch.items[0].state -eq 'Consumed') "$case-exposure-consumed"
                    $dark = Capture-View 'exposure-plus-two' $darkerEv
                    $a = New-Object Drawing.Bitmap($bright); $b = New-Object Drawing.Bitmap($dark)
                    try {
                        [double]$luminanceBefore=0; [double]$luminanceAfter=0; [int]$count=0
                        for ($y=0; $y -lt $a.Height; $y+=4) {
                            for ($x=0; $x -lt $a.Width; $x+=4) {
                                $pa=$a.GetPixel($x,$y); $pb=$b.GetPixel($x,$y)
                                $luminanceBefore += [int]$pa.R + [int]$pa.G + [int]$pa.B
                                $luminanceAfter += [int]$pb.R + [int]$pb.G + [int]$pb.B
                                $count++
                            }
                        }
                        Write-VerificationJson "$directory/exposure-response.json" @{sampledPixels=$count;before=$luminanceBefore;after=$luminanceAfter}
                        Check ($luminanceBefore -gt 0 -and $luminanceAfter -lt $luminanceBefore * 0.95) "$case-visible-exposure-response"
                    } finally { $a.Dispose(); $b.Dispose() }
                    # Guarded exact states chain from the last confirmed commit, never a fresh query.
                    $snapshot = Invoke-VerificationSession parameters $session
                    Write-VerificationJson "$directory/override-snapshot.json" $snapshot
                    $hostView = @($snapshot.parameters | Where-Object { $_.source -eq 'host' -and $_.id -like 'view.*' })
                    Check ($status.capabilities.hostViewOverrides.version -eq 1 -and
                        @($hostView | Where-Object overrideOperationSupported).Count -eq 25 -and
                        @($snapshot.parameters | Where-Object { $_.id -notlike 'view.*' -and ($_.overrideOperationSupported -or
                            $_.overrideOperationReason -ne $(if ($_.source -eq 'lab') {'lab-parameter'} else {'lighting-profile'})) }).Count -eq 0) "$case-override-discovery"
                    $taa = @($hostView | Where-Object id -eq 'view.taa.enabled')[0]
                    $directions = @($hostView | Where-Object id -eq 'view.gtao.direction_count')[0]
                    $original = @((Get-OverrideState $taa), (Get-OverrideState $directions))
                    $arm = Set-Overrides $snapshot.controlState @(
                        @{id='view.taa.enabled';type='bool';present=$true;value=(-not $taa.resolved.value)},
                        @{id='view.gtao.direction_count';type='uint';present=$true;value=$directions.resolved.value})
                    Write-VerificationJson "$directory/override-arm.json" $arm
                    $armCommit = $arm.batch.controlCommit
                    Check ($arm.batch.succeeded -and $arm.consumed -and $armCommit.changed -and
                        @($arm.batch.items | Where-Object { $_.id -eq 'view.taa.enabled' -and $_.state -eq 'Consumed' }).Count -eq 1 -and
                        @($arm.batch.items | Where-Object { $_.id -eq 'view.gtao.direction_count' -and $_.state -eq 'NoChange' -and
                            $null -eq $_.firstConsumed }).Count -eq 1 -and
                        $armCommit.before.hostViewRevision -eq $snapshot.controlState.hostViewRevision -and
                        $armCommit.after.hostViewRevision -eq ($armCommit.before.hostViewRevision + 1)) "$case-override-arm"
                    $stale = Set-Overrides $snapshot.controlState $original -AllowFailure
                    Check (-not $stale.ok -and $stale.errorCode -eq 'ExpectedHostViewRevisionMismatch' -and
                        $stale.controlState.hostViewRevision -eq $armCommit.after.hostViewRevision) "$case-override-stale-stamp-conflicts"
                    $restored = Set-Overrides $armCommit.after $original
                    Write-VerificationJson "$directory/override-restore.json" $restored
                    $after = Invoke-VerificationSession parameters $session
                    Write-VerificationJson "$directory/override-restored.json" $after
                    Check ($restored.batch.succeeded -and
                        $restored.batch.controlCommit.after.hostViewRevision -eq ($armCommit.after.hostViewRevision + 1) -and
                        $after.controlState.hostViewRevision -eq $restored.batch.controlCommit.after.hostViewRevision) "$case-override-restore-commit"
                    foreach ($state in $original) {
                        $now = @($after.parameters | Where-Object id -eq $state.id)[0]
                        $before = @($snapshot.parameters | Where-Object id -eq $state.id)[0]
                        Check ($now.hostOverride.present -eq $state.present -and
                            (-not $state.present -or $now.hostOverride.value -eq $state.value) -and
                            $now.resolved.value -eq $before.resolved.value -and
                            $now.resolved.source -eq $before.resolved.source) "$case-override-restored-$($state.id)"
                    }
                    $null = Capture-View 'override-restored' $darkerEv
                    # A required frame state is decided before issue and published beside its evidence.
                    $live = Invoke-VerificationSession status $session
                    Write-VerificationJson "$directory/required-state-status.json" $live
                    $taaFrame = @($live.frame.hostView | Where-Object id -eq 'view.taa.enabled')[0]
                    $taaPlan = @($live.frame.temporal.consumers | Where-Object name -eq 'temporal-aa')[0]
                    function New-RequiredState([bool]$Value, [long]$MinRevision, [switch]$ValuesOnly) {
                        $state = [ordered]@{
                            runIdentity=$after.controlState.runIdentity; contentIdentity=$after.controlState.contentIdentity
                            minHostViewRevision=$MinRevision
                            values=@(@{id='view.taa.enabled';type='bool';value=$Value;source=$taaFrame.source})
                            referenceSampling=$false }
                        if (-not $ValuesOnly) {
                            $state.temporalConsumers = @(@{consumer='temporal-aa';status=$taaPlan.status;disableReason=$taaPlan.disableReason})
                        }
                        return ConvertTo-Json -Compress -Depth 8 -InputObject $state
                    }
                    $revision = [long]$after.controlState.hostViewRevision
                    $predicate = Invoke-VerificationSession capture $session @{SettleFrames=8;OutputDirectory=$directory;
                        Label='required-state';TimeoutSeconds=$TimeoutSeconds;RequiredFrameState=(New-RequiredState $taaFrame.value $revision)}
                    Write-VerificationJson "$directory/required-state-receipt.json" $predicate
                    $sidecar = Get-Content (Get-VerificationCaptureFile $predicate '.json') -Raw | ConvertFrom-Json
                    Check ($live.frame.controlState.hostViewRevision -eq $revision -and $predicate.status -eq 'completed' -and
                        $predicate.publication -eq 'Published' -and $null -eq $predicate.failureCode -and
                        $sidecar.control.schemaVersion -eq 1 -and $sidecar.control.requiredFrameState.minHostViewRevision -eq $revision -and
                        $sidecar.control.hostViewControl.hostViewRevision -ge $revision -and
                        @($sidecar.control.hostView | Where-Object { $_.id -eq 'view.taa.enabled' -and $_.value -eq $taaFrame.value }).Count -eq 1) "$case-required-state-publishes-with-control"
                    # The observed consumer state follows the observed value, so pairing it with the
                    # other value can never be resolved and rejects before a job exists.
                    $contradictory = Invoke-VerificationSession capture $session @{OutputDirectory=$directory;Label='required-state-contradictory';
                        NoWait=$true;RequiredFrameState=(New-RequiredState (-not $taaFrame.value) $revision)} -AllowFailure
                    Check (-not $contradictory.ok -and $contradictory.reason -eq 'RequiredFrameStateInvalid') "$case-required-state-contradiction-rejects"
                    $mismatch = Invoke-VerificationSession capture $session @{SettleFrames=8;OutputDirectory=$directory;
                        Label='required-state-mismatch';TimeoutSeconds=$TimeoutSeconds;
                        RequiredFrameState=(New-RequiredState (-not $taaFrame.value) $revision -ValuesOnly)}
                    Write-VerificationJson "$directory/required-state-mismatch-receipt.json" $mismatch
                    Check ($mismatch.status -eq 'failed' -and $mismatch.failureCode -eq 'RequiredFrameStateMismatch' -and
                        $mismatch.publication -ne 'Published' -and $mismatch.failure -match 'view\.taa\.enabled value') "$case-required-state-mismatch-fails-before-issue"
                    $unreachable = Invoke-VerificationSession capture $session @{OutputDirectory=$directory;Label='required-state-future';NoWait=$true;
                        RequiredFrameState=(New-RequiredState $taaFrame.value ($revision + 1))} -AllowFailure
                    Check (-not $unreachable.ok -and $unreachable.reason -eq 'RequiredFrameStateUnavailable') "$case-required-state-unreachable-revision-rejects"
                    $null = Invoke-VerificationSession sequence $session @{CameraPath='unused';RequiredContentId='Missing.Host.Content';NoWait=$true}
                    $during = Set-Values $valid -AllowFailure
                    Check (-not $during.ok -and $during.error -match 'sequence') "$case-sequence-exclusion"
                    $null = Invoke-VerificationSession sequence-cancel $session
                } catch { $caseFailure=$_; throw }
                finally {
                    if ($start) {
                        try {$null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit}
                        catch {if ($null -eq $caseFailure) {throw};[Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")}
                    }
                }
                $log = Get-Content -LiteralPath $start.outputLog -Raw
                if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') {throw "Native validation errors: $($start.outputLog)"}
                $cases += @{configuration=$config;backend=$backend;content=$content;passed=$true;directory=$directory}
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch {$result=@{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message}}
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) {exit 1}
