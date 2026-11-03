<# Generation-bound selected-probe overlays and ordinary-budget isolation. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Probe selection requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeSelection" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Set-Values($Session, $Run, $Values) {
    $r = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($Values) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $r.batch.succeeded) { throw 'Probe selection parameter batch failed.' }
}
function Capture($Session, $Run, $Directory, $Products, $View='') {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $options = @{ExpectedRunIdentity=$Run;Timing='next-frame';SettleFrames=16;
        Products=(ConvertTo-Json -InputObject $Products -Depth 8 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    if ($View) { $options.View=$View }
    $r = Invoke-VerificationSession capture $Session $options
    Write-VerificationJson "$Directory/receipt.json" $r
    if ($r.status -ne 'completed' -or $r.publication -ne 'Published') {
        throw ('Probe selection capture did not publish: '+($r | ConvertTo-Json -Depth 12 -Compress))
    }
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    $fixtures = @(@{id='f4';value=4;view='F4_Floor';category=1},
        @{id='inactive';value=18;view='Contract_FieldUnsupported';category=2},
        @{id='black';value=19;view='F2_Interior';category=1})
    Write-VerificationJson "$output/fixtures.json" $fixtures
    foreach ($backend in $Rhi) {
        $session = 'gi-selection-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        $primaryFailure = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Probe selection requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            $previous = $null
            foreach ($fixture in $fixtures) {
                $directory = "$output/$backend/$($fixture.id)"
                $null = New-Item -ItemType Directory -Path $directory -Force
                Set-Values $session $run @(
                    @{id='gi_reference.fixture';type='enum';value=$fixture.value},
                    @{id='gi_reference.receiver_diagnostics';type='bool';value=$false},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$false},
                    @{id='view.taa.enabled';type='bool';value=$false},
                    @{id='view.taa.resolution_preset';type='enum';value=0},
                    @{id='view.gi_probes.enabled';type='bool';value=$true},
                    @{id='view.gi_probes.bounds';type='bool';value=$false},
                    @{id='view.gi_probes.radius';type='float';value=0.45},
                    @{id='view.gi_probes.budget';type='uint';value=7},
                    @{id='view.gi_probes.slice_axis';type='uint';value=0},
                    @{id='view.gi_probes.classification_mask';type='uint';value=15},
                    @{id='view.gi_probes.maximum_distance';type='float';value=0.0},
                    @{id='view.gi_probes.mode';type='enum';value=0},
                    @{id='view.gi_probes.depth_mode';type='enum';value=0})
                $products = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'},
                    @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},@{resource='GI.Probes.RenderedIDs'},
                        @{resource='GI.Field.Classification'},@{resource='GI.Probes.PrivateDepth'},@{resource='DisplayView.DepthBuffer'})})
                if ($previous) {
                    Capture $session $run "$directory/replacement" $products $fixture.view
                    $rejected = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;GIProbeSelection=($previous | ConvertTo-Json -Compress)} -AllowFailure
                    Write-VerificationJson "$directory/stale-rejected.json" $rejected
                    if ($rejected.ok -or $rejected.error -ne 'GIProbeSelectionIdentityMismatch') { throw 'A stale field selection was admitted.' }
                }
                $null = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;ClearSelection=$true}
                Capture $session $run "$directory/ordinary" $products $fixture.view
                $choiceText = & $Python "$PSScriptRoot/ValidateGIProbeSelection.py" --choose "$directory/ordinary" --category $fixture.category
                if ($LASTEXITCODE -ne 0) { throw 'No visible non-budget probe is available for selection.' }
                $choice = $choiceText | ConvertFrom-Json
                Write-VerificationJson "$directory/choice.json" $choice
                $selection = $choice.selection
                $selectionJson = $selection | ConvertTo-Json -Compress
                $selected = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;GIProbeSelection=$selectionJson}
                Write-VerificationJson "$directory/select.json" $selected
                Capture $session $run "$directory/selected" $products $fixture.view
                $invalidSelection = @{coordinate=$selection.coordinate;expectedGeneration=([uint64]$selection.expectedGeneration+1)}
                $invalid = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;GIProbeSelection=($invalidSelection | ConvertTo-Json -Compress)} -AllowFailure
                Write-VerificationJson "$directory/invalid-rejected.json" $invalid
                if ($invalid.ok -or $invalid.error -ne 'GIProbeSelectionIdentityMismatch') { throw 'An invalid selection changed the accepted request.' }
                Capture $session $run "$directory/after-rejection" $products $fixture.view
                if ($fixture.id -eq 'f4') {
                    if (-not $choice.occluded) { throw 'No fully scene-occluded probe control is available.' }
                    $null = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;
                        GIProbeSelection=($choice.occluded.selection | ConvertTo-Json -Compress)}
                    Set-Values $session $run @(@{id='view.gi_probes.depth_mode';type='enum';value=1})
                    Capture $session $run "$directory/occluded" $products $fixture.view
                    $null = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;GIProbeSelection=$selectionJson}
                    Set-Values $session $run @(@{id='view.gi_probes.depth_mode';type='enum';value=0})
                }
                # A different independently owned numeric request must not overwrite selection.
                $numeric = @{coordinate=@((($selection.coordinate[0]+1)%$choice.counts[0]),$selection.coordinate[1],$selection.coordinate[2]);expectedGeneration=$selection.expectedGeneration}
                $numericProducts = @(@{kind='image';source='composited'},@{kind='resource';targets=@(@{resource='GI.Field.ProbeInspection'})})
                $null = New-Item -ItemType Directory -Path "$directory/numeric" -Force
                $numericResult = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';SettleFrames=4;
                    GIProbeInspection=($numeric | ConvertTo-Json -Compress);Products=($numericProducts | ConvertTo-Json -Depth 8 -Compress);
                    OutputDirectory="$directory/numeric";TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/numeric/receipt.json" $numericResult
                if ($numericResult.status -ne 'completed' -or $numericResult.publication -ne 'Published') { throw 'Independent numeric inspection did not publish.' }
                Set-Values $session $run @(
                    @{id='view.gi_probes.slice_axis';type='uint';value=2},
                    @{id='view.gi_probes.layer_minimum';type='uint';value=3},
                    @{id='view.gi_probes.layer_maximum';type='uint';value=1},
                    @{id='view.gi_probes.classification_mask';type='uint';value=0},
                    @{id='view.gi_probes.maximum_distance';type='float';value=0.001},
                    @{id='view.gi_probes.budget';type='uint';value=1})
                Capture $session $run "$directory/filtered" $products $fixture.view
                $null = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;ClearSelection=$true}
                Capture $session $run "$directory/cleared" $products $fixture.view
                $null = Invoke-VerificationSession select-gi-probe $session @{ExpectedRunIdentity=$run;GIProbeSelection=$selectionJson}
                foreach ($arm in @(
                    @{id='irradiance';taa=$false;quality=0;depth=0},
                    @{id='scene-depth';taa=$false;quality=0;depth=1},
                    @{id='native-taa';taa=$true;quality=0;depth=0},
                    @{id='native-taa-depth';taa=$true;quality=0;depth=1},
                    @{id='quality';taa=$true;quality=1;depth=0},
                    @{id='quality-depth';taa=$true;quality=1;depth=1})) {
                    Set-Values $session $run @(
                        @{id='view.taa.enabled';type='bool';value=$arm.taa},
                        @{id='view.taa.resolution_preset';type='enum';value=$arm.quality},
                        @{id='view.taa.post_temporal_view';type='enum';value=0},
                        @{id='view.gi_probes.mode';type='enum';value=1},
                        @{id='view.gi_probes.depth_mode';type='enum';value=$arm.depth})
                    $depthName = if ($arm.quality) { 'DisplayView.DisplayDepth' } else { 'DisplayView.DepthBuffer' }
                    $armProducts = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'},
                        @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},@{resource='GI.Probes.RenderedIDs'},
                            @{resource='GI.Field.Classification'},@{resource='GI.Probes.PrivateDepth'})})
                    # Display-domain temporal depth exists only when a consumer requests it.
                    if ($arm.depth) { $armProducts[3].targets += @{resource=$depthName} }
                    if ($fixture.id -eq 'black') { $armProducts[3].targets += @{resource='GI.Field.Irradiance'} }
                    Capture $session $run "$directory/$($arm.id)" $armProducts $fixture.view
                }
                $previous = $selection
            }
        } catch {
            $primaryFailure = $_
            throw
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Probe selection shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Probe selection native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeSelection.py" @('--root',$output) "$output/oracle-result.json" 'Probe selection failed'
    Assert-VerificationArtifacts $root $output $Python 'Probe selection failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Probe selection source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Probe selection binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
