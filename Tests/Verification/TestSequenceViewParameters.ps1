<#
.SYNOPSIS
Verifies frame-local sequence assignments against shared Host view descriptors,
capture provenance and exact Host override state across completion and cancellation.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/SequenceViewParameters' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @(); $succeeded = $false
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Get-HostState($Parameters) {
    return ConvertTo-Json -Compress -Depth 12 -InputObject ([ordered]@{
        revision=$Parameters.revision; controlState=$Parameters.controlState
        overrides=@($Parameters.parameters | Where-Object source -eq 'host' |
            ForEach-Object { [ordered]@{id=$_.id;override=$_.hostOverride} })
    })
}
function Capture-Frame([string]$Label) {
    $capture = Invoke-VerificationSession capture $session @{SettleFrames=4;OutputDirectory=$directory;Label=$Label;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$directory/$Label-receipt.json" $capture
    Check ($capture.status -eq 'completed') "$case-$Label-completed"
    return Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $case = "$config-$backend"
            $directory = Join-Path $output $case
            $null = New-Item -ItemType Directory -Path $directory
            $session = "seq-view-$runId-$case"
            $start = $null; $failure = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.temporal_aa';WindowSize='640x360';NoDevTools=$true;Validation='standard';
                    StateRoot="$output/state-$case";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                $ready = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session} `
                    -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                Check ($ready.protocol -eq 5 -and $ready.capabilities.sequenceViewParameters.field -eq 'parameters' -and
                    -not $ready.capabilities.sequenceViewParameters.persistent) "$case-capability"
                $parameters = Invoke-VerificationSession parameters $session
                $taa = @($parameters.parameters | Where-Object id -eq 'view.taa.enabled')[0]
                # Pin an inherited-equal value so loss of override presence cannot hide behind equality.
                $pinned = Invoke-VerificationSession host-view-overrides $session @{
                    ExpectedState=($parameters.controlState | ConvertTo-Json -Compress)
                    Overrides=(ConvertTo-Json -InputObject @(@{id=$taa.id;type=$taa.type;present=$true;value=$taa.current}) -Compress)
                    WaitConsumed=$true}
                Check ($pinned.batch.succeeded) "$case-equal-pin"
                $baseline = Invoke-VerificationSession parameters $session
                $baselineState = Get-HostState $baseline
                Write-VerificationJson "$directory/baseline.json" $baseline
                $ordinary = Capture-Frame 'before'
                $values = @{'view.taa.enabled'=$false;'view.taa.history_filter'=0;
                    'view.gtao.temporal_accumulation'=$false;'view.gtao.temporal_max_samples'=4;
                    'view.gtao.radius'=2;'view.exposure.compensation'=1.25}
                $valuesFile = Join-Path $directory 'parameters.json'
                Write-VerificationJson $valuesFile $values
                $done = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_TemporalAALab_MovingObject';
                    ParametersFile=$valuesFile;CaptureFrames='0,179';OutputDirectory=$directory;Label='overlay';TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/sequence.json" $done
                Check ($done.sequence.state -eq 'completed' -and $done.sequence.submittedFrames -eq 180) "$case-completed"
                $frames = @{}
                foreach ($file in Get-ChildItem -LiteralPath $directory -Filter 'overlay-*.json') {
                    $metadata = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
                    if ($metadata.PSObject.Properties['image'] -and $metadata.sequence) { $frames[[int]$metadata.sequence.frame]=$metadata }
                }
                foreach ($frame in @(0,179)) {
                    Check ($frames.ContainsKey($frame)) "$case-frame-$frame-present"
                    foreach ($id in $values.Keys) {
                        $entry = @($frames[$frame].control.hostView | Where-Object id -eq $id)[0]
                        Check ($entry.source -eq 'sequence' -and $entry.value -eq $values[$id]) "$case-frame-$frame-$id"
                    }
                    $entry = @($frames[$frame].control.hostView | Where-Object id -eq 'view.taa.enabled')[0]
                    Check ($entry.hostOverride.present -and $entry.hostOverride.value -eq $taa.current -and
                        -not $frames[$frame].temporal.requested -and $frames[$frame].temporal.gtao.radius -eq 2 -and
                        $frames[$frame].sceneInputs.exposure.compensationEV -eq 1.25) "$case-frame-$frame-effective-and-pinned"
                }
                Check ((Get-HostState (Invoke-VerificationSession parameters $session)) -eq $baselineState) "$case-completion-preserves-host-state"
                $after = Capture-Frame 'after-completion'
                foreach ($id in $values.Keys) {
                    $beforeEntry = @($ordinary.control.hostView | Where-Object id -eq $id)[0]
                    $afterEntry = @($after.control.hostView | Where-Object id -eq $id)[0]
                    Check ($afterEntry.source -eq $beforeEntry.source -and $afterEntry.value -eq $beforeEntry.value) "$case-unmask-$id"
                }
                # A missing content predicate holds an accepted overlay without applying it.
                $waiting = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_TemporalAALab_MovingObject';
                    RequiredContentId='Missing.SequenceView.Content';Parameters=($values | ConvertTo-Json -Compress);NoWait=$true}
                Check ($waiting.ok) "$case-cancellable-admission"
                $cancelled = Invoke-VerificationSession sequence-cancel $session
                Write-VerificationJson "$directory/cancelled.json" $cancelled
                $null = Capture-Frame 'after-cancellation'
                Check ((Get-HostState (Invoke-VerificationSession parameters $session)) -eq $baselineState) "$case-cancellation-preserves-host-state"
                foreach ($bad in @(@{'missing.view.id'=1},@{'view.taa.enabled'=1},
                    @{'view.gtao.temporal_max_samples'=0},@{'view.taa.history_filter'=7})) {
                    $rejected = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_TemporalAALab_MovingObject';
                        Parameters=($bad | ConvertTo-Json -Compress)} -AllowFailure
                    Check (-not $rejected.ok) "$case-invalid-assignments-rejected"
                }
                $overlap = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_TemporalAALab_MovingObject';
                    Parameters=($values | ConvertTo-Json -Compress);Events='[{"frame":0,"set":{"view.taa.enabled":true}}]'} -AllowFailure
                $reference = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_TemporalAALab_MovingObject';
                    Parameters=($values | ConvertTo-Json -Compress);ReferenceSamples=2} -AllowFailure
                Check (-not $overlap.ok -and -not $reference.ok -and
                    (Get-HostState (Invoke-VerificationSession parameters $session)) -eq $baselineState) "$case-conflicts-reject-without-mutation"
                $cases += @{name=$case;passed=$true;directory=$directory}
            } catch { $failure=$_.Exception.Message; throw }
            finally {
                if ($null -ne $start) {
                    $stop = Stop-VerificationSession $session $start "$directory/stop.json" -RequireCleanExit
                    Check ($stop.exitCode -eq 0) "$case-clean-stop"
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                    $log = Get-Content -LiteralPath $start.outputLog -Raw
                    Check ($log -notmatch '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') "$case-validation-log"
                }
                if ($failure) { $cases += @{name=$case;passed=$false;failure=$failure} }
            }
        }
    }
    $succeeded = $true
} finally {
    $result = @{schemaVersion=1;ok=$succeeded;directory=$output;checks=$checks;cases=$cases}
    Write-VerificationJson "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
}
