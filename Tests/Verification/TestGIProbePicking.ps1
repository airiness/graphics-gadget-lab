<# Logical-view-bound, on-demand 1x1 probe picking with paired native evidence. #>
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
Import-Module "$PSScriptRoot/GIProbeViewerSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Picking requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbePicking" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Capture-Pick($Session, $Run, $Directory, $Products, $Pick = $null, $View = '') {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $options = @{ExpectedRunIdentity=$Run;Timing='next-frame';SettleFrames=0;
        Products=(ConvertTo-Json -InputObject $Products -Depth 10 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    if ($Pick) {
        $options.GIProbePicking = $Pick | ConvertTo-Json -Depth 16 -Compress
        Write-VerificationJson "$Directory/request.json" $Pick
    }
    if ($View) { $options.View = $View; $options.SettleFrames = 16 }
    $receipt = Invoke-VerificationSession capture $Session $options
    Write-VerificationJson "$Directory/receipt.json" $receipt
    return $receipt
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $session = 'gi-pick-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start = $null
        $directory = "$output/$backend"
        $null = New-Item -ItemType Directory -Path $directory -Force
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$root/Build/EPickState-$session";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Picking requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            $token = 0
            foreach ($fixture in @(@{id='f4';value=4;view='F4_Floor';mask=2},
                @{id='inactive';value=18;view='Contract_FieldUnsupported';mask=4})) {
                $null = Initialize-ProbeViewer $session $run $fixture.value $TimeoutSeconds
                $null = Set-ProbeViewerValues $session $run @(
                    @{id='view.gi_probes.enabled';type='bool';value=$true},
                    @{id='view.gi_probes.radius';type='float';value=0.45},
                    @{id='view.gi_probes.budget';type='uint';value=64},
                    @{id='view.gi_probes.classification_mask';type='uint';value=$fixture.mask},
                    @{id='view.gi_probes.mode';type='enum';value=0},
                    @{id='view.gi_probes.depth_mode';type='enum';value=0}) $TimeoutSeconds
                $arms = @(@{id='native';taa=$false;quality=0;projection=0;depth=0;mode=0},
                    @{id='scene';taa=$false;quality=0;projection=0;depth=1;mode=0})
                if ($fixture.id -eq 'f4') {
                    $arms += @(@{id='irradiance';taa=$false;quality=0;projection=0;depth=0;mode=1},
                        @{id='change';taa=$false;quality=0;projection=0;depth=0;mode=2},
                        @{id='taa';taa=$true;quality=0;projection=0;depth=1;mode=0},
                        @{id='jittered';taa=$true;quality=0;projection=1;depth=1;mode=0},
                        @{id='quality';taa=$true;quality=1;projection=0;depth=1;mode=0})
                }
                foreach ($arm in $arms) {
                    $case = "$directory/$($fixture.id)/$($arm.id)"
                    $null = Set-ProbeViewerValues $session $run @(
                        @{id='view.taa.enabled';type='bool';value=$arm.taa},
                        @{id='view.taa.resolution_preset';type='enum';value=$arm.quality},
                        @{id='view.taa.post_temporal_view';type='enum';value=$arm.projection},
                        @{id='view.gi_probes.depth_mode';type='enum';value=$arm.depth},
                        @{id='view.gi_probes.mode';type='enum';value=$arm.mode},
                        @{id='gi_reference.field_readiness';type='bool';value=($arm.mode -eq 2)}) $TimeoutSeconds
                    $depth = if ($arm.quality) { 'DisplayView.DisplayDepth' } else { 'DisplayView.DepthBuffer' }
                    $products = @(@{kind='image';source='composited'},@{kind='graph'},
                        @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},@{resource='GI.Probes.RenderedIDs'},
                            @{resource='GI.Field.Classification'},@{resource='GI.Probes.PrivateDepth'},@{resource=$depth})})
                    $preview = Capture-Pick $session $run "$case/preview" $products $null $fixture.view
                    if ($preview.status -ne 'completed' -or $preview.publication -ne 'Published') { throw 'Picking preview failed.' }
                    $text = & $Python "$PSScriptRoot/ValidateGIProbePicking.py" --choose "$case/preview"
                    if ($LASTEXITCODE -ne 0) { throw 'Picking oracle could not choose pixels.' }
                    $choices = ConvertFrom-Json -InputObject ([string]$text)
                    if ($arm.id -ne 'native') { $choices = @($choices[0],$choices[-1]) }
                    Write-VerificationJson "$case/choices.json" $choices
                    $index = 0
                    foreach ($choice in $choices) {
                        $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                        $pick = @{token=(++$token);pixel=@($choice.pixel);source=$status.frame.probePickingSource}
                        $receipt = Capture-Pick $session $run "$case/pick-$index" $products $pick
                        if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') {
                            throw ('Picking failed: '+($receipt | ConvertTo-Json -Depth 12 -Compress))
                        }
                        ++$index
                    }
                }
            }
            # Structurally valid stale sources must fail before a Runtime request, never NoHit.
            $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
            foreach ($mismatch in @('field','scene','view','settings','revision','dpi','origin','content','future')) {
                $frozen = $status.frame.probePickingSource | ConvertTo-Json -Depth 16 | ConvertFrom-Json
                switch ($mismatch) {
                    field { ++$frozen.field.generation }
                    scene { ++$frozen.field.sceneRevision }
                    view { ++$frozen.view.revision }
                    settings { $frozen.settings.radiusInSpacings = 0.5 }
                    revision { ++$frozen.settingsRevision }
                    dpi { $frozen.mapping.dpiScale *= 1.25 }
                    origin { $frozen.mapping.guiOrigin[0] = 1 }
                    content { $frozen.contentId = 'gglab.lab.other' }
                    future { $frozen.sourceFrame = [uint64]::MaxValue }
                }
                $receipt = Capture-Pick $session $run "$directory/mismatch-$mismatch" @(@{kind='graph'}) `
                    @{token=(++$token);pixel=@(0,0);source=$frozen}
                if ($receipt.status -ne 'failed' -or $receipt.publication -ne 'NotPublished' -or
                    $receipt.failureCode -notlike 'GIProbePicking*') { throw 'A stale picking source did not fail explicitly.' }
            }
        } finally { Stop-ProbeViewer $session $start $directory }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbePicking.py" @('--root',$output) "$output/oracle-result.json" 'Picking oracle failed'
    Assert-VerificationArtifacts $root $output $Python 'Picking artifacts failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Picking sources changed during verification.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Picking binaries changed during verification.'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;productionAcceptance=$false}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message}
    throw
}
