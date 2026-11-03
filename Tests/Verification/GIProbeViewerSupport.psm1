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
    [bool]$Images = $true, [int]$TimeoutSeconds = 600) {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $products = @(@{kind='graph'})
    if ($Images) { $products += @(@{kind='image';source='scene'},@{kind='image';source='composited'}) }
    if ($Enabled -and $Images) {
        $products += @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},
            @{resource='GI.Probes.RenderedIDs'},@{resource='GI.Field.Classification'})}
    }
    $result = Invoke-VerificationSession capture $Session @{ExpectedRunIdentity=$Run;Timing='next-frame';
        SettleFrames=$SettleFrames;Products=(ConvertTo-Json -InputObject @($products) -Depth 8 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Directory/receipt.json" $result
    if ($result.status -ne 'completed' -or $result.publication -ne 'Published') { throw 'Viewer evidence did not publish.' }
    return $result
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
    Set-ProbeViewerCase,Invoke-ProbeViewerCapture,Stop-ProbeViewer
