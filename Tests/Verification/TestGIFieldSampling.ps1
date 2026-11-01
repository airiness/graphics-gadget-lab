<# Static single-bounce lookup/composition contracts; quality measurements remain separate. #>
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
    throw 'Field sampling requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldSampling" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory
$cases = @(
    @{id='plane';fixture=1;view='F1_Plane';ev100=10;albedo=0},
    @{id='plane-colour';fixture=1;view='F1_Plane';ev100=0;albedo=1},
    @{id='plane-high';fixture=1;view='F1_Plane';ev100=15;albedo=0},
    @{id='sphere';fixture=0;view='F1_Sphere';ev100=10;albedo=0},
    @{id='sealed';fixture=2;view='F2_Interior';ev100=10;albedo=0},
    @{id='thin';fixture=3;view='F3_Interior';ev100=10;albedo=0},
    @{id='sunroom';fixture=4;view='F4_Floor';ev100=10;albedo=0},
    @{id='coverage';fixture=17;view='Contract_FieldCoverage';ev100=10;albedo=0},
    @{id='unsupported';fixture=18;view='Contract_FieldUnsupported';ev100=10;albedo=0},
    @{id='coastal';fixture=15;view='GI_InteriorExterior';ev100=15;albedo=0}
)
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
function Set-Inputs([string]$Session, [string]$Run, [hashtable]$Values) {
    $changes = @($Values.Keys | ForEach-Object {
        @{id="gi_reference.$_";type=$(if ($_ -in @('fixture','ev100','albedo','environment')) {'enum'} elseif ($Values[$_] -is [bool]) {'bool'} else {'uint'});value=$Values[$_]}
    })
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Field sampling parameters were not consumed.' }
    return $effect
}
try {
    $sources = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $sources
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/cases.json" $cases
    Write-VerificationJson "$output/backends.json" $Rhi
    foreach ($backend in $Rhi) {
        $session = 'gi-lookup-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Field sampling requires an owned session.' }
            $run = $start.runIdentity
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                New-Item -ItemType Directory -Path $directory -Force | Out-Null
                $effect = Set-Inputs $session $run @{fixture=$case.fixture;ev100=$case.ev100;albedo=$case.albedo;
                    field_enabled=$true;field_sampling=$true;probe_enabled=$true}
                Write-VerificationJson "$directory/effect.json" $effect
                $targets = @('GI.Field.Irradiance','GI.Field.Distance','GI.Field.Classification',
                    'GI.Field.ReceiverIrradiance','GI.Field.ReceiverQuery','GI.Receiver.Position',
                    'GI.Receiver.VertexNormal','GI.Receiver.ShadingNormal' | ForEach-Object { @{resource=$_} })
                # All eight payload slots belong to the paired lookup group. Images are
                # separate settled frames and do not claim exact same-frame provenance.
                $capture = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View=$case.view;
                    Targets=(ConvertTo-Json -InputObject $targets -Compress);IncludeGraph=$true;SettleFrames=512;
                    OutputDirectory="$directory/lookup";TimeoutSeconds=$TimeoutSeconds}
                if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Field lookup did not publish.' }
                Write-VerificationJson "$directory/lookup/receipt.json" $capture
                Write-VerificationJson "$directory/status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
                if ($case.id -in @('plane','coastal')) {
                    foreach ($arm in @('on','update-only','off')) {
                        if ($arm -eq 'update-only') { $null = Set-Inputs $session $run @{field_sampling=$false} }
                        if ($arm -eq 'off') { $null = Set-Inputs $session $run @{probe_enabled=$false} }
                        $image = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                            SettleFrames=256;OutputDirectory="$directory/$arm";TimeoutSeconds=$TimeoutSeconds}
                        if ($image.status -ne 'completed' -or $image.publication -ne 'Published') { throw 'Field composition image did not publish.' }
                        Write-VerificationJson "$directory/$arm/receipt.json" $image
                    }
                }
                if ($case.id -eq 'sunroom') {
                    $referenceEffect = Set-Inputs $session $run @{field_enabled=$false;probe_enabled=$true;
                        samples=1024;depth=1;batch_samples=64;accumulate=$true}
                    Write-VerificationJson "$directory/reference-effect.json" $referenceEffect
                    $referenceTargets = @('GI.Reference.Irradiance','GI.Reference.Position','GI.Reference.Identity',
                        'GI.Reference.Diagnostics','GI.Receiver.Position','GI.Receiver.ShadingNormal',
                        'GI.Receiver.BaseColor','GI.Reference.LambertRadiance' | ForEach-Object { @{resource=$_} })
                    $reference = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View=$case.view;
                        Targets=(ConvertTo-Json -InputObject $referenceTargets -Compress);IncludeGraph=$true;SettleFrames=0;
                        OutputDirectory="$directory/reference";TimeoutSeconds=$TimeoutSeconds}
                    if ($reference.status -ne 'completed' -or $reference.publication -ne 'Published') { throw 'Matched D1 reference did not publish.' }
                    Write-VerificationJson "$directory/reference/receipt.json" $reference
                }
                Write-Verbose "$backend $($case.id) field lookup published"
            }
            $null = Set-Inputs $session $run @{fixture=17;environment=1;ev100=0;
                field_enabled=$true;field_sampling=$true;probe_enabled=$true}
            foreach ($enabled in @($false,$true)) {
                $arm = if ($enabled) {'gtao-on'} else {'gtao-off'}
                $hostEffect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject @(@{id='view.gtao.enabled';type='bool';value=$enabled}) -Compress);
                    TimeoutSeconds=$TimeoutSeconds}
                if (-not $hostEffect.batch.succeeded) { throw 'A0 Host GTAO control was not consumed.' }
                $aoTargets = @('DisplayView.SceneColor','GI.Field.ReceiverIrradiance','GI.Field.ReceiverQuery',
                    'GI.Receiver.DiffuseWeight','GI.Receiver.BaseColor','GI.Receiver.Position','IBL.IrradianceCubemap')
                if ($enabled) { $aoTargets += 'GTAO.FinalAO' }
                $aoTargets = @($aoTargets | ForEach-Object { @{resource=$_} })
                $aoCapture = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View='Contract_FieldCoverage';
                    Targets=(ConvertTo-Json -InputObject $aoTargets -Compress);IncludeGraph=$true;SettleFrames=512;
                    OutputDirectory="$output/$backend/a0/$arm";TimeoutSeconds=$TimeoutSeconds}
                if ($aoCapture.status -ne 'completed' -or $aoCapture.publication -ne 'Published') { throw 'A0 comparison did not publish.' }
                Write-VerificationJson "$output/$backend/a0/$arm/receipt.json" $aoCapture
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Field sampling session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Field sampling native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIFieldSampling.py" @('--root',$output) "$output/oracle-result.json" 'Field sampling failed'
    Assert-VerificationArtifacts $root $output $Python 'Field sampling failed'
    $finalSources = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSources
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $sources $finalSources 'Field sampling sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Field sampling binaries changed during verification.' -Depth 2
    Write-VerificationJson "$output/result.json" @{status='passed';backends=$Rhi;observations=($Rhi.Count*($cases.Count+9));lookupContractsPassed=$true;
        staticFieldQualityQualified=$false;readinessQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=[string]$_} | ConvertTo-Json -Compress))
    exit 1
}
[Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
