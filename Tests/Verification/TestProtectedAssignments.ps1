<#
.SYNOPSIS
Qualifies guarded Lab/profile assignments, conflicts and submitted effects.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/ProtectedAssignments' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
function Check([bool]$Condition, [string]$Name) {
    $script:checks += @{name=$Name;passed=$Condition}
    if (-not $Condition) { throw "Failed: $Name" }
}
function Discover { Invoke-VerificationSession parameters $script:session }
function Guard([string]$Domain) {
    $discovery = Discover
    $stamp = $discovery.domainControlStates.PSObject.Properties[$Domain].Value
    if ($null -eq $stamp) { throw "Unavailable control domain: $Domain" }
    return $stamp
}
function Assign-Values($Values, $Expected, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $arguments = @{ Changes=(ConvertTo-Json -InputObject @($Values) -Compress -Depth 8)
        ExpectedState=($Expected | ConvertTo-Json -Compress -Depth 8); TimeoutSeconds=$TimeoutSeconds }
    foreach ($key in $Options.Keys) { $arguments[$key] = $Options[$key] }
    Invoke-VerificationSession assign $script:session $arguments -AllowFailure:$AllowFailure
}
function Check-Done($Response, [string]$Name) {
    Check ($Response.batch.terminal -and $Response.batch.succeeded -and
        $Response.batch.domainControl.committed -and $null -eq $Response.batch.domainControl.conflict) "$Name-committed"
    $item = $Response.batch.items[0]
    Check ($item.state -eq 'Consumed' -and $item.firstConsumed -ge $item.published -and
        $item.published -ge $item.applied) "$Name-submitted-effect"
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($kind in @('atmo', 'rebuild')) {
                $case = "$config-$backend-$kind"
                $directory = Join-Path $output $case
                $null = New-Item -ItemType Directory -Path $directory
                $script:session = "protected-$runId-$case"
                $start = $null; $failure = $null
                try {
                    $lab = if ($kind -eq 'atmo') { 'gglab.lab.atmosphere_range' } else { 'gglab.lab.forward_plus' }
                    $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;Lab=$lab
                        WindowSize='640x360';NoDevTools=$true;Validation='standard';StateRoot="$directory/state"
                        StartTimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/start.json" $start
                    $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                        -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
                    Check ($status.capabilities.protectedAssignments.command -eq 'assign' -and
                        -not $status.capabilities.protectedAssignments.mixedDomains) "$case-capability"
                    $before = Discover
                    Write-VerificationJson "$directory/discovery.json" $before
                    $guard = $before.domainControlStates.lab
                    Check ($guard.runIdentity -eq $start.runIdentity -and $guard.activationRevision -gt 0 -and
                        $guard.labRevision -gt 0 -and $guard.valuesRevision -gt 0) "$case-owner-identity"
                    if ($kind -eq 'atmo') {
                        $id = 'atmosphere_range.aerial_perspective'
                        $value = -not @($before.parameters | Where-Object id -eq $id)[0].current
                        $immediate = @(@{id=$id;type='bool';value=$value})
                        $done = Assign-Values $immediate $guard @{WaitConsumed=$true}
                        Write-VerificationJson "$directory/immediate.json" $done
                        Check-Done $done "$case-immediate"
                        $stale = Assign-Values $immediate $guard @{} -AllowFailure
                        Check (-not $stale.ok -and $stale.errorCode -eq 'ExpectedParameterControlMismatch') "$case-stale-nochange"
                        $current = Guard lab
                        $mixed = Assign-Values @($immediate[0],@{id='view.exposure.ev100';type='float';value=7.0}) `
                            $current @{} -AllowFailure
                        Check (-not $mixed.ok -and $mixed.errorCode -eq 'ParameterDomainUnsupported') "$case-single-domain"
                        $wrongRun = $current | ConvertTo-Json -Compress | ConvertFrom-Json
                        $wrongRun.runIdentity = 'ffffffffffffffffffffffffffffffff'
                        $restarted = Assign-Values $immediate $wrongRun @{} -AllowFailure
                        Check (-not $restarted.ok -and $restarted.errorCode -eq 'ExpectedRunChanged') "$case-run-check"

                        $sampleId = 'atmosphere_range.ibl.sample_count'
                        $sample = @((Discover).parameters | Where-Object id -eq $sampleId)[0].current
                        $target = if ($sample -eq 32) { 64 } else { 32 }
                        $values = @(@{id=$sampleId;type='uint';value=$target})
                        $admission = Assign-Values $values (Guard lab) @{NoWait=$true}
                        Write-VerificationJson "$directory/deferred-admission.json" $admission
                        Check (-not $admission.batch.domainControl.committed -and
                            $admission.batch.items.Count -eq 0) "$case-no-predicted-effect"
                        $busy = Assign-Values $values (Guard lab) @{} -AllowFailure
                        Check (-not $busy.ok -and $busy.errorCode -eq 'Busy') "$case-retained-slot"
                        $done = Wait-VerificationCondition -Poll {
                            Invoke-VerificationSession parameter-result $session @{BatchId=$admission.batchId}
                        } -Until {param($r) $r.batch.terminal} -TimeoutSeconds $TimeoutSeconds
                        Write-VerificationJson "$directory/deferred.json" $done
                        Check-Done $done "$case-deferred"
                        $null = Invoke-VerificationSession parameter-result $session @{BatchId=$admission.batchId;Consume=$true}

                        $sunId = 'atmosphere_range.sun.elevation_degrees'
                        $degrees = @((Discover).parameters | Where-Object id -eq $sunId)[0].current
                        $sun = if ($degrees -eq 25.0) { 35.0 } else { 25.0 }
                        $sunGuard = Guard lab
                        $done = Assign-Values @(@{id=$sunId;type='float';value=$sun}) $sunGuard @{WaitConsumed=$true}
                        Write-VerificationJson "$directory/sun.json" $done
                        Check-Done $done "$case-sun"
                        Check ($done.batch.domainControl.after.worldRevision -gt $sunGuard.worldRevision) "$case-world-intent"
                        $oldLab = Guard lab
                        $selection = Assign-Values @(@{id='environment.profile';type='enum';value=1}) `
                            (Guard lighting-profile) @{WaitConsumed=$true}
                        Write-VerificationJson "$directory/profile.json" $selection
                        Check-Done $selection "$case-profile"
                        $discovery = Discover
                        Check ($discovery.lighting.active.profileId -eq 'atmosphere_range.oblique' -and
                            $discovery.lighting.active.generation -eq $selection.batch.items[0].identity.generation) "$case-profile-generation"
                        $shared = Assign-Values $immediate $oldLab @{} -AllowFailure
                        Check (-not $shared.ok -and $shared.errorCode -eq 'ExpectedParameterControlMismatch') "$case-shared-world-conflict"
                        $same = Assign-Values @(@{id='environment.profile';type='enum';value=1}) `
                            (Guard lighting-profile) @{WaitConsumed=$true}
                        Write-VerificationJson "$directory/nochange.json" $same
                        Check ($same.batch.succeeded -and $same.batch.items[0].state -eq 'NoChange' -and
                            ($same.batch.domainControl.before | ConvertTo-Json -Compress) -eq
                            ($same.batch.domainControl.after | ConvertTo-Json -Compress)) "$case-nochange-control"
                        $revision = $selection.batch.revision
                        $expectedContent = $discovery.domainControlStates.lab.contentIdentity
                        $expectedGeneration = $selection.batch.items[0].identity.generation
                        $expectedConsumed = $selection.batch.items[0].firstConsumed
                    } else {
                        $id = 'forward_plus.fixture'
                        $value = if (@($before.parameters | Where-Object id -eq $id)[0].current -eq 0) { 1 } else { 0 }
                        $values = @(@{id=$id;type='enum';value=$value})
                        $done = Assign-Values $values $guard @{WaitConsumed=$true}
                        Write-VerificationJson "$directory/rebuild.json" $done
                        Check-Done $done "$case-rebuild"
                        Check ($done.batch.items[0].declaredClass -eq 'Rebuild' -and
                            $done.batch.domainControl.before.contentIdentity -eq $guard.contentIdentity -and
                            $done.batch.domainControl.after.contentIdentity -eq $guard.contentIdentity) "$case-source-commit"
                        $fresh = Guard lab
                        Check ($fresh.contentIdentity -ne $guard.contentIdentity -and
                            $fresh.activationRevision -gt $guard.activationRevision) "$case-target-activation"
                        $stale = Assign-Values $values $guard @{} -AllowFailure
                        Check (-not $stale.ok -and $stale.errorCode -eq 'ExpectedContentChanged') "$case-old-activation-rejected"
                        $same = Assign-Values $values $fresh @{WaitConsumed=$true}
                        Check ($same.batch.succeeded -and $same.batch.items[0].state -eq 'NoChange' -and
                            $null -eq $same.batch.items[0].firstConsumed) "$case-rebuild-nochange"
                        $revision = $done.batch.revision
                        $expectedContent = $fresh.contentIdentity
                        $expectedGeneration = $null
                        $expectedConsumed = $done.batch.items[0].firstConsumed
                    }
                    $capture = Invoke-VerificationSession capture $session @{SettleFrames=2;RequiredContentId=$lab
                        Label='protected';OutputDirectory=$directory;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/capture.json" $capture
                    $metadata = Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
                    Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published' -and
                        $metadata.control.hostViewControl.contentIdentity -eq $expectedContent -and
                        $metadata.parameters.revision -ge $revision -and
                        $capture.frameSerial -ge $expectedConsumed) "$case-published-target-frame"
                    if ($null -ne $expectedGeneration) {
                        Check ($metadata.sceneInputs.environment.activeIBLGeneration -eq $expectedGeneration) "$case-frozen-lighting"
                    }
                    $cases += @{configuration=$config;backend=$backend;kind=$kind;passed=$true;directory=$directory}
                } catch { $failure=$_; throw }
                finally {
                    if ($start) {
                        try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                        catch { if ($null -eq $failure) { throw } }
                    }
                }
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result=@{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 12))
if (-not $result.ok) { exit 1 }
