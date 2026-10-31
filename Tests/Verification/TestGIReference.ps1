<#
.SYNOPSIS
Checks bounded fixed-depth F1-F4 and independent sun/sky transport. Does not qualify DDGI or Final.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [string]$GIOffBaselineDirectory,
    [ValidateSet('All', 'OneBounce', 'Transport')][string]$CaseSet = 'All',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/GIReference' }
$pinnedGIOff = -not $GIOffBaselineDirectory
if ($pinnedGIOff) { $GIOffBaselineDirectory = Join-Path $root 'Assets/Media/GGLabCoastalRetreat/GI/Daylight' }
$GIOffBaselineDirectory = (Resolve-Path -LiteralPath $GIOffBaselineDirectory).Path
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Check-Python([string]$Script, [string]$Directory, [string]$Result, [string]$Name) {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = @(& $Python $Script $Directory 2>&1)
        $code = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $previous }
    Write-VerificationJson $Result @{exitCode=$code;output=($lines -join "`n")}
    Check ($code -eq 0) "$Name (details: $Result)"
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $Python -c 'import numpy, jsonschema'
    if ($LASTEXITCODE -ne 0) { throw 'The selected Python must provide numpy and jsonschema.' }
    foreach ($config in $Configuration) {
        $configDirectory = Join-Path $output $config
        $null = New-Item -ItemType Directory -Path $configDirectory -Force
        $cases = @()
        foreach ($backend in $Rhi) {
            $directory = Join-Path $configDirectory $backend
            $null = New-Item -ItemType Directory -Path $directory -Force
            $session = "gi-reference-$runId-$config-$backend"
            $start = $null
            try {
                $start = Invoke-VerificationSession start $session @{ Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;
                    Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/start.json" $start
                Check ($start.ownership -eq 'created') "$backend-owned-run"
                $run = $start.runIdentity
                $warmup = Invoke-VerificationSession capture $session @{ ExpectedRunIdentity=$run;
                    Timing='after-ready';SettleFrames=1;RequiredContentId='gglab.lab.gi_reference';
                    OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds }
                Check ($warmup.status -eq 'completed' -and $warmup.publication -eq 'Published') "$backend-ready"
                $observations = @()
                if ($CaseSet -ne 'Transport') {
                    foreach ($fixture in @(0,1)) {
                        $view = if ($fixture -eq 0) { 'F1_Sphere' } else { 'F1_Plane' }
                        $variants = @(
                            @{name='base';samples=64;seed=61051;albedo=0;ev100=10},
                            @{name='independent';samples=64;seed=61052;albedo=0;ev100=10},
                            @{name='double';samples=128;seed=61051;albedo=0;ev100=10},
                            @{name='white-ev0';samples=64;seed=61051;albedo=0;ev100=0},
                            @{name='white-ev15';samples=64;seed=61051;albedo=0;ev100=15},
                            @{name='tint-ev0';samples=64;seed=61051;albedo=1;ev100=0},
                            @{name='tint-ev10';samples=64;seed=61051;albedo=1;ev100=10},
                            @{name='tint-ev15';samples=64;seed=61051;albedo=1;ev100=15})
                        foreach ($variant in $variants) {
                            $observations += @{directory="$backend/f$fixture-$($variant.name)";fixture=$fixture;
                                samples=$variant.samples;seed=$variant.seed;albedo=$variant.albedo;ev100=$variant.ev100;view=$view}
                        }
                    }
                    foreach ($view in @('F2_Interior','F2_Floor','F2_Ceiling','F2_West','F2_East','F2_Front','F2_Roof')) {
                        $observations += @{directory="$backend/$view";fixture=2;samples=64;seed=61051;albedo=0;ev100=10;view=$view}
                    }
                    foreach ($view in @('F3_Interior','F3_Floor','F3_Ceiling','F3_West','F3_East','F3_Front','F3_Roof')) {
                        $observations += @{directory="$backend/$view";fixture=3;samples=64;seed=61051;albedo=0;ev100=10;view=$view}
                    }
                    # Each base family has independent-seed and doubled-sample observations.
                    foreach ($surface in @('Floor','Back')) {
                        foreach ($variant in @('base','independent','double')) {
                            $observations += @{directory="$backend/f4-$($surface.ToLower())-$variant";fixture=4;
                                samples=$(if ($variant -eq 'double') {4096} else {2048});
                                seed=$(if ($variant -eq 'independent') {61052} else {61051});
                                albedo=0;ev100=10;view="F4_$surface";variant=$variant}
                        }
                    }
                    foreach ($view in @('F4_Overview','F4_West','F4_East')) {
                        $observations += @{directory="$backend/$view";fixture=4;samples=512;seed=61051;albedo=0;ev100=10;view=$view}
                    }
                    foreach ($variant in @('base','independent','double','intensity','red','receiver','blocked','atmosphere')) {
                        $observations += @{directory="$backend/sun-$variant";
                            fixture=$(if ($variant -eq 'blocked') {6} else {5});
                            samples=$(if ($variant -eq 'double') {1024} else {512});
                            seed=$(if ($variant -eq 'independent') {61052} else {61051});
                            albedo=$(if ($variant -eq 'receiver') {1} else {0});ev100=10;view='Contract_Sun';variant=$variant;
                            sun_lux=$(if ($variant -eq 'intensity') {20000} else {10000});
                            secondary_albedo=$(if ($variant -eq 'red') {1} else {0});atmosphere=($variant -eq 'atmosphere')}
                    }
                    foreach ($batch in @(1,7,31,4096)) {
                        $observations += @{directory="$backend/sun-batch-$batch";fixture=5;samples=512;seed=61051;
                            albedo=0;ev100=10;view='Contract_Sun';batch_samples=$batch}
                    }
                    $observations += @{directory="$backend/long-plane";fixture=1;samples=8193;seed=61051;
                        albedo=0;ev100=10;view='F1_Plane';batch_samples=31}
                    foreach ($variant in @('direct','cube')) {
                        $observations += @{directory="$backend/constant-$variant";fixture=7;samples=512;seed=61051;
                            albedo=0;ev100=10;view='Contract_Environment';environment=$(if ($variant -eq 'cube') {1} else {0})}
                    }
                    foreach ($source in @('gradient','physical')) {
                        $variants = if ($source -eq 'gradient') { @('base','independent','double','intensity','yaw','mip0') } else {
                            @('base','independent','double','mip0') }
                        foreach ($variant in $variants) {
                            $observations += @{directory="$backend/$source-$variant";fixture=7;
                                samples=$(if ($variant -eq 'double') {1024} else {512});
                                seed=$(if ($variant -eq 'independent') {61052} else {61051});
                                albedo=0;ev100=10;view='Contract_Environment';variant=$variant;
                                environment=$(if ($source -eq 'gradient') {2} else {3});
                                filter=$(if ($variant -eq 'mip0') {1} else {0});
                                intensity=$(if ($variant -eq 'intensity') {2} else {1});
                                yaw=$(if ($variant -eq 'yaw') {90} else {0})}
                        }
                    }
                }
                if ($CaseSet -ne 'OneBounce') {
                    foreach ($depth in @(1,2,3,4)) {
                        $observations += @{directory="$backend/transport-planes-d$depth";fixture=8;
                            depth=$depth;samples=4096;seed=61051;albedo=0;secondary_albedo=1;ev100=10;
                            view='Contract_Sun';transport=$true}
                        $observations += @{directory="$backend/transport-f4-d$depth";fixture=4;
                            depth=$depth;samples=4096;seed=61051;albedo=0;ev100=10;view='F4_Floor';transport=$true}
                    }
                    foreach ($depth in @(2,3)) {
                        foreach ($variant in @('independent','double')) {
                            foreach ($fixture in @(8,4)) {
                                $family = if ($fixture -eq 8) {'planes'} else {'f4'}
                                $observations += @{directory="$backend/transport-$family-d$depth-$variant";fixture=$fixture;
                                    depth=$depth;samples=$(if ($variant -eq 'double') {8192} else {4096});
                                    seed=$(if ($variant -eq 'independent') {61052} else {61051});
                                    albedo=0;secondary_albedo=$(if ($fixture -eq 8) {1} else {0});ev100=10;
                                    view=$(if ($fixture -eq 8) {'Contract_Sun'} else {'F4_Floor'});
                                    variant=$variant;transport=$true}
                            }
                        }
                    }
                    foreach ($depth in @(1,2,3)) {
                        $observations += @{directory="$backend/transport-receiver-d$depth";fixture=8;
                            depth=$depth;samples=4096;seed=61051;albedo=1;secondary_albedo=1;ev100=10;
                            view='Contract_Sun';transport=$true}
                    }
                    foreach ($batch in @(1,7,31)) {
                        $observations += @{directory="$backend/transport-batch-$batch";fixture=8;depth=3;
                            samples=512;seed=61051;albedo=0;secondary_albedo=1;ev100=10;
                            view='Contract_Sun';batch_samples=$batch;variant='batch';transport=$true}
                    }
                    foreach ($fixture in @(2,3)) {
                        foreach ($depth in @(2,3,4)) {
                            foreach ($surface in @('Interior','Floor','Ceiling','West','East','Front','Roof')) {
                                $observations += @{directory="$backend/transport-f$fixture-d$depth-$surface";fixture=$fixture;
                                    depth=$depth;samples=64;seed=61051;albedo=0;ev100=10;
                                    view="F${fixture}_$surface";transport=$true}
                            }
                        }
                    }
                }
                $targets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                    'GI.Reference.Identity','GI.Receiver.FaceNormal','GI.Receiver.ShadingNormal',
                    'GI.Receiver.HitAlbedo','GI.Reference.LambertRadiance') | ForEach-Object { @{resource=$_} }
                $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'})
                foreach ($observation in $observations) {
                    foreach ($entry in @{depth=1;transport=$false;batch_samples=64;environment=0;filter=0;intensity=1;yaw=0;sun_lux=10000;secondary_albedo=0;atmosphere=$false}.GetEnumerator()) {
                        if (-not $observation.ContainsKey($entry.Key)) { $observation[$entry.Key] = $entry.Value }
                    }
                    $changes = @()
                    foreach ($key in @('fixture','depth','samples','batch_samples','seed','albedo','ev100','environment','filter','intensity','yaw','sun_lux','secondary_albedo','atmosphere')) {
                        $type = if ($key -in @('depth','samples','batch_samples','seed')) { 'uint' } elseif ($key -eq 'atmosphere') { 'bool' } else { 'enum' }
                        $changes += @{id="gi_reference.$key";type=$type;value=$observation[$key]}
                    }
                    $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                        Changes=($changes | ConvertTo-Json -Compress -Depth 6);TimeoutSeconds=$TimeoutSeconds}
                    Check ($effect.batch.succeeded) "$($observation.directory)-parameters-consumed"
                    $captureDirectory = Join-Path $configDirectory $observation.directory
                    if ($observation.transport) {
                        # Preserve the eight-target group bound. Primary normals are
                        # covered by the retained D=1 suite; this group adds path validity.
                        $transportTargets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                            'GI.Reference.Identity','GI.Receiver.HitAlbedo','GI.Reference.PathDiagnostics',
                            'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance') | ForEach-Object { @{resource=$_} }
                        $products = @(@{kind='resource';targets=@($transportTargets)},@{kind='graph'})
                    }
                    elseif ($observation.fixture -ge 4) {
                        $lightingTargets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                            'GI.Reference.Identity','GI.Receiver.HitAlbedo','GI.Receiver.ShadingNormal',
                            'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance') | ForEach-Object { @{resource=$_} }
                        $products = @(@{kind='resource';targets=@($lightingTargets)},@{kind='graph'})
                    }
                    else { $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'}) }
                    $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                        Timing='after-ready';SettleFrames=1;View=$observation.view;RequiredContentId='gglab.lab.gi_reference';
                        Products=($products | ConvertTo-Json -Compress -Depth 8);
                        OutputDirectory=$captureDirectory;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$captureDirectory/capture.json" $capture
                    Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                    Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published') "$($observation.directory)-published"
                    $cases += $observation
                }
            }
            finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                    Write-VerificationJson "$directory/stop.json" $stop
                    Check ($stop.ok -and $stop.exitCode -eq 0) "$backend-exited-cleanly"
                    $log = Get-Content -LiteralPath $start.outputLog -Raw
                    Check ($log -notmatch '\[error\]|\[critical\]|Validation Error|VUID-') "$backend-no-native-or-runtime-errors"
                }
            }
        }
        Write-VerificationJson "$configDirectory/cases.json" @($cases)
        Check-Python "$PSScriptRoot/ValidateGIReference.py" $configDirectory `
            "$configDirectory/validation.json" "$config-F1-F4-lighting-numerical-gates"
        foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
            Check-Python "$root/Scripts/Verification/$validator" $configDirectory `
                "$configDirectory/$validator.result.json" "$config-$validator"
        }
        if ($CaseSet -ne 'OneBounce' -and $config -in @('Debug','Develop')) {
            $transactions = Invoke-VerificationScript "$PSScriptRoot/TestGIReferenceTransactions.ps1" @{
                Configuration=$config;Rhi=$Rhi;Python=$Python;
                RunDirectory="$configDirectory/transactions";TimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$configDirectory/transactions-receipt.json" $transactions
            Check ($transactions.exitCode -eq 0) "$config-reference-transactions-and-deep-invalidity"
        }
        # The pinned GI-off pixels are qualified for Develop. Run the ordinary
        # composition in fresh processes, with no GI Lab or ray-scene activation.
        if ($config -eq 'Develop' -and $CaseSet -ne 'Transport') {
            foreach ($backend in $Rhi) {
                $offDirectory = Join-Path $configDirectory "gi-off-$backend"
                $offSession = "gi-reference-off-$runId-$backend"
                $offStart = $null
                try {
                    $offStart = Invoke-VerificationSession start $offSession @{Configuration=$config;Rhi=$backend;
                        Demo='atrium';WindowSize='1280x800';NoDevTools=$true;Validation='standard';
                        StateRoot="$offDirectory/state";StartTimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$offDirectory/start.json" $offStart
                    Check ($offStart.ownership -eq 'created') "$backend-gi-off-owned-run"
                    foreach ($view in @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge')) {
                        $offCapture = Invoke-VerificationSession capture $offSession @{
                            ExpectedRunIdentity=$offStart.runIdentity;Timing='after-ready';SettleFrames=64;View=$view;
                            Label='pre-gi-g0';Products='[{"kind":"image","source":"scene"},{"kind":"graph"}]';
                            OutputDirectory="$offDirectory/captures";TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$offDirectory/$view.json" $offCapture
                        Check ($offCapture.status -eq 'completed' -and $offCapture.publication -eq 'Published') "$backend-gi-off-$view"
                    }
                }
                finally {
                    if ($offStart -and $offStart.ownership -eq 'created') {
                        $offStop = Invoke-VerificationSession stop $offSession @{ExpectedRunIdentity=$offStart.runIdentity}
                        Write-VerificationJson "$offDirectory/stop.json" $offStop
                        Check ($offStop.ok -and $offStop.exitCode -eq 0) "$backend-gi-off-exited-cleanly"
                        $offLog = Get-Content -LiteralPath $offStart.outputLog -Raw
                        Check ($offLog -notmatch '\[error\]|\[critical\]|Validation Error|VUID-') "$backend-gi-off-no-errors"
                    }
                }
                $comparison = @(& powershell -NoProfile -ExecutionPolicy Bypass -File "$root/Scripts/Verification/CompareCaptures.ps1" -Reference "$GIOffBaselineDirectory/$backend" -Candidate "$offDirectory/captures" -Threshold 0 -MaxMeanError 0 -MaxDifferingPercent 0 -RequireMatchingInputs)
                $comparisonExit = $LASTEXITCODE
                Write-VerificationJson "$offDirectory/comparison.json" ($comparison | ConvertFrom-Json)
                Check ($comparisonExit -eq 0) "$backend-gi-off-exact-pixel-and-input-regression"
                foreach ($graphFile in Get-ChildItem "$offDirectory/captures" -Filter '*.graph.json') {
                    $offGraph = Get-Content -LiteralPath $graphFile.FullName -Raw | ConvertFrom-Json
                    $giPasses = @($offGraph.compile.passes | Where-Object { -not $_.culled -and $_.name -match '^(GI\.|RayScene\.)' })
                    Check ($giPasses.Count -eq 0) "$backend-gi-off-no-reference-or-ray-scene-work"
                }
            }
        }

    }
    $result = @{ok=$true;directory=$output;checks=$checks;caseSet=$CaseSet;pinnedGIOff=$pinnedGIOff;giOffBaseline=$GIOffBaselineDirectory;scope='Fixed-depth integration; ReferenceUnconverged for DDGI'}
}
catch {
    $result = @{ok=$false;directory=$output;checks=$checks;pinnedGIOff=$pinnedGIOff;giOffBaseline=$GIOffBaselineDirectory;error=$_.Exception.Message}
}
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
