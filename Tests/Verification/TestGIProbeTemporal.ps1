<# Temporal probe display, exact jitter phases and inherited display-depth mapping. #>
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
    throw 'Temporal probes requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeTemporal" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Set-Values($Session, $Run, $Values) {
    $r = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($Values) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $r.batch.succeeded) { throw 'Temporal probes parameter batch failed.' }
}
function Targets($Quality, $Sequence, $Irradiance) {
    $depth = if ($Quality) { 'DisplayView.DisplayDepth' } else { 'DisplayView.DepthBuffer' }
    $targets = @(@{resource='GI.Probes.Counts'},@{resource='GI.Probes.RenderedIDs'},
        @{resource='GI.Field.Classification'},@{resource=$depth})
    if ($Irradiance) { $targets += @{resource='GI.Field.Irradiance'} }
    if ($Sequence) { $targets += @{resource='GI.Probes.PrivateDepth'} }
    if ($Quality) { $targets += @{resource='DisplayView.DepthBuffer'} }
    return $targets
}
function Set-Display($Session, $Run, $Arm) {
    Set-Values $Session $Run @(@{id='view.taa.enabled';type='bool';value=$Arm.taa},
        @{id='view.taa.resolution_preset';type='enum';value=$Arm.quality},
        @{id='view.taa.post_temporal_view';type='enum';value=$Arm.projection},
        @{id='view.gi_probes.mode';type='enum';value=$Arm.mode},
        @{id='view.gi_probes.depth_mode';type='enum';value=1})
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    $arms = @(
        @{id='native-off';taa=$false;quality=0;projection=1;mode=0;phases=1},
        @{id='native-stable';taa=$true;quality=0;projection=1;mode=0;phases=8},
        @{id='native-jittered';taa=$true;quality=0;projection=0;mode=0;phases=8},
        @{id='native-irradiance';taa=$true;quality=0;projection=0;mode=1;phases=8},
        @{id='quality';taa=$true;quality=1;projection=1;mode=0;phases=18},
        @{id='quality-irradiance';taa=$true;quality=1;projection=0;mode=1;phases=18})
    Write-VerificationJson "$output/arms.json" $arms
    foreach ($backend in $Rhi) {
        $session = 'gi-temporal-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        $primaryFailure = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Temporal probes require an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            # Use a static field for graphics qualification; this suite makes no convergence claim.
            foreach ($fixture in @(@{id='f4';value=4;low=6;high=6},@{id='coastal';value=15;low=1;high=2})) {
                Set-Values $session $run @(@{id='gi_reference.fixture';type='enum';value=$fixture.value},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.receiver_diagnostics';type='bool';value=$false},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$false},
                    @{id='gi_reference.probe_enabled';type='bool';value=$true},
                    @{id='view.gi_probes.enabled';type='bool';value=$true},
                    @{id='view.gi_probes.bounds';type='bool';value=$false},
                    @{id='view.gi_probes.radius';type='float';value=0.45},
                    @{id='view.gi_probes.preview_ev';type='float';value=-4.0},
                    @{id='view.gi_probes.budget';type='uint';value=64},
                    @{id='view.gi_probes.slice_axis';type='uint';value=2},
                    @{id='view.gi_probes.layer_minimum';type='uint';value=$fixture.low},
                    @{id='view.gi_probes.layer_maximum';type='uint';value=$fixture.high},
                    @{id='view.gi_probes.classification_mask';type='uint';value=2},
                    @{id='view.gi_probes.maximum_distance';type='float';value=0.0})
                $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } -Until {
                    param($r) $r.frame -and $r.frame.giField -and $r.frame.giField.summary.initialized -and $r.frame.giField.summary.inspectionAvailable
                } -TimeoutSeconds $TimeoutSeconds
                foreach ($arm in $arms) {
                    Set-Display $session $run $arm
                    $directory = "$output/$backend/$($fixture.id)/$($arm.id)"
                    $null = New-Item -ItemType Directory -Path $directory -Force
                    if ($fixture.id -eq 'coastal' -and $arm.phases -gt 1 -and $arm.mode -eq 0) {
                        $frames = @(0..($arm.phases-1))
                        $exports = @($frames | ForEach-Object { @{frame=$_;targets=@(Targets ($arm.quality -eq 1) $true $false);graph=$true} })
                        $result = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                            CameraPath='GI_CostStatic';Source='composited';CaptureFrames=($frames -join ',');
                            Exports=(ConvertTo-Json -InputObject $exports -Depth 10 -Compress);
                            OutputDirectory=$directory;Label='phase';TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$directory/receipt.json" $result
                        if ($result.sequence.state -ne 'completed') { throw ('Probe temporal phase sequence did not complete: '+($result | ConvertTo-Json -Depth 12 -Compress)) }
                    } else {
                        $result = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$(if ($fixture.id -eq 'f4') {'F4_Floor'} else {''});
                            Timing='next-frame';SettleFrames=32;Products=(ConvertTo-Json -Depth 10 -Compress -InputObject @(
                                @{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'},
                                @{kind='resource';targets=@(Targets ($arm.quality -eq 1) $false ($arm.mode -eq 1))}));
                            OutputDirectory=$directory;TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$directory/receipt.json" $result
                        if ($result.status -ne 'completed' -or $result.publication -ne 'Published') { throw ('Probe temporal capture did not publish: '+($result | ConvertTo-Json -Depth 12 -Compress)) }
                    }
                    Write-VerificationJson "$directory/receipt.json" $result
                }
            }
        } catch {
            $primaryFailure = $_
            throw
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Probe temporal shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Probe temporal native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeTemporal.py" @('--root',$output) "$output/oracle-result.json" 'Probe temporal display failed'
    Assert-VerificationArtifacts $root $output $Python 'Probe temporal display failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Probe temporal source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Probe temporal binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
