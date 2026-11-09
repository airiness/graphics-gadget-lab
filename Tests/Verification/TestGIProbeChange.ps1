<# Completed estimator change, block explanations and paired diagnostic images. #>
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
    throw 'Change verification requires one configuration and valid explicit backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeChange" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
function Capture-Change($Session, $Run, $Directory, $View, [bool]$Available, [bool]$BlackField = $false) {
    $products = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'})
    if ($Available) {
        $products += @{kind='resource';targets=@(@{resource='GI.Probes.Change'},@{resource='GI.Probes.Counts'},
            @{resource='GI.Probes.RenderedIDs'},@{resource='DisplayView.DepthBuffer'})}
        if ($BlackField) { $products[-1].targets += @{resource='GI.Field.Irradiance'} }
    }
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $receipt = Invoke-VerificationSession capture $Session @{ExpectedRunIdentity=$Run;Timing='next-frame';
        SettleFrames=8;View=$View;Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Directory/receipt.json" $receipt
    if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') { throw 'Change capture did not publish.' }
}
$records = @()
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $session = 'gi-change-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start = $null
        $directory = "$output/$backend"
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Change verification requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($fixture in @(@{id='f2';value=2;view='F2_Interior'},@{id='f4';value=4;view='F4_Floor'},
                @{id='black';value=19;view='F2_Interior'},@{id='excluded';value=14;view='Probe_Contract'})) {
                $null = Initialize-ProbeViewer $session $run $fixture.value $TimeoutSeconds
                $null = Set-ProbeViewerValues $session $run @(
                    @{id='gi_reference.field_readiness';type='bool';value=$true},
                    @{id='view.gi_probes.mode';type='enum';value=2},
                    @{id='view.gi_probes.radius';type='float';value=0.45},
                    @{id='view.gi_probes.budget';type='uint';value=256},
                    @{id='view.gi_probes.classification_mask';type='uint';value=2}) $TimeoutSeconds
                $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $r.frame.giField.summary.changeObservation } -TimeoutSeconds $TimeoutSeconds
                $generation = $null
                foreach ($arm in @(
                    @{id='off-before';enabled=$false;normalized=$true;blocks=$false;depth=0;range=3.0},
                    @{id='normalized';enabled=$true;normalized=$true;blocks=$false;depth=0;range=3.0},
                    @{id='raw';enabled=$true;normalized=$false;blocks=$false;depth=0;range=0.1},
                    @{id='depth';enabled=$true;normalized=$true;blocks=$false;depth=1;range=3.0},
                    @{id='blocks';enabled=$true;normalized=$true;blocks=$true;depth=0;range=3.0},
                    @{id='off-after';enabled=$false;normalized=$true;blocks=$false;depth=0;range=3.0})) {
                    $null = Set-ProbeViewerValues $session $run @(
                        @{id='view.gi_probes.enabled';type='bool';value=$arm.enabled},
                        @{id='view.gi_probes.change_normalized';type='bool';value=$arm.normalized},
                        @{id='view.gi_probes.change_range';type='float';value=$arm.range},
                        @{id='view.gi_probes.change_blocks';type='bool';value=$arm.blocks},
                        @{id='view.gi_probes.depth_mode';type='enum';value=$arm.depth}) $TimeoutSeconds
                    $relative = "$backend/$($fixture.id)/$($arm.id)"
                    Capture-Change $session $run "$output/$relative" $fixture.view $arm.enabled ($fixture.id -eq 'black')
                    $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                    if ($generation -and $generation -ne $status.frame.giField.generation) { throw 'Presentation reset the field.' }
                    $generation = $status.frame.giField.generation
                    $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;arm=$arm}
                    Write-VerificationJson "$output/cases.json" @($records)
                }
            }
            # Readiness-disabled fields explicitly lack a measurement and schedule no Change work.
            $null = Set-ProbeViewerValues $session $run @(
                @{id='gi_reference.field_readiness';type='bool';value=$false},
                @{id='view.gi_probes.enabled';type='bool';value=$true}) $TimeoutSeconds
            Capture-Change $session $run "$directory/disabled" 'Probe_Contract' $false
            $records += @{directory="$backend/disabled";rhi=$backend;fixture='disabled';arm=@{enabled=$false}}
            Write-VerificationJson "$output/cases.json" @($records)
        } finally { Stop-ProbeViewer $session $start $directory }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeChange.py" @('--images',$output) "$output/validator.json" 'Change validation failed'
    Assert-VerificationArtifacts $root $output $Python 'Change artifacts failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Change sources changed during the run.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Change binaries changed during the run.'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;cases=$records.Count;qualityAcceptance=$false}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message}
    throw
}
