<#
.SYNOPSIS
Runs a guarded hidden TAA A/B experiment and restores exact Host override states.
.DESCRIPTION
Creates a fresh automation session by default. -Attach uses an existing
session and never stops it. The fixed lighting-contract fixture must pass preflight.
Every mutation and confirmed handle is journaled before the next operation. Lost
admission or consumption responses stop automatic mutation; journal.json retains
the recovery evidence. Pixel measurements do not determine the experiment verdict.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/Invoke-TaaComparison.ps1 -Configuration Develop -Rhi dx12
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Develop',
    [ValidateSet('dx12', 'vulkan')][string]$Rhi = 'dx12',
    [ValidatePattern('^[A-Za-z0-9_-]{1,64}$')][string]$Session,
    [switch]$Attach,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [ValidateRange(1, 300)][int]$TimeoutSeconds = 120,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $PSScriptRoot 'VerificationSupport.psm1')

function Get-TaaField($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    if ($Object -is [Collections.IDictionary]) { return $Object[$Name] }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}
function Test-TaaEqual($Left, $Right) {
    if ($null -eq $Left -or $null -eq $Right) { return $null -eq $Left -and $null -eq $Right }
    if ($Left -is [Collections.IDictionary] -or $Left -is [pscustomobject]) {
        $leftKeys = if ($Left -is [Collections.IDictionary]) { @($Left.Keys) } else { @($Left.PSObject.Properties.Name) }
        $rightKeys = if ($Right -is [Collections.IDictionary]) { @($Right.Keys) } else { @($Right.PSObject.Properties.Name) }
        if ((($leftKeys | Sort-Object) -join '|') -cne (($rightKeys | Sort-Object) -join '|')) { return $false }
        foreach ($key in $leftKeys) {
            if (-not (Test-TaaEqual (Get-TaaField $Left $key) (Get-TaaField $Right $key))) { return $false }
        }
        return $true
    }
    if ($Left -is [array] -or $Right -is [array]) {
        if (@($Left).Count -ne @($Right).Count) { return $false }
        for ($index=0; $index -lt @($Left).Count; ++$index) {
            if (-not (Test-TaaEqual $Left[$index] $Right[$index])) { return $false }
        }
        return $true
    }
    return $Left -ceq $Right
}
function Save-TaaJournal {
    $script:Taa.journal.updatedAtUtc = [DateTime]::UtcNow.ToString('o')
    $path = Join-Path $script:Taa.directory 'journal.json'
    Write-VerificationJson "$path.tmp" $script:Taa.journal
    Move-Item -LiteralPath "$path.tmp" -Destination $path -Force
}
function Invoke-TaaControl([string]$Command, [hashtable]$Options = @{}) {
    $optionsCopy = @{ TimeoutSeconds = $script:Taa.timeout }
    foreach ($key in $Options.Keys) { $optionsCopy[$key] = $Options[$key] }
    if ($script:Taa.journal.runIdentity -and $Command -notin @('start', 'status', 'parameters')) {
        $optionsCopy.ExpectedRunIdentity = $script:Taa.journal.runIdentity
    }
    return Invoke-VerificationSession $Command $script:Taa.journal.session $optionsCopy -AllowFailure
}
function Assert-TaaResponse($Response) {
    if (-not $Response.ok) { throw ($Response | ConvertTo-Json -Compress -Depth 12) }
}
function Assert-TaaRun($Stamp) {
    if ($Stamp.runIdentity -cne $script:Taa.journal.runIdentity) { throw 'ExpectedRunChanged: stop recovery against this endpoint.' }
}
function Get-TaaDescriptor($Parameters, [string]$Id) {
    $found = @($Parameters.parameters | Where-Object id -CEQ $Id)
    if ($found.Count -ne 1) { throw "Missing or duplicate parameter: $Id" }
    return $found[0]
}
function Get-TaaOverride($Descriptor) {
    $state = [ordered]@{ id=$Descriptor.id; type=$Descriptor.type; present=[bool]$Descriptor.hostOverride.present }
    if ($state.present) { $state.value = $Descriptor.hostOverride.value }
    return $state
}
function New-TaaOperation([string]$Kind, [string]$Name, $Intent) {
    $operation = [ordered]@{ kind=$Kind; name=$Name; intent=$Intent; phase='intent'; handle=$null
        result=$null; consumed=$false; cancellation=$null }
    $script:Taa.journal.operations += $operation
    Save-TaaJournal
    return $operation
}
function Set-TaaUncertain([string]$Reason) {
    $script:Taa.journal.uncertainty = $Reason
    Save-TaaJournal
    throw "RecoveryUncertain: $Reason"
}
function Receive-TaaResult($Operation, [string]$Command, [string]$IdName) {
    # Save the borrowed terminal evidence before consuming. Never retry a lost consume.
    $Operation.phase = 'consume-intent'
    Save-TaaJournal
    $options = @{ Consume=$true }; $options[$IdName] = $Operation.handle
    try { $consumed = Invoke-TaaControl $Command $options }
    catch { Set-TaaUncertain "Lost $Command consumption response for $($Operation.handle): $_" }
    if (-not $consumed.ok -or -not (Get-TaaField $consumed 'consumed')) {
        Set-TaaUncertain "Unconfirmed $Command consumption for $($Operation.handle)."
    }
    $Operation.consumed = $true
    $Operation.phase = 'collected'
    Save-TaaJournal
}
function Complete-TaaBatch($Operation, [switch]$AllowEffectFailure) {
    $deadline = [DateTime]::UtcNow.AddSeconds($script:Taa.timeout)
    do {
        $response = Invoke-TaaControl parameter-result @{ BatchId=$Operation.handle }
        Assert-TaaResponse $response
        if ($response.batch.terminal) { break }
        if ([DateTime]::UtcNow -ge $deadline) { throw "Parameter batch $($Operation.handle) is still pending." }
        Start-Sleep -Milliseconds 100
    } while ($true)
    $Operation.result = $response
    $Operation.phase = 'terminal'
    $commit = $response.batch.controlCommit
    if ($null -ne $commit.after) { $script:Taa.journal.lastStamp = $commit.after }
    Save-TaaJournal
    Receive-TaaResult $Operation parameter-result BatchId
    $script:Taa.journal.pendingBatch = $null
    Save-TaaJournal
    if (($null -eq $commit.after -or -not $response.batch.succeeded) -and -not $AllowEffectFailure) {
        throw "Override operation did not succeed: $($response | ConvertTo-Json -Compress -Depth 16)"
    }
    return $response
}
function Set-TaaOverrides([string]$Name, $States, [switch]$AllowEffectFailure) {
    $operation = New-TaaOperation override $Name @{ expected=$script:Taa.journal.lastStamp; overrides=@($States) }
    try {
        $response = Invoke-TaaControl host-view-overrides @{
            NoWait=$true; ExpectedState=($operation.intent.expected | ConvertTo-Json -Compress)
            Overrides=(ConvertTo-Json -InputObject @($States) -Compress -Depth 8) }
    } catch { Set-TaaUncertain "Lost override admission response: $_" }
    if (-not $response.ok) {
        $operation.result = $response
        if (-not (Get-TaaField $response 'errorCode')) { Set-TaaUncertain 'Override admission is unknown.' }
        $operation.phase = 'rejected'; Save-TaaJournal
        Assert-TaaResponse $response
    }
    if (-not (Get-TaaField $response 'batchId')) { Set-TaaUncertain 'Override admission returned no confirmed handle.' }
    $operation.handle = $response.batchId
    $operation.phase = 'admitted'
    $script:Taa.journal.pendingBatch = $operation
    Save-TaaJournal
    return Complete-TaaBatch $operation -AllowEffectFailure:$AllowEffectFailure
}
function Complete-TaaCapture($Operation, [switch]$Cancel) {
    $deadline = [DateTime]::UtcNow.AddSeconds($script:Taa.timeout)
    $cancelled = $false
    do {
        if ($Cancel -or [DateTime]::UtcNow -ge $deadline) {
            $Operation.phase = 'cancel-intent'; Save-TaaJournal
            # A missing cancellation response does not prove cancellation. Query the
            # known handle to recover its actual terminal outcome in the same run.
            try { $Operation.cancellation = Invoke-TaaControl capture-cancel @{ JobId=$Operation.handle } }
            catch { $Operation.cancellation = @{ ok=$false; error="$_" } }
            Save-TaaJournal
            $cancelled = (Get-TaaField $Operation.cancellation 'cancellation') -ceq 'Cancelled'
            $Cancel = $false
            $deadline = [DateTime]::UtcNow.AddSeconds($script:Taa.timeout)
            break
        }
        $response = Invoke-TaaControl result @{ JobId=$Operation.handle }
        Assert-TaaResponse $response
        if ($response.status -cne 'pending') { break }
        Start-Sleep -Milliseconds 100
    } while ($true)
    if ($null -ne $Operation.cancellation) {
        do {
            $response = Invoke-TaaControl result @{ JobId=$Operation.handle }
            Assert-TaaResponse $response
            if ($response.status -cne 'pending') { break }
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Capture cancellation/draining remains unconfirmed.' }
            Start-Sleep -Milliseconds 100
        } while ($true)
    }
    $Operation.result = $response; $Operation.phase = 'terminal'; Save-TaaJournal
    Receive-TaaResult $Operation result JobId
    $script:Taa.journal.pendingCapture = $null
    Save-TaaJournal
    if ($cancelled -and $response.publication -ceq 'Published') { throw 'Cancelled capture unexpectedly published.' }
    return $response
}
function Capture-TaaFrame([string]$Name, $Predicate) {
    $options = @{ NoWait=$true; SettleFrames=32; Timing='after-ready'; Label=$Name
        View='CAM_LightingSphere'
        RequiredContentId='gglab.lab.lighting_contract'; OutputDirectory=(Join-Path $script:Taa.directory $Name)
        Products='[{"kind":"image","source":"scene"},{"kind":"graph"}]' }
    if ($null -ne $Predicate) { $options.RequiredFrameState = $Predicate | ConvertTo-Json -Compress -Depth 12 }
    $operation = New-TaaOperation capture $Name $options
    try { $response = Invoke-TaaControl capture $options }
    catch { Set-TaaUncertain "Lost capture admission response: $_" }
    if (-not $response.ok) {
        $operation.result = $response
        if (-not (Get-TaaField $response 'admission') -and -not (Get-TaaField $response 'errorCode')) {
            Set-TaaUncertain 'Capture admission is unknown.'
        }
        $operation.phase = 'rejected'; Save-TaaJournal
        Assert-TaaResponse $response
    }
    if (-not (Get-TaaField $response 'jobId')) { Set-TaaUncertain 'Capture admission returned no confirmed handle.' }
    $operation.handle = $response.jobId; $operation.phase = 'admitted'
    $script:Taa.journal.pendingCapture = $operation; Save-TaaJournal
    $done = Complete-TaaCapture $operation
    if ($done.status -cne 'completed' -or $done.publication -cne 'Published') {
        throw "Capture $Name did not publish: $($done | ConvertTo-Json -Compress -Depth 8)"
    }
    return $done
}
function New-TaaPredicate([bool]$Enabled) {
    return [ordered]@{ runIdentity=$script:Taa.journal.runIdentity
        contentIdentity=$script:Taa.journal.lastStamp.contentIdentity
        minHostViewRevision=$script:Taa.journal.lastStamp.hostViewRevision
        values=@(@{ id='view.taa.enabled';type='bool';value=$Enabled;source='host' },
            @{ id='view.taa.resolution_preset';type='enum';value=$script:Taa.native;source='host' })
        temporalConsumers=@(@{ consumer='temporal-aa';status=$(if ($Enabled) {'active'} else {'disabled'}) })
        referenceSampling=$false }
}
function Assert-TaaPreflight($Status, $Parameters) {
    Assert-TaaRun $Parameters.controlState
    if ($Status.environment.process.runIdentity -cne $script:Taa.journal.runIdentity) { throw 'ExpectedRunChanged.' }
    $cap = $Status.capabilities
    if ($cap.hostViewControlState.version -ne 1 -or $cap.hostViewOverrides.version -ne 1 -or
        $cap.requiredFrameState.version -ne 1 -or $cap.requiredFrameState.controlSchemaVersion -ne 1 -or
        $cap.bundleSchemaVersion -ne 3 -or $cap.consistency -cne 'SameFrame' -or
        $cap.resultConsumption -cne 'explicit' -or $cap.hostViewOverrides.maxItems -lt 2 -or
        $cap.requiredFrameState.maxValues -lt 2 -or $cap.runIdentityChecks.version -ne 1) {
        throw 'Required control, predicate, SameFrame or result capabilities unavailable.'
    }
    foreach ($command in @('parameter-result', 'capture', 'result', 'capture-cancel', 'stop')) {
        if ($cap.runIdentityChecks.commands -cnotcontains $command) { throw "Missing run check: $command" }
    }
    foreach ($product in @('image', 'graph')) {
        if ($cap.requiredProducts -cnotcontains $product) { throw "Missing required product: $product" }
    }
    if ($cap.requiredFrameState.commands -cnotcontains 'capture' -or
        $cap.requiredFrameState.valueSources -cnotcontains 'host' -or
        $cap.requiredFrameState.temporalConsumers -cnotcontains 'temporal-aa' -or
        -not $cap.requiredFrameState.referenceSampling -or $cap.graphProductsPerJob -lt 1) {
        throw 'Required predicate or full-graph capability unavailable.'
    }
    if ($Status.frame.labId -cne 'gglab.lab.lighting_contract' -or
        $Status.frame.referenceViews -cnotcontains 'CAM_LightingSphere' -or
        (Get-TaaDescriptor $Parameters 'lighting_contract.scene.source').current -ne 0 -or
        (Get-TaaDescriptor $Parameters 'lighting_contract.physical_sun').current -ne $false) {
        throw 'The static lighting-contract fixture is not available.'
    }
    foreach ($id in @('view.taa.enabled', 'view.taa.resolution_preset')) {
        $descriptor = Get-TaaDescriptor $Parameters $id
        if (-not $descriptor.overrideOperationSupported -or $descriptor.source -cne 'host') {
            throw "Cannot restore the required target $id."
        }
    }
    $preset = Get-TaaDescriptor $Parameters 'view.taa.resolution_preset'
    $native = @($preset.enumItems | Where-Object name -CEQ 'native')
    if ($native.Count -ne 1 -or $preset.type -cne 'enum' -or
        (Get-TaaDescriptor $Parameters 'view.taa.enabled').type -cne 'bool') { throw 'Unsupported TAA parameter schema.' }
    $script:Taa.native = $native[0].value
}
function Assert-TaaFixture($Metadata) {
    if ($Metadata.content.labId -cne 'gglab.lab.lighting_contract' -or
        $Metadata.camera.referenceView -cne 'CAM_LightingSphere' -or $Metadata.developmentTools -ne $false -or
        $null -ne $Metadata.sequence -or $Metadata.image.width -ne 640 -or $Metadata.image.height -ne 360 -or
        [Math]::Abs($Metadata.time.fixedDeltaTime - 0.0166667) -gt 1e-10 -or
        $Metadata.temporal.settings.resolutionPreset -cne 'native' -or
        $Metadata.timing.mode -cne 'after-ready' -or $Metadata.timing.settleFrames -ne 32) {
        throw 'Capture does not match the locked static Native fixture.'
    }
}
function Assert-TaaGraph($Graph, [bool]$Enabled, [long]$FrameSerial) {
    if ($Graph.mode -cne 'full' -or $Graph.execution.status -cne 'recorded' -or
        $Graph.frameSerial -ne $FrameSerial -or -not $Graph.submission.completed) {
        throw 'Expected a full same-frame submitted graph with a recording execution record.'
    }
    $passes = @($Graph.compile.passes | Where-Object name -CEQ 'PostProcess.TemporalAA')
    $executed = @($passes | Where-Object { $Graph.execution.executedPassIndices -contains $_.index })
    if (($Enabled -and ($passes.Count -ne 1 -or $passes[0].culled -or $executed.Count -ne 1)) -or
        (-not $Enabled -and $executed.Count -ne 0)) { throw 'TAA resolve execution contradicts the experiment arm.' }
}
function Test-TaaBundle($Capture, $Predicate, [Nullable[bool]]$Enabled) {
    $validation = & $script:Taa.python (Join-Path $PSScriptRoot 'ValidateCaptureBundles.py') $Capture.manifest 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Capture bundle validation failed: $validation" }
    $image = Get-VerificationCaptureFile $Capture '.png' 0
    $sidecar = Get-VerificationCaptureFile $Capture '.json' 0
    $graphPath = Get-VerificationCaptureFile $Capture '.graph.json' 1
    $graphValidation = & $script:Taa.python (Join-Path $PSScriptRoot 'ValidateRenderGraphExports.py') $graphPath 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "Graph validation failed: $graphValidation" }
    $metadata = Get-Content -LiteralPath $sidecar -Raw | ConvertFrom-Json
    Assert-TaaFixture $metadata
    Assert-TaaRun $metadata.control.hostViewControl
    if ($null -ne $Predicate) {
        $saved = $metadata.control.requiredFrameState
        if ($saved.runIdentity -cne $Predicate.runIdentity -or
            $saved.contentIdentity -ne $Predicate.contentIdentity -or
            $saved.minHostViewRevision -ne $Predicate.minHostViewRevision -or
            -not (Test-TaaEqual $saved.values $Predicate.values) -or
            $saved.referenceSampling -ne $Predicate.referenceSampling) {
            throw 'Published evidence does not carry the admitted predicate.'
        }
        $expectedConsumers = @(Get-TaaField $Predicate 'temporalConsumers' | Where-Object { $null -ne $_ })
        $savedConsumers = @($saved.temporalConsumers)
        if ($savedConsumers.Count -ne $expectedConsumers.Count) { throw 'Published predicate consumer set changed.' }
        foreach ($consumer in $expectedConsumers) {
            $match = @($savedConsumers | Where-Object consumer -CEQ $consumer.consumer)
            if ($match.Count -ne 1 -or $match[0].status -cne $consumer.status) {
                throw 'Published predicate consumer requirement changed.'
            }
        }
    }
    $graph = Get-Content -LiteralPath $graphPath -Raw | ConvertFrom-Json
    if ($null -ne $Enabled) { Assert-TaaGraph $graph $Enabled $metadata.frame.serial }
    return @{ image=$image; sidecar=$sidecar; graph=$graphPath; manifest=$Capture.manifest
        frameSerial=$metadata.frame.serial; activeIBLGeneration=$metadata.sceneInputs.environment.activeIBLGeneration }
}
function Restore-TaaState {
    if ($script:Taa.journal.uncertainty) { throw 'RecoveryUncertain: automatic restoration stopped; inspect journal.json.' }
    if ($null -ne $script:Taa.journal.pendingCapture) {
        $null = Complete-TaaCapture $script:Taa.journal.pendingCapture -Cancel
    }
    if ($null -ne $script:Taa.journal.pendingBatch) {
        $null = Complete-TaaBatch $script:Taa.journal.pendingBatch -AllowEffectFailure
    }
    if ($null -eq $script:Taa.journal.snapshot) { return }
    $live = Invoke-TaaControl parameters; Assert-TaaResponse $live; Assert-TaaRun $live.controlState
    Write-VerificationJson (Join-Path $script:Taa.directory 'before-restore.json') $live
    $differences = @()
    foreach ($original in $script:Taa.journal.snapshot) {
        $current = Get-TaaOverride (Get-TaaDescriptor $live $original.id)
        if (-not (Test-TaaEqual $current $original)) {
            $differences += @{ original=$original; current=$current }
        }
    }
    $script:Taa.journal.cleanup.remainingDifferences = $differences
    Save-TaaJournal
    if ($live.controlState.contentIdentity -ne $script:Taa.journal.lastStamp.contentIdentity) {
        $present = @($live.parameters | Where-Object { (Get-TaaField $_ 'hostOverride') -and $_.hostOverride.present })
        $script:Taa.journal.cleanup.hostViewOverridesPresent = $present.Count -ne 0
        # Discovery cannot distinguish continuation from a new writer after discard.
        # Neither case grants this experiment authority over the new activation.
        $script:Taa.journal.cleanup.hostViewOverridesRetained = $null
        Save-TaaJournal
        if ($present.Count -eq 0) { throw 'HostOverridesDiscarded: content activation cleared the Host overrides.' }
        throw 'ExpectedContentChanged: current Host overrides remain; retention ownership is unverified; explicit recovery required.'
    }
    if ($live.controlState.hostViewRevision -ne $script:Taa.journal.lastStamp.hostViewRevision) {
        throw 'RestoreConflict: Host view changed since the last confirmed experiment commit.'
    }
    # Never replace the chained stamp with the just-observed one.
    $restored = Set-TaaOverrides restore $script:Taa.journal.snapshot -AllowEffectFailure
    $after = Invoke-TaaControl parameters; Assert-TaaResponse $after; Assert-TaaRun $after.controlState
    $values = @()
    foreach ($original in $script:Taa.journal.snapshot) {
        $descriptor = Get-TaaDescriptor $after $original.id
        $current = Get-TaaOverride $descriptor
        if (-not (Test-TaaEqual $current $original)) {
            throw 'RestoreConflict: override presence/value did not restore.'
        }
        $values += @{ id=$original.id;type=$original.type;value=$descriptor.resolved.value;source=$descriptor.resolved.source }
    }
    $script:Taa.journal.cleanup.intentRestored = $true
    $script:Taa.journal.cleanup.remainingDifferences = @()
    $script:Taa.journal.cleanup.effectsSucceeded = $restored.batch.succeeded
    $script:Taa.journal.cleanup.resolved = $values
    Save-TaaJournal
    $predicate = @{ runIdentity=$script:Taa.journal.runIdentity;contentIdentity=$script:Taa.journal.lastStamp.contentIdentity
        minHostViewRevision=$script:Taa.journal.lastStamp.hostViewRevision;values=$values;referenceSampling=$false }
    $proof = Capture-TaaFrame restored $predicate
    $script:Taa.journal.cleanup.evidence = Test-TaaBundle $proof $predicate $null
    $stamp = $proof.products[0].metadata.control.hostViewControl
    if ($stamp.hostViewRevision -ne $script:Taa.journal.lastStamp.hostViewRevision) {
        throw 'RestoreConflict: submitted restoration evidence belongs to a later Host revision.'
    }
    $script:Taa.journal.cleanup.renderingVerified = $true
    Save-TaaJournal
    if (-not $restored.batch.succeeded) { throw 'Host intent restored, but restoration effect receipts report failure.' }
}
function Stop-TaaOwnedSession {
    if ($script:Taa.journal.ownership -cne 'created' -or -not $script:Taa.journal.runIdentity) { return }
    $start = $script:Taa.journal.start
    if ($null -eq $start -or $start.runIdentity -cne $script:Taa.journal.runIdentity) {
        throw 'Process creation did not confirm ownership of this run; automatic stop refused.'
    }
    $stop = Invoke-TaaControl stop
    $script:Taa.journal.stop = $stop
    if (-not $stop.ok -or (Get-TaaField $stop 'exitCode') -ne 0) { throw 'Created run did not confirm a clean shutdown.' }
}

