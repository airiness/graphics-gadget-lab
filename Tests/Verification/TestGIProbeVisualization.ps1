<# Stable GPU probe selection, bounded exports and composited-only spheres. #>
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
    throw 'Probe visualization requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeVisualization" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Set-Values($Session, $Run, $Values) {
    $r = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($Values) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $r.batch.succeeded) { throw 'Probe visualization parameter batch failed.' }
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    $cases = @(
        @{id='off';enabled=$false;taa=$false;axis=0;low=0;high=65535;mask=15;distance=0.0;budget=2048;bounds=$false},
        @{id='all';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=15;distance=0.0;budget=16384;bounds=$false},
        @{id='repeat';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=15;distance=0.0;budget=16384;bounds=$false},
        @{id='budget';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=15;distance=0.0;budget=7;bounds=$false},
        @{id='slice-x';enabled=$true;taa=$false;axis=1;low=1;high=3;mask=15;distance=0.0;budget=16384;bounds=$false},
        @{id='slice-y';enabled=$true;taa=$false;axis=2;low=1;high=3;mask=15;distance=0.0;budget=16384;bounds=$false},
        @{id='slice-z';enabled=$true;taa=$false;axis=3;low=1;high=3;mask=15;distance=0.0;budget=16384;bounds=$false},
        @{id='empty';enabled=$true;taa=$false;axis=2;low=3;high=1;mask=15;distance=0.0;budget=32;bounds=$false},
        @{id='inactive';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=4;distance=0.0;budget=16384;bounds=$false},
        @{id='active';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=2;distance=0.0;budget=16384;bounds=$false},
        @{id='distance';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=15;distance=5.0;budget=16384;bounds=$false},
        @{id='bounds';enabled=$true;taa=$false;axis=0;low=0;high=65535;mask=0;distance=0.0;budget=32;bounds=$true},
        @{id='released';enabled=$false;taa=$false;axis=0;low=0;high=65535;mask=15;distance=0.0;budget=2048;bounds=$false}
    )
    Write-VerificationJson "$output/cases.json" $cases
    foreach ($backend in $Rhi) {
        $session = 'gi-probes-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        $primaryFailure = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Probe visualization requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($fixture in @(@{id='f4';value=4;view='F4_Floor'},@{id='coastal';value=15;view=''})) {
                Set-Values $session $run @(
                    @{id='gi_reference.fixture';type='enum';value=$fixture.value},
                    @{id='gi_reference.ev100';type='enum';value=10},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$false},
                    @{id='gi_reference.probe_enabled';type='bool';value=$true},
                    @{id='gi_reference.probe_rays';type='uint';value=256},
                    @{id='gi_reference.seed';type='uint';value=61051})
                foreach ($case in $cases) {
                    $directory = "$output/$backend/$($fixture.id)/$($case.id)"
                    Set-Values $session $run @(
                        @{id='view.taa.enabled';type='bool';value=$case.taa},
                        @{id='view.taa.resolution_preset';type='enum';value=0},
                        @{id='view.gi_probes.enabled';type='bool';value=$case.enabled},
                        @{id='view.gi_probes.bounds';type='bool';value=$case.bounds},
                        @{id='view.gi_probes.radius';type='float';value=0.2},
                        @{id='view.gi_probes.budget';type='uint';value=$case.budget},
                        @{id='view.gi_probes.slice_axis';type='uint';value=$case.axis},
                        @{id='view.gi_probes.layer_minimum';type='uint';value=$case.low},
                        @{id='view.gi_probes.layer_maximum';type='uint';value=$case.high},
                        @{id='view.gi_probes.classification_mask';type='uint';value=$case.mask},
                        @{id='view.gi_probes.maximum_distance';type='float';value=$case.distance})
                    $products = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'})
                    if ($case.enabled -and -not $case.taa) {
                        $products += @{kind='resource';targets=@(@{resource='GI.Probes.Counts'},
                            @{resource='GI.Probes.RenderedIDs'},@{resource='GI.Field.Classification'})}
                    }
                    $options = @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=4;
                        Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);
                        OutputDirectory=$directory;TimeoutSeconds=$TimeoutSeconds}
                    if ($fixture.view) { $options.View=$fixture.view }
                    $result = Invoke-VerificationSession capture $session $options
                    Write-VerificationJson "$directory/receipt.json" $result
                    if ($result.status -ne 'completed' -or $result.publication -ne 'Published') { throw 'Probe display capture did not publish.' }
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
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Probe visualization shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Probe native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeVisualization.py" @('--root',$output) "$output/oracle-result.json" 'Probe visualization failed'
    Assert-VerificationArtifacts $root $output $Python 'Probe visualization failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Probe visualization source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Probe visualization binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
