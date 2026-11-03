Set-StrictMode -Version 3.0
Import-Module "$PSScriptRoot/../../Scripts/Verification/VerificationSupport.psm1"

function Set-ProbeViewerValues($Session, $Run, $Values, $TimeoutSeconds = 600) {
    $result = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($Values) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $result.batch.succeeded) { throw 'Probe viewer inputs did not reach a submitted frame.' }
    return $result
}

function Initialize-ProbeViewer($Session, $Run, [int]$Fixture, $TimeoutSeconds = 600) {
    $values = @(
        @{id='gi_reference.fixture';type='enum';value=$Fixture},
        @{id='gi_reference.receiver_diagnostics';type='bool';value=$false},
        @{id='gi_reference.field_enabled';type='bool';value=$true},
        @{id='gi_reference.field_sampling';type='bool';value=$true},
        @{id='gi_reference.field_readiness';type='bool';value=$false},
        @{id='gi_reference.probe_enabled';type='bool';value=$true},
        @{id='gi_reference.probe_rays';type='uint';value=256},
        @{id='gi_reference.seed';type='uint';value=1},
        @{id='gi_reference.field_irradiance_width';type='enum';value=0},
        @{id='gi_reference.probe_allocation_diagnostics';type='bool';value=$false},
        @{id='view.taa.enabled';type='bool';value=$false},
        @{id='view.taa.resolution_preset';type='enum';value=0},
        @{id='view.gi_probes.enabled';type='bool';value=$false},
        @{id='view.gi_probes.bounds';type='bool';value=$false},
        @{id='view.gi_probes.radius';type='float';value=0.12},
        @{id='view.gi_probes.slice_axis';type='uint';value=0},
        @{id='view.gi_probes.layer_minimum';type='uint';value=0},
        @{id='view.gi_probes.layer_maximum';type='uint';value=65535},
        @{id='view.gi_probes.classification_mask';type='uint';value=15},
        @{id='view.gi_probes.maximum_distance';type='float';value=0.0},
        @{id='view.gi_probes.preview_ev';type='float';value=-4.0})
    $null = Invoke-VerificationSession select-gi-probe $Session @{ExpectedRunIdentity=$Run;ClearSelection=$true}
    $effect = Set-ProbeViewerValues $Session $Run $values $TimeoutSeconds
    return $effect
}

function Get-ProbeViewerCases([int[]]$Budgets = @(256,2048,4096,16384)) {
    $cases = @(@{id='off-before';kind='off';budget=2048;mode=0;depth=0;selected=$false})
    foreach ($budget in $Budgets) {
        foreach ($depth in @(0,1)) {
            foreach ($kind in @('classification','irradiance','selected')) {
                $cases += @{id="$kind-b$budget-d$depth";kind=$kind;budget=$budget;
                    mode=$(if ($kind -eq 'irradiance') {1} else {0});depth=$depth;selected=($kind -eq 'selected')}
            }
        }
    }
    $cases += @{id='off-after';kind='off';budget=2048;mode=0;depth=0;selected=$false}
    return $cases
}

function Set-ProbeViewerCase($Session, $Run, $Case, $TimeoutSeconds = 600) {
    $null = Set-ProbeViewerValues $Session $Run @(
        @{id='view.gi_probes.enabled';type='bool';value=($Case.kind -ne 'off')},
        @{id='view.gi_probes.budget';type='uint';value=$Case.budget},
        @{id='view.gi_probes.mode';type='enum';value=$Case.mode},
        @{id='view.gi_probes.depth_mode';type='enum';value=$Case.depth}) $TimeoutSeconds
    if ($Case.selected) {
        $status = Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run}
        $summary = $status.frame.giField.summary
        # The separate marker remains bounded to one probe, independently of array capacity.
        $coordinate = @($summary.counts | ForEach-Object { [int][Math]::Floor($_ / 2) })
        $selection = @{coordinate=$coordinate;expectedGeneration=$summary.field.generation}
        $null = Invoke-VerificationSession select-gi-probe $Session @{ExpectedRunIdentity=$Run;
            GIProbeSelection=($selection | ConvertTo-Json -Compress)}
    } else {
        $null = Invoke-VerificationSession select-gi-probe $Session @{ExpectedRunIdentity=$Run;ClearSelection=$true}
    }
}