$script:Taa = $null
$result = @{ ok=$false; directory=$null; error=$null; cleanupError=$null; stopError=$null }
try {
    if ($Attach -and -not $Session) { throw '-Attach requires -Session.' }
    $runId = [Guid]::NewGuid().ToString('N').Substring(0, 10)
    if (-not $Session) { $Session = "taa-$runId" }
    if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/TaaComparison' }
    $directory = New-VerificationRunDirectory $OutputDirectory $RunDirectory $runId
    $result.directory = $directory
    $script:Taa = @{ directory=$directory; timeout=$TimeoutSeconds
        python=(Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        native=$null; journal=[ordered]@{ schema='gglab.taa-experiment';schemaVersion=1;session=$Session
            ownership=$(if ($Attach) {'attached'} else {'created'});runIdentity=$null;start=$null;updatedAtUtc=$null
            snapshot=$null;lastStamp=$null;operations=@();pendingCapture=$null;pendingBatch=$null;uncertainty=$null
            cleanup=@{ intentRestored=$false;renderingVerified=$false;remainingDifferences=@() };arms=@{} } }
    Save-TaaJournal
    # Resolve local validators and policy before touching Host view state.
    $script:Taa.journal.client = @{}
    foreach ($file in @('Invoke-TaaComparison.ps1', 'CompareCaptures.ps1', 'TaaComparison.policy.json')) {
        $script:Taa.journal.client[$file] = (Get-FileHash (Join-Path $PSScriptRoot $file) -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    Save-TaaJournal
    & $script:Taa.python -c 'import jsonschema' 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Python jsonschema is required for bundle validation.' }
    if (-not $Attach) {
        $start = Invoke-TaaControl start @{ Configuration=$Configuration;Rhi=$Rhi;Lab='gglab.lab.lighting_contract'
            WindowSize='640x360';NoDevTools=$true;Validation='standard';FixedDeltaTime=0.0166667
            StateRoot=(Join-Path $directory 'State');StartTimeoutSeconds=$TimeoutSeconds }
        Assert-TaaResponse $start
        $script:Taa.journal.start = $start
        # Missing start identity never grants stop authority over a later status run.
        $script:Taa.journal.runIdentity = $start.runIdentity
        Save-TaaJournal
        if ($start.runIdentity -notmatch '^[a-f0-9]{32}$') { throw 'Start did not confirm a process run identity.' }
        $result.executable = @{ path=(Join-Path $root "Build/Output/x64/$Configuration/GraphicsGadgetLab.exe") }
        $result.executable.sha256 = (Get-FileHash $result.executable.path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $status = Wait-VerificationCondition -Poll { Invoke-TaaControl status } -Until {
        param($s) $s.ok -and $s.frame -and $s.frame.ready } -TimeoutSeconds $TimeoutSeconds
    if ($Attach) { $script:Taa.journal.runIdentity = $status.environment.process.runIdentity }
    if ($script:Taa.journal.runIdentity -notmatch '^[a-f0-9]{32}$') { throw 'Missing process run identity.' }
    $parameters = Invoke-TaaControl parameters; Assert-TaaResponse $parameters
    Assert-TaaPreflight $status $parameters
    $script:Taa.journal.environment = $status.environment
    Save-TaaJournal
    $prime = Capture-TaaFrame prime $null
    $null = Test-TaaBundle $prime $null $null
    $snapshot = Invoke-TaaControl parameters; Assert-TaaResponse $snapshot
    Assert-TaaPreflight (Invoke-TaaControl status) $snapshot
    $script:Taa.journal.snapshot = @((Get-TaaOverride (Get-TaaDescriptor $snapshot 'view.taa.enabled')),
        (Get-TaaOverride (Get-TaaDescriptor $snapshot 'view.taa.resolution_preset')))
    $script:Taa.journal.lastStamp = $snapshot.controlState
    Save-TaaJournal
    $null = Set-TaaOverrides baseline @(@{id='view.taa.enabled';type='bool';present=$true;value=$true},
        @{id='view.taa.resolution_preset';type='enum';present=$true;value=$script:Taa.native})
    $onPredicate = New-TaaPredicate $true
    $on = Capture-TaaFrame on $onPredicate
    $script:Taa.journal.arms.on = Test-TaaBundle $on $onPredicate $true
    Save-TaaJournal
    $null = Set-TaaOverrides off @(@{id='view.taa.enabled';type='bool';present=$true;value=$false})
    $offPredicate = New-TaaPredicate $false
    $off = Capture-TaaFrame off $offPredicate
    $script:Taa.journal.arms.off = Test-TaaBundle $off $offPredicate $false
    Save-TaaJournal
    $comparison = Invoke-VerificationScript (Join-Path $PSScriptRoot 'CompareCaptures.ps1') @{
        Reference=$script:Taa.journal.arms.on.image;Candidate=$script:Taa.journal.arms.off.image
        RequireMatchingInputs=$true;AllowedDifferencePolicy=(Join-Path $PSScriptRoot 'TaaComparison.policy.json')
        DiffDirectory=(Join-Path $directory 'diff') }
    $result.comparison = $comparison.output | ConvertFrom-Json
    Write-VerificationJson (Join-Path $directory 'comparison.json') $result.comparison
    if ($comparison.exitCode -ne 0 -or -not $result.comparison.ok) { throw 'TAA arms have undeclared fixture differences; inspect comparison.json.' }
    $result.ok = $true
} catch { $result.error = $_.Exception.Message }
finally {
    if ($null -ne $script:Taa) {
        try { Restore-TaaState } catch { $result.ok=$false; $result.cleanupError=$_.Exception.Message }
        try { Stop-TaaOwnedSession } catch { $result.ok=$false; $result.stopError=$_.Exception.Message }
        $result.cleanup = $script:Taa.journal.cleanup
        $result.journal = Join-Path $script:Taa.directory 'journal.json'
        Save-TaaJournal
        Write-VerificationJson (Join-Path $script:Taa.directory 'result.json') $result
    }
}
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 32))
if (-not $result.ok) { exit 1 }
