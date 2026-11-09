<# Continuous GGX visibility contracts, independent CPU qualification and Coastal diagnostics. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [string]$ShaderCacheDirectory,
    [switch]$Smoke,
    [switch]$SkipCoastal,
    [switch]$WithDevTools,
    [int]$TimeoutSeconds = 600
)
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GISpecularVisibility" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$previous = $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT
$env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT = "$root/Build/Fixtures/GIVisibility"
function Invoke-PythonCheck([string]$Script, [string[]]$Arguments, [string]$Receipt) {
    $lines = @(& $Python $Script @Arguments)
    $code = $LASTEXITCODE
    Write-VerificationJson $Receipt @{exitCode=$code;output=($lines -join "`n")}
    if ($code -ne 0) { throw "Visibility verification failed: $Script" }
}
function Get-VisibilitySourceIdentity {
    $names = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification Schemas Projects PropertySheets)
    if ($LASTEXITCODE -ne 0) { throw 'Source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object {
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
}
function Get-VisibilityBinaryIdentity([string]$Config) {
    return @(@('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') | ForEach-Object {
        [ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath "$root/Build/Output/x64/$Config/$_" -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
}
function Assert-VisibilityIdentity([object[]]$Expected, [object[]]$Actual, [string]$Kind) {
    if ((ConvertTo-Json -InputObject @($Expected) -Depth 4 -Compress) -cne
        (ConvertTo-Json -InputObject @($Actual) -Depth 4 -Compress)) {
        throw "$Kind inputs changed during visibility verification."
    }
}
function Test-VisibilityArtifacts([string]$Directory) {
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        Invoke-PythonCheck "$root/Scripts/Verification/$validator" @($Directory) "$Directory/$validator.result.json"
    }
}
function Set-Inputs([string]$Session, [string]$Run, [hashtable]$Values) {
    # Immediate bindings belong to the already active fixture, not a pending
    # RestartSession in the same batch. Activate it before material consumption.
    if ($Values.ContainsKey('visibility_material') -and $Values.Count -gt 1) {
        $other = $Values.Clone()
        $other.Remove('visibility_material')
        $null = Set-Inputs $Session $Run $other
        $Values = @{visibility_material=$Values.visibility_material}
    }
    $changes = foreach ($key in $Values.Keys) {
        @{id="gi_reference.$key";type=$(if ($key -in @('fixture','visibility_geometry','environment','intensity','yaw','sun_lux','ev100')) {'enum'} else {'uint'});value=$Values[$key]}
    }
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($changes) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Visibility inputs were not consumed.' }
    return $effect
}
function Capture-Resources([string]$Session, [string]$Run, [string]$Directory, [string[]]$Names, [string]$View) {
    $products = @(@{kind='resource';targets=@($Names | ForEach-Object {@{resource=$_}})},@{kind='graph'}) | ConvertTo-Json -Depth 8 -Compress
    $receipt = Invoke-VerificationSession capture $Session @{ExpectedRunIdentity=$Run;Timing='after-ready';
        SettleFrames=1;View=$View;Products=$products;OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Directory/capture.json" $receipt
    if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') {
        throw "Visibility publication incomplete in ${Directory}: $($receipt.failure)"
    }
}
function Copy-ShaderState([string]$Source, [string]$Target) {
    if (-not $Source) { return }
    $null = New-Item -ItemType Directory -Path $Target -Force
    foreach ($name in @('ShaderCache','ShaderArtifacts')) {
        if (Test-Path -LiteralPath "$Source/$name") { Copy-Item -LiteralPath "$Source/$name" -Destination "$Target/$name" -Recurse -Force }
    }
}
try {
    Invoke-PythonCheck "$PSScriptRoot/ValidateGISpecularVisibility.py" @('--generate', $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT, '--self-test') "$output/oracle-self-test.json"
    Copy-Item -LiteralPath "$env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT/manifest.json" -Destination "$output/fixtures.json"
    $identities = Get-VisibilitySourceIdentity
    Write-VerificationJson "$output/source-identity.json" $identities
    foreach ($config in $Configuration) {
        $null = New-Item -ItemType Directory -Path "$output/$config" -Force
        $binaries = Get-VisibilityBinaryIdentity $config
        Write-VerificationJson "$output/$config/binaries.json" $binaries
        $cases = @()
        foreach ($backend in $Rhi) {
            $directory = "$output/$config/$backend"
            $null = New-Item -ItemType Directory -Path $directory -Force
            $shaderState = if ($ShaderCacheDirectory) { "$ShaderCacheDirectory/$backend" } else { '' }
            Copy-ShaderState $shaderState "$directory/state"
            $session = 'gi-visibility-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
            $start = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize='129x81';NoDevTools=(-not $WithDevTools);Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Visibility verification requires an owned session.' }
                $run = $start.runIdentity
                $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                    SettleFrames=1;OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds}
                if ($warmup.publication -ne 'Published') { throw 'Contract content warmup failed.' }
                $matrix = @()
                foreach ($m in 0..11) { $matrix += @{material=$m;geometry=2;view='Contract_VisibilityNormal'} }
                $matrix += @(@{material=1;geometry=0;view='Contract_VisibilityNormal'},
                    @{material=4;geometry=1;view='Contract_VisibilityNormal'},
                    @{material=1;geometry=3;view='Contract_VisibilityNormal'},
                    @{material=5;geometry=2;view='Contract_VisibilityGrazing'},
                    @{material=6;geometry=2;view='Contract_VisibilityGrazing'},
                    @{material=7;geometry=2;view='Contract_VisibilityGrazing'})
                if ($Smoke) { $matrix = @($matrix[0],$matrix[5],$matrix[12],$matrix[13]) }
                $index = 0
                foreach ($entry in $matrix) {
                    foreach ($sample in @(@{n=4096;seed=61051},@{n=8192;seed=61051},@{n=8192;seed=61052})) {
                        $case = @{directory="$backend/case-$index";rhi=$backend;fixture=10;
                            visibility_geometry=$entry.geometry;visibility_material=$entry.material;view=$entry.view;
                            samples=$sample.n;seed=$sample.seed;batch_samples=64;tile_pixels=0;lighting='baseline'}
                        $effect = Set-Inputs $session $run @{fixture=10;visibility_geometry=$case.visibility_geometry;
                            visibility_material=$case.visibility_material;samples=$case.samples;seed=$case.seed;
                            batch_samples=$case.batch_samples;tile_pixels=$case.tile_pixels}
                        $captureDirectory = "$output/$config/$($case.directory)"
                        Capture-Resources $session $run $captureDirectory @('Visibility','Base','Coat','Weights','Position','Diagnostics','F0','LayerEnergy' | ForEach-Object {"GI.SpecularVisibility.$_"}) $case.view
                        Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                        Capture-Resources $session $run "$captureDirectory/receivers" @('FaceNormal','VertexNormal','ShadingNormal','DiffuseWeight','BaseColor','ReflectionRoughness','ClearcoatNormalRoughness','HitAlbedo' | ForEach-Object {"GI.Receiver.$_"}) $case.view
                        Capture-Resources $session $run "$captureDirectory/lut" @('IBL.BrdfLut') $case.view
                        $cases += $case
                        Write-VerificationJson "$output/$config/cases.json" @($cases)
                        $index++
                    }
                }
                # Same counter samples with radically different tile/batch partitioning.
                foreach ($arm in @(@{batch=31;tile=4096;environment=0;lighting='partition'},
                    @{batch=64;tile=0;environment=1;lighting='published-constant'},
                    @{batch=64;tile=0;environment=3;lighting='sun-sky'})) {
                    $case = @{directory="$backend/case-$index";rhi=$backend;fixture=10;
                        visibility_geometry=2;visibility_material=0;view='Contract_VisibilityNormal';
                        samples=8192;seed=61051;batch_samples=$arm.batch;tile_pixels=$arm.tile;lighting=$arm.lighting}
                    $effect = Set-Inputs $session $run @{fixture=10;visibility_geometry=2;visibility_material=0;
                        samples=8192;seed=61051;batch_samples=$arm.batch;tile_pixels=$arm.tile;
                        environment=$arm.environment;intensity=2;yaw=90;sun_lux=20000;ev100=15}
                    $captureDirectory = "$output/$config/$($case.directory)"
                    Capture-Resources $session $run $captureDirectory @('Visibility','Base','Coat','Weights','Position','Diagnostics','F0','LayerEnergy' | ForEach-Object {"GI.SpecularVisibility.$_"}) $case.view
                    Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                    Capture-Resources $session $run "$captureDirectory/receivers" @('FaceNormal','VertexNormal','ShadingNormal','DiffuseWeight','BaseColor','ReflectionRoughness','ClearcoatNormalRoughness','HitAlbedo' | ForEach-Object {"GI.Receiver.$_"}) $case.view
                    Capture-Resources $session $run "$captureDirectory/lut" @('IBL.BrdfLut') $case.view
                    $cases += $case
                    $index++
                }
                # Replacement during a partial sweep, cancellation of a waiting
                # capture, and deactivation all exercise the real GPU owner path.
                $null = Set-Inputs $session $run @{fixture=10;visibility_geometry=2;visibility_material=0;
                    samples=1048576;batch_samples=1;tile_pixels=64;environment=0}
                $old = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                Write-VerificationJson "$directory/transaction-before.json" $old
                $oldGate = @($old.frame.gates | Where-Object {$_.name -eq 'gi-specular-visibility'})[0]
                if ($oldGate.state -ne 'pending') { throw 'Partial sweep unexpectedly completed.' }
                $pending = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;NoWait=$true;
                    Timing='after-ready';OutputDirectory="$directory/cancelled";TimeoutSeconds=$TimeoutSeconds}
                $cancel = Invoke-VerificationSession capture-cancel $session @{ExpectedRunIdentity=$run;JobId=$pending.jobId}
                $cancelled = Invoke-VerificationSession result $session @{ExpectedRunIdentity=$run;JobId=$pending.jobId;Consume=$true}
                Write-VerificationJson "$directory/cancellation.json" @{admission=$pending;cancel=$cancel;receipt=$cancelled}
                if ($cancel.cancellation -ne 'Cancelled' -or $cancelled.status -ne 'cancelled') { throw 'Pending visibility capture cancellation failed.' }
                $replacement = Set-Inputs $session $run @{visibility_material=5}
                $new = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                Write-VerificationJson "$directory/transaction-replacement.json" @{effect=$replacement;status=$new}
                $newGate = @($new.frame.gates | Where-Object {$_.name -eq 'gi-specular-visibility'})[0]
                $before = [uint64]([regex]::Match($oldGate.detail, 'Observation ([0-9]+)').Groups[1].Value)
                $after = [uint64]([regex]::Match($newGate.detail, 'Observation ([0-9]+)').Groups[1].Value)
                if ($after -le $before -or $newGate.state -ne 'pending') { throw 'Primary replacement did not restart visibility.' }
                $same = Set-Inputs $session $run @{visibility_material=5}
                Write-VerificationJson "$directory/transaction-no-change.json" $same
                if ($same.batch.items[0].state -ne 'NoChange') { throw 'Identical primary inputs did not remain NoChange.' }
                $null = Set-Inputs $session $run @{visibility_material=0}
                $null = Set-Inputs $session $run @{fixture=1;samples=64;batch_samples=64;tile_pixels=0}
                $deactivated = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                    SettleFrames=1;OutputDirectory="$directory/deactivated";TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/deactivation.json" $deactivated
                $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                if (@($status.frame.gates | Where-Object {$_.name -eq 'gi-specular-visibility'}).Count -ne 0) { throw 'Inactive visibility still gates the diffuse Lab.' }
                if (-not $SkipCoastal) {
                    $contractStop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$run}
                    Write-VerificationJson "$directory/contracts-stop.json" $contractStop
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/contracts-output.log"
                    if (-not $contractStop.ok -or $contractStop.exitCode -ne 0) { throw 'Contract-session shutdown failed.' }
                    $contractLog = Get-Content -LiteralPath $start.outputLog -Raw
                    $start = $null
                    if ($contractLog -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Contract native validation failed.' }
                    Copy-ShaderState "$directory/state" "$directory/coastal-state"
                    $session += '-coastal'
                    $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                        Lab='gglab.lab.gi_reference';WindowSize='1280x800';NoDevTools=(-not $WithDevTools);Validation='standard';
                        StateRoot="$directory/coastal-state";StartTimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/coastal-start.json" $start
                    if ($start.ownership -ne 'created') { throw 'Coastal diagnostics require an owned session.' }
                    $run = $start.runIdentity
                    $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                        SettleFrames=1;OutputDirectory="$directory/coastal-warmup";TimeoutSeconds=$TimeoutSeconds}
                    if ($warmup.publication -ne 'Published') { throw 'Coastal content warmup failed.' }
                    $effect = Set-Inputs $session $run @{fixture=11;samples=256;seed=61051;batch_samples=64;tile_pixels=0}
                    Write-VerificationJson "$directory/coastal-effect.json" $effect
                    foreach ($view in @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge')) {
                        $case = @{directory="$backend/coastal-$view";rhi=$backend;fixture=11;
                            visibility_geometry=0;visibility_material=0;view=$view;samples=256;seed=61051;batch_samples=64;tile_pixels=0}
                        $captureDirectory = "$output/$config/$($case.directory)"
                        Capture-Resources $session $run $captureDirectory @('Visibility','Base','Coat','Weights','Position','Diagnostics','F0','LayerEnergy' | ForEach-Object {"GI.SpecularVisibility.$_"}) $view
                        Capture-Resources $session $run "$captureDirectory/receivers" @('FaceNormal','VertexNormal','ShadingNormal','DiffuseWeight','BaseColor','ReflectionRoughness','ClearcoatNormalRoughness','HitAlbedo' | ForEach-Object {"GI.Receiver.$_"}) $view
                        Capture-Resources $session $run "$captureDirectory/lut" @('IBL.BrdfLut') $view
                        $image = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$view;Timing='after-ready';
                            SettleFrames=1;OutputDirectory="$captureDirectory/image";TimeoutSeconds=$TimeoutSeconds}
                        if ($image.publication -ne 'Published') { throw 'Coastal diagnostic image incomplete.' }
                        $cases += $case
                    }
                }
            }
            finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                    Write-VerificationJson "$directory/stop.json" $stop
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                    if (-not $stop.ok -or $stop.exitCode -ne 0) { throw ('Visibility teardown failed. Prior error: '+[string]$Error[0]) }
                    $log = Get-Content -LiteralPath $start.outputLog -Raw
                    if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Native validation failed.' }
                }
                Write-VerificationJson "$output/$config/cases.json" @($cases)
            }
        }
        Test-VisibilityArtifacts "$output/$config"
        Invoke-PythonCheck "$PSScriptRoot/ValidateGISpecularVisibility.py" @('--root', "$output/$config") "$output/$config/oracle-result.json"
        $finalBinaries = Get-VisibilityBinaryIdentity $config
        Write-VerificationJson "$output/$config/binaries-final.json" $finalBinaries
        Assert-VisibilityIdentity $binaries $finalBinaries 'Executable/compiler'
    }
    $finalIdentities = Get-VisibilitySourceIdentity
    Write-VerificationJson "$output/source-identity-final.json" $finalIdentities
    Assert-VisibilityIdentity $identities $finalIdentities 'Source'
    Write-VerificationJson "$output/result.json" @{status='passed';configuration=$Configuration;rhi=$Rhi;
        coastal=(-not $SkipCoastal);smoke=[bool]$Smoke;developmentTools=[bool]$WithDevTools;productionS1=$false}
    Write-Output (ConvertTo-Json -InputObject @{ok=$true;directory=$output} -Compress)
}
catch {
    Write-VerificationJson "$output/result.json" @{status='failed';qualification='unconverged-or-invalid';error=[string]$_}
    throw
}
finally { $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT = $previous }
