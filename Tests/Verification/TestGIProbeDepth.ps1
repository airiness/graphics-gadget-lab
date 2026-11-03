<# Native scene-depth rejection and actual GPU D32 boundary conformance. #>
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
    throw 'Probe depth requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeDepth" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Set-Values($Session, $Run, $Values) {
    $r = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($Values) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $r.batch.succeeded) { throw 'Probe depth parameter batch failed.' }
}
function Capture($Session, $Run, $Directory, $Products, $View='') {
    $options = @{ExpectedRunIdentity=$Run;Timing='next-frame';SettleFrames=16;
        Products=(ConvertTo-Json -InputObject $Products -Depth 8 -Compress);
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    if ($View) { $options.View=$View }
    $r = Invoke-VerificationSession capture $Session $options
    Write-VerificationJson "$Directory/receipt.json" $r
    if ($r.status -ne 'completed' -or $r.publication -ne 'Published') { throw 'Probe depth capture did not publish.' }
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    foreach ($backend in $Rhi) {
        foreach ($lab in @('coordinate_conformance','gi_reference')) {
            $session = 'gi-depth-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
            $start = $null
            $primaryFailure = $null
            $directory = "$output/$backend/$lab"
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                    Lab="gglab.lab.$lab";WindowSize='640x400';NoDevTools=$true;Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Probe depth requires an owned session.' }
                $run = $start.runIdentity
                if ($lab -eq 'coordinate_conformance') {
                    $targets = @('Standard','Reversed') | ForEach-Object {
                        $convention = $_
                        @('Inputs','Scene','Winners','D32') | ForEach-Object { @{resource="GI.Depth.$convention.$_"} }
                    }
                    Capture $session $run "$directory/boundaries" @(@{kind='graph'},@{kind='resource';targets=@($targets)})
                    # Fail promptly on a native boundary mismatch before the longer scene run.
                    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeDepth.py" @('--conformance',"$directory/boundaries") "$directory/boundary-result.json" 'Probe D32 conformance failed'
                    continue
                }
                $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
                foreach ($fixture in @(@{id='f4';value=4;view='F4_Floor';layer=6;high=6},@{id='coastal';value=15;view='';layer=1;high=2})) {
                    Set-Values $session $run @(
                        @{id='gi_reference.fixture';type='enum';value=$fixture.value},
                        @{id='gi_reference.field_enabled';type='bool';value=$true},
                        @{id='gi_reference.field_sampling';type='bool';value=$true},
                        @{id='gi_reference.field_readiness';type='bool';value=$true},
                        @{id='gi_reference.probe_enabled';type='bool';value=$true},
                        @{id='view.taa.enabled';type='bool';value=$false},
                        @{id='view.taa.resolution_preset';type='enum';value=0},
                        @{id='view.gi_probes.enabled';type='bool';value=$true},
                        @{id='view.gi_probes.bounds';type='bool';value=$false},
                        @{id='view.gi_probes.radius';type='float';value=0.45},
                        @{id='view.gi_probes.preview_ev';type='float';value=-4.0},
                        @{id='view.gi_probes.budget';type='uint';value=64},
                        @{id='view.gi_probes.slice_axis';type='uint';value=2},
                        @{id='view.gi_probes.layer_minimum';type='uint';value=$fixture.layer},
                        @{id='view.gi_probes.layer_maximum';type='uint';value=$fixture.high},
                        @{id='view.gi_probes.classification_mask';type='uint';value=2})
                    $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } -Until {
                        param($r) $r.frame -and $r.frame.giField -and $r.frame.giField.summary.initialized -and $r.frame.giField.summary.inspectionAvailable
                    } -TimeoutSeconds $TimeoutSeconds
                    foreach ($mode in @(0,1)) {
                        foreach ($depth in @(0,1)) {
                            Set-Values $session $run @(@{id='view.gi_probes.mode';type='enum';value=$mode},
                                @{id='view.gi_probes.depth_mode';type='enum';value=$depth})
                            $products = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'},
                                @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},@{resource='GI.Probes.RenderedIDs'},
                                    @{resource='GI.Field.Classification'},@{resource='GI.Field.Irradiance'},@{resource='DisplayView.DepthBuffer'})})
                            Capture $session $run "$directory/$($fixture.id)/mode$mode-depth$depth" $products $fixture.view
                        }
                    }
                }
            } catch {
                $primaryFailure = $_
                throw
            } finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                    Write-VerificationJson "$directory/stop.json" $stop
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                    if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Probe depth shutdown failed.' }
                    $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                    if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Probe native/runtime validation failed.' }
                }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeDepth.py" @('--root',$output) "$output/oracle-result.json" 'Probe depth failed'
    Assert-VerificationArtifacts $root $output $Python 'Probe depth failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Probe depth source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Probe depth binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
