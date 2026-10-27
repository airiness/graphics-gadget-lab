<#
.SYNOPSIS
Tests experiment recovery, ownership, predicates and execution evidence without a GPU.
#>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/TaaWorkflow' }
$output = New-VerificationRunDirectory $OutputDirectory '' ([Guid]::NewGuid().ToString('N').Substring(0,8))
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Reject([scriptblock]$Action, [string]$Pattern, [string]$Name) {
    try { $null = & $Action } catch { Check ($_.Exception.Message -match $Pattern) $Name; return }
    throw "Expected rejection: $Name"
}
# Load the production functions without executing its process-launch entry point.
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $root 'Scripts/Verification/Invoke-TaaComparison.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($definition in $ast.FindAll({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Reset-Scenario {
    $script:Taa = @{ directory=$output;timeout=1;native=0;journal=@{
        runIdentity=('a'*32);session='fixture';snapshot=$null;lastStamp=@{runIdentity=('a'*32);contentIdentity=8;hostViewRevision=4}
        uncertainty=$null;operations=@();pendingBatch=$null;pendingCapture=$null;cleanup=@{intentRestored=$false;renderingVerified=$false} } }
    $script:scenario = @{ calls=@();cancel='TooLate';status='completed';publication='Published';lostConsume=$false
        lostAdmission=$false;rejectAdmission=$null;lostCancel=$false;live=$null }
}
function Invoke-TaaControl([string]$Command, [hashtable]$Options=@{}) {
    $script:scenario.calls += @{command=$Command;options=$Options}
    if ($Command -eq 'capture-cancel') {
        if ($script:scenario.lostCancel) { throw 'Lost cancellation response' }
        return [pscustomobject]@{ok=$true;cancellation=$script:scenario.cancel}
    }
    if ($Command -eq 'result') {
        if (Get-TaaField $Options 'Consume') {
            $saved = Get-Content (Join-Path $output 'journal.json') -Raw | ConvertFrom-Json
            $script:scenario.savedBeforeConsume = $saved.operations[-1].phase -eq 'consume-intent' -and
                $saved.operations[-1].result.status -eq $script:scenario.status
        }
        if ((Get-TaaField $Options 'Consume') -and $script:scenario.lostConsume) { throw 'Lost consuming response' }
        return [pscustomobject]@{ok=$true;jobId=7;status=$script:scenario.status;publication=$script:scenario.publication
            consumed=[bool](Get-TaaField $Options 'Consume')}
    }
    if ($Command -eq 'host-view-overrides') {
        if ($script:scenario.lostAdmission) { throw 'Lost admission response' }
        if ($script:scenario.rejectAdmission) {
            return [pscustomobject]@{ok=$false;errorCode=$script:scenario.rejectAdmission;error='Changed by another writer'}
        }
        throw 'An override must not be submitted in this recovery case.'
    }
    if ($Command -eq 'parameters') { return $script:scenario.live }
    if ($Command -eq 'stop') { return [pscustomobject]@{ok=$true;exitCode=0} }
    throw "Unexpected control operation $Command."
}
try {
    Reset-Scenario
    Check (Test-TaaEqual @{present=$false;type='bool';id='test'} ([pscustomobject]@{id='test';type='bool';present=$false})) 'state-equality-ignores-object-property-order'
    Check (-not (Test-TaaEqual @{present=$false} @{present=$true;value=$false})) 'absent-and-explicit-equal-pin-are-distinct'
    foreach ($verdict in @('Cancelled','TooLate','Finished')) {
        Reset-Scenario
        $scenario.cancel=$verdict
        if ($verdict -eq 'Cancelled') { $scenario.status='cancelled';$scenario.publication='NotPublished' }
        if ($verdict -eq 'Finished') { $scenario.status='failed';$scenario.publication='NotPublished' }
        $op=New-TaaOperation capture test @{}; $op.handle=7; $Taa.journal.pendingCapture=$op
        $done=Complete-TaaCapture $op -Cancel
        Check ($done.status -eq $scenario.status -and $op.consumed -and $null -eq $Taa.journal.pendingCapture -and
            $op.cancellation.cancellation -eq $verdict) "cancel-$verdict-reads-actual-result-and-collects"
        $saved=Get-Content (Join-Path $output 'journal.json') -Raw | ConvertFrom-Json
        Check ($saved.operations[0].result.status -eq $scenario.status -and $saved.operations[0].consumed) "cancel-$verdict-journals-terminal-evidence"
    }
    Reset-Scenario
    $scenario.lostCancel=$true
    $op=New-TaaOperation capture test @{}; $op.handle=7
    $done=Complete-TaaCapture $op -Cancel
    Check ($done.publication -eq 'Published' -and -not $op.cancellation.ok -and $op.consumed) 'lost-cancel-does-not-claim-prevented-publication'
    Check $scenario.savedBeforeConsume 'terminal-evidence-is-durable-before-consuming'
    Reset-Scenario
    $Taa.timeout=0
    $op=New-TaaOperation capture timeout @{}; $op.handle=7
    $done=Complete-TaaCapture $op
    Check ($scenario.calls[0].command -eq 'capture-cancel' -and $done.publication -eq 'Published') 'timeout-too-late-recovers-actual-published-result'
    Reset-Scenario
    $scenario.lostConsume=$true
    $op=New-TaaOperation capture test @{}; $op.handle=7
    Reject { Complete-TaaCapture $op } 'RecoveryUncertain' 'lost-consuming-response-stops-recovery'
    Check ($Taa.journal.uncertainty -and -not $op.consumed -and
        @($scenario.calls | Where-Object {$_.command -eq 'result' -and (Get-TaaField $_.options 'Consume')}).Count -eq 1) 'consume-is-never-blindly-retried'
    $Taa.journal.pendingCapture=$op
    $callsBeforeRecovery=$scenario.calls.Count
    Reject { Restore-TaaState } 'RecoveryUncertain' 'uncertain-consumption-prevents-finally-retrying-owned-handle'
    Check ($scenario.calls.Count -eq $callsBeforeRecovery) 'uncertain-finally-performs-no-recovery-requests'
    Reset-Scenario
    $scenario.lostAdmission=$true
    Reject { Set-TaaOverrides on @(@{id='view.taa.enabled';type='bool';present=$true;value=$true}) } 'RecoveryUncertain' 'lost-admission-is-uncertain'
    Check ($scenario.calls.Count -eq 1 -and $Taa.journal.operations[0].phase -eq 'intent' -and
        $null -eq $Taa.journal.operations[0].handle) 'recovery-intent-precedes-unconfirmed-admission'
    Reject { Restore-TaaState } 'RecoveryUncertain' 'uncertain-admission-prevents-automatic-restore'
    Reset-Scenario
    $scenario.rejectAdmission='ExpectedHostViewRevisionMismatch'
    Reject { Set-TaaOverrides off @() } 'ExpectedHostViewRevisionMismatch' 'stale-mutation-is-rejected'
    Check ($Taa.journal.lastStamp.hostViewRevision -eq 4 -and -not $Taa.journal.uncertainty) 'rejection-never-rebases-stamp'
    Reset-Scenario
    $Taa.journal.snapshot=@(@{id='view.taa.enabled';type='bool';present=$false})
    $scenario.live=[pscustomobject]@{ok=$true;controlState=[pscustomobject]@{
        runIdentity=('a'*32);contentIdentity=8;hostViewRevision=6};parameters=@([pscustomobject]@{
        id='view.taa.enabled';type='bool';hostOverride=[pscustomobject]@{present=$false}})}
    Reject { Restore-TaaState } 'RestoreConflict' 'unrelated-or-aba-write-prevents-restore-even-when-target-matches'
    Check ($scenario.calls.Count -eq 1 -and $scenario.calls[0].command -eq 'parameters') 'restore-conflict-submits-no-mutation'
    $scenario.live.controlState.contentIdentity=9
    Reject { Restore-TaaState } 'HostOverridesDiscarded' 'new-content-reports-discarded-overrides'
    $scenario.live.parameters[0].hostOverride.present=$true
    $scenario.live.parameters[0].hostOverride | Add-Member value $true
    Reject { Restore-TaaState } 'ownership is unverified' 'new-content-with-overrides-requires-explicit-recovery'
    Check ($Taa.journal.cleanup.hostViewOverridesPresent -and
        $null -eq $Taa.journal.cleanup.hostViewOverridesRetained) 'discovery-never-invents-override-retention-provenance'
    $scenario.live.controlState.runIdentity=('b'*32)
    Reject { Restore-TaaState } 'ExpectedRunChanged' 'replacement-run-never-receives-restore'
    Reset-Scenario
    $predicate=New-TaaPredicate $true
    Check ($predicate.runIdentity -eq ('a'*32) -and $predicate.contentIdentity -eq 8 -and
        $predicate.minHostViewRevision -eq 4 -and $predicate.values[1].value -eq 0 -and
        $predicate.temporalConsumers[0].status -eq 'active' -and -not $predicate.referenceSampling) 'active-arm-predicate-pins-native-host-state'
    $graph=[pscustomobject]@{mode='full';frameSerial=12;submission=[pscustomobject]@{completed=$true}
        execution=[pscustomobject]@{status='recorded';executedPassIndices=@(2)}
        compile=[pscustomobject]@{passes=@([pscustomobject]@{index=2;name='PostProcess.TemporalAA';culled=$false})}}
    Assert-TaaGraph $graph $true 12
    $checks+='active-graph-requires-recorded-resolve'
    $graph.compile.passes[0].culled=$true
    Reject { Assert-TaaGraph $graph $true 12 } 'contradicts' 'listed-but-culled-pass-does-not-prove-execution'
    $graph.execution.executedPassIndices=@()
    Assert-TaaGraph $graph $false 12
    $checks+='disabled-graph-may-list-culled-resolve'
    Reject { Assert-TaaGraph $graph $true 12 } 'contradicts' 'nonexecuted-pass-does-not-prove-active-taa'
    $graph.mode='summary'
    Reject { Assert-TaaGraph $graph $false 12 } 'full same-frame' 'summary-graph-is-insufficient-evidence'
    $graph.mode='full'
    Reject { Assert-TaaGraph $graph $false 13 } 'full same-frame' 'other-frame-graph-is-insufficient-evidence'
    Reset-Scenario
    $preflightStatus = @{
        environment=@{process=@{runIdentity=('a'*32)}}
        frame=@{labId='gglab.lab.lighting_contract';referenceViews=@('CAM_LightingSphere')}
        capabilities=@{
            hostViewControlState=@{version=1};hostViewOverrides=@{version=1;maxItems=128}
            requiredFrameState=@{version=1;controlSchemaVersion=1;maxValues=128;commands=@('capture')
                valueSources=@('host');temporalConsumers=@('temporal-aa');referenceSampling=$true}
            bundleSchemaVersion=3;consistency='SameFrame';resultConsumption='explicit';graphProductsPerJob=1
            requiredProducts=@('image','graph');runIdentityChecks=@{version=1;commands=@('parameter-result','capture','result','capture-cancel','stop')} } }
    $preflightParameters=@{ controlState=$Taa.journal.lastStamp;parameters=@(
        @{id='view.taa.enabled';type='bool';source='host';overrideOperationSupported=$true},
        @{id='view.taa.resolution_preset';type='enum';source='host';overrideOperationSupported=$true;enumItems=@(@{name='native';value=0})},
        @{id='lighting_contract.scene.source';current=0},@{id='lighting_contract.physical_sun';current=$false}) }
    Assert-TaaPreflight $preflightStatus $preflightParameters
    $checks+='complete-preflight-accepts-supported-static-fixture'
    $preflightStatus.capabilities.hostViewOverrides.maxItems=1
    Reject { Assert-TaaPreflight $preflightStatus $preflightParameters } 'capabilities unavailable' 'preflight-requires-unsplit-restoration-capacity'
    $preflightStatus.capabilities.hostViewOverrides.maxItems=128
    $preflightParameters.parameters[0].overrideOperationSupported=$false
    Reject { Assert-TaaPreflight $preflightStatus $preflightParameters } 'Cannot restore' 'unsupported-restoration-target-rejects-before-mutation'
    $preflightParameters.parameters[0].overrideOperationSupported=$true
    $preflightParameters.parameters[2].current=1
    Reject { Assert-TaaPreflight $preflightStatus $preflightParameters } 'static lighting-contract' 'unapproved-scene-rejects-before-mutation'
    $preflightParameters.parameters[2].current=0
    $preflightStatus.capabilities.runIdentityChecks.commands=@('capture','result')
    Reject { Assert-TaaPreflight $preflightStatus $preflightParameters } 'Missing run check' 'preflight-requires-run-qualified-recovery'
    Check ($scenario.calls.Count -eq 0 -and $Taa.journal.operations.Count -eq 0) 'failed-preflight-admits-no-operations'
    Reset-Scenario
    $Taa.journal.ownership='attached';$Taa.journal.start=$null
    Stop-TaaOwnedSession
    Check ($scenario.calls.Count -eq 0) 'attached-session-is-never-automatically-stopped'
    $Taa.journal.ownership='created';$Taa.journal.start=[pscustomobject]@{runIdentity=('b'*32)}
    Reject { Stop-TaaOwnedSession } 'ownership' 'mismatched-start-metadata-does-not-grant-stop-authority'
    $Taa.journal.start.runIdentity=('a'*32)
    Stop-TaaOwnedSession
    Check ($scenario.calls.Count -eq 1 -and $scenario.calls[0].command -eq 'stop') 'only-created-confirmed-run-is-stopped'
    # Exercise restoration's separate intent and rendering claims. NoChange
    # effect receipts cannot substitute for a submitted-frame observation.
    & {
        Reset-Scenario
        $Taa.journal.snapshot=@(@{id='view.taa.enabled';type='bool';present=$false})
        $scenario.live=[pscustomobject]@{ok=$true;controlState=[pscustomobject]@{
            runIdentity=('a'*32);contentIdentity=8;hostViewRevision=4};parameters=@([pscustomobject]@{
            id='view.taa.enabled';type='bool';hostOverride=[pscustomobject]@{present=$false}
            resolved=[pscustomobject]@{value=$false;source='content'}})}
        $scenario.proofFail=$true; $scenario.effectSuccess=$true
        function Set-TaaOverrides([string]$Name,$States,[switch]$AllowEffectFailure) {
            return [pscustomobject]@{batch=[pscustomobject]@{succeeded=$scenario.effectSuccess}}
        }
        function Capture-TaaFrame([string]$Name,$Predicate) {
            return [pscustomobject]@{products=@([pscustomobject]@{metadata=[pscustomobject]@{control=[pscustomobject]@{
                hostViewControl=$Taa.journal.lastStamp}}})}
        }
        function Test-TaaBundle($Capture,$Predicate,$Enabled) {
            if ($scenario.proofFail) { throw 'No submitted restoration proof' }
            return @{frameSerial=42}
        }
        Reject { Restore-TaaState } 'No submitted' 'structural-restoration-requires-submitted-proof'
        Check ($Taa.journal.cleanup.intentRestored -and -not $Taa.journal.cleanup.renderingVerified) 'intent-restoration-is-reported-separately'
        $scenario.proofFail=$false
        Restore-TaaState
        Check ($Taa.journal.cleanup.intentRestored -and $Taa.journal.cleanup.renderingVerified) 'structural-restoration-accepts-matching-submitted-frame'
        $scenario.effectSuccess=$false
        Reject { Restore-TaaState } 'effect receipts report failure' 'postcommit-effect-failure-remains-failure'
        Check ($Taa.journal.cleanup.intentRestored -and $Taa.journal.cleanup.renderingVerified -and
            -not $Taa.journal.cleanup.effectsSucceeded) 'postcommit-failure-retains-intent-and-rendering-evidence'
    }
    $result=@{ok=$true;checks=$checks;directory=$output}
} catch { $result=@{ok=$false;checks=$checks;directory=$output;error=$_.Exception.Message;stack=$_.ScriptStackTrace} }
Write-VerificationJson (Join-Path $output 'result.json') $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 8))
if (-not $result.ok) { exit 1 }