function Invoke-ProbeViewerCapture($Session, $Run, $Directory, [bool]$Enabled, [int]$SettleFrames = 32,
    [bool]$Images = $true, [int]$TimeoutSeconds = 600, [string]$View = '') {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $products = @(@{kind='graph'})
    if ($Images) { $products += @(@{kind='image';source='scene'},@{kind='image';source='composited'}) }
    if ($Enabled -and $Images) {
        $products += @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},
            @{resource='GI.Probes.RenderedIDs'},@{resource='GI.Field.Classification'})}
    }
    $result = Invoke-VerificationSession capture $Session @{ExpectedRunIdentity=$Run;Timing='next-frame';
        View=$View;
        SettleFrames=$SettleFrames;Products=(ConvertTo-Json -InputObject @($products) -Depth 8 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Directory/receipt.json" $result
    if ($result.status -ne 'completed' -or $result.publication -ne 'Published') { throw 'Viewer evidence did not publish.' }
    return $result
}

function Get-ProbeViewerWarmupSample($Status, $Run) {
    if (-not $Status.PSObject.Properties['frame']) {
        throw 'Viewer warmup requires status submission evidence, not a capture receipt.'
    }
    if ($Status.environment.process.runIdentity -ne $Run) { throw 'Viewer warmup run identity changed.' }
    $frame = $Status.frame
    if (-not $frame -or -not $frame.ready -or $frame.outcome -ne 'submitted' -or
        -not $frame.giField -or -not $frame.giField.summary.initialized) { return $null }
    if (-not $Status.parameterControl.available) { throw 'Viewer warmup parameter revision is unavailable.' }
    $summary = $frame.giField.summary
    return [ordered]@{
        runIdentity = $Status.environment.process.runIdentity
        contentId = $(if ($frame.labId) { $frame.labId } else { $frame.demoId })
        parameterRevision = [uint64]$Status.parameterControl.revision
        controlState = $frame.controlState
        field = $summary.field
        viewRevision = [uint64]$summary.probeVisualization.view.revision
        visualization = $summary.probeVisualization.settings
        extent = @([int]$Status.session.width, [int]$Status.session.height)
        backend = $frame.backend
        frameSerial = [uint64]$frame.frameSerial
        settledFrames = [uint64]$frame.settledFrames
        outcome = $frame.outcome
        ready = [bool]$frame.ready
    }
}

function Wait-ProbeViewerWarmup {
    [CmdletBinding()]
    param([string]$Session, [string]$Run, [ValidateRange(1,3600)][int]$Frames,
        [string]$Directory, [int]$TimeoutSeconds = 600)
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $state = @{start=$null;previous=$null;binding=$null;samples=(New-Object 'Collections.Generic.List[object]')}
    $end = Wait-VerificationCondition -TimeoutSeconds $TimeoutSeconds -Poll {
        $sample = Get-ProbeViewerWarmupSample (Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run}) $Run
        if ($null -eq $sample) {
            if ($null -ne $state.start) { throw 'Viewer warmup lost a submitted Ready frame.' }
            return $null
        }
        $binding = [ordered]@{}
        foreach ($key in @('runIdentity','contentId','parameterRevision','controlState','field',
            'viewRevision','visualization','extent','backend')) { $binding[$key] = $sample[$key] }
        $bindingJson = ConvertTo-Json -InputObject $binding -Depth 16 -Compress
        if ($null -eq $state.start) {
            if ($sample.settledFrames + $Frames -gt [uint32]::MaxValue) { throw 'Viewer warmup counter would overflow.' }
            $state.start = $sample
            $state.binding = $bindingJson
        } else {
            if ($bindingJson -cne $state.binding) { throw 'Viewer warmup arm or observation binding changed.' }
            $serialDelta = [decimal]$sample.frameSerial - $state.previous.frameSerial
            $submittedDelta = [decimal]$sample.settledFrames - $state.previous.settledFrames
            if ($submittedDelta -lt 0 -or $serialDelta -lt $submittedDelta) {
                throw 'Viewer warmup submitted counter reset or contradicted frame serials.'
            }
        }
        $state.previous = $sample
        $state.samples.Add($sample)
        return $sample
    } -Until { param($sample)
        $null -ne $sample -and $sample.settledFrames - $state.start.settledFrames -ge $Frames
    }
    $evidence = [ordered]@{
        schema = 'gglab.probe-viewer-warmup'
        schemaVersion = 1
        method = 'submitted-settle-counter'
        requestedFrames = $Frames
        submittedFrames = $end.settledFrames - $state.start.settledFrames
        start = $state.start
        end = $end
        samples = @($state.samples.ToArray())
    }
    Write-VerificationJson "$Directory/submitted-frames.json" $evidence
    return $evidence
}

function Stop-ProbeViewer($Session, $Start, $Directory) {
    if ($Start -and $Start.ownership -eq 'created') {
        $null = Stop-VerificationSession $Session -StartedSession $Start -ReceiptPath "$Directory/stop.json" -RequireCleanExit
        Copy-Item -LiteralPath $Start.outputLog -Destination "$Directory/output.log"
        $log = [IO.File]::ReadAllText($Start.outputLog).Replace([string][char]0,'')
        if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') {
            throw "Probe viewer native validation failed: $Directory/output.log"
        }
    }
}

Export-ModuleMember -Function Set-ProbeViewerValues,Initialize-ProbeViewer,Get-ProbeViewerCases,
    Set-ProbeViewerCase,Invoke-ProbeViewerCapture,Wait-ProbeViewerWarmup,Stop-ProbeViewer
