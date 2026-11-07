<# Independent material/transport checks and same-session snapshot replacement. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [switch]$Smoke,
    [string]$ShaderCacheDirectory,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIReferenceMaterials" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory
$previousFixtures = $env:GGLAB_GI_MATERIAL_FIXTURE_ROOT
$env:GGLAB_GI_MATERIAL_FIXTURE_ROOT = "$root/Build/Fixtures/GIMaterials"
function Invoke-PythonCheck([string]$Script, [string[]]$Arguments, [string]$Receipt) {
    $lines = @(& $Python $Script @Arguments)
    $code = $LASTEXITCODE
    Write-VerificationJson $Receipt @{exitCode=$code;output=($lines -join "`n")}
    if ($code -ne 0) { throw "Python check failed: $Receipt" }
}
function Hash-File([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Get-Implementation {
    $names = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification)
    if ($LASTEXITCODE -ne 0) { throw 'Source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $records = foreach ($name in $names) { if (Test-Path -LiteralPath "$root/$name" -PathType Leaf) {
        @{path=$name;sha256=(Hash-File "$root/$name")}
    } }
    return @($records)
}
function Set-Reference([string]$Session, [string]$Run, [hashtable]$Values) {
    $changes = foreach ($name in $Values.Keys) {
        @{id="gi_reference.$name";type=$(if ($name -eq 'fixture') {'enum'} else {'uint'});value=$Values[$name]}
    }
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($changes) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Material parameters were not consumed.' }
    return $effect
}
function Copy-ShaderState([string]$Source, [string]$Target) {
    if (-not $Source) { return }
    foreach ($name in @('ShaderCache','ShaderArtifacts')) {
        $path = Join-Path $Source $name
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Missing shader seed: $path" }
        $null = New-Item -ItemType Directory -Path $Target -Force
        Copy-Item -LiteralPath $path -Destination (Join-Path $Target $name) -Recurse -Force
    }
}
function Test-MaterialTransactions([string]$Config, [string]$Backend, [string]$Directory, [string]$ShaderState) {
    Copy-ShaderState $ShaderState "$Directory/state"
    $session = 'gi-material-tx-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
    $start = $null
    try {
        $start = Invoke-VerificationSession start $session @{Configuration=$Config;Rhi=$Backend;
            Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
            StateRoot="$Directory/state";StartTimeoutSeconds=$TimeoutSeconds}
        Write-VerificationJson "$Directory/start.json" $start
        if ($start.ownership -ne 'created') { throw 'Transaction test must own its session.' }
        $run = $start.runIdentity
        $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
            SettleFrames=1;OutputDirectory="$Directory/warmup";TimeoutSeconds=$TimeoutSeconds}
        if ($warmup.publication -ne 'Published') { throw 'Transaction warmup failed.' }
        $effect = Set-Reference $session $run @{fixture=9;depth=2;samples=1048576;batch_samples=1;tile_pixels=1}
        Write-VerificationJson "$Directory/configuration-effect.json" $effect
        $old = Wait-VerificationCondition -Poll {
            Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
        } -Until {param($s) @($s.frame.gates | Where-Object {
            $_.name -eq 'gi-reference' -and $_.state -eq 'pending' -and
            $_.detail -match 'tiles ([0-9]+)/64000 submitted' -and [int]$Matches[1] -ge 4096}).Count -eq 1
        } -TimeoutSeconds $TimeoutSeconds
        Write-VerificationJson "$Directory/old-status.json" $old
        $gate = @($old.frame.gates | Where-Object {$_.name -eq 'gi-reference'})[0]
        $oldId = [uint64]([regex]::Match($gate.detail, 'Observation ([0-9]+)').Groups[1].Value)
        $pending = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;NoWait=$true;
            Timing='after-ready';OutputDirectory="$Directory/cancelled";TimeoutSeconds=$TimeoutSeconds}
        $cancel = Invoke-VerificationSession capture-cancel $session @{ExpectedRunIdentity=$run;JobId=$pending.jobId}
        $cancelled = Invoke-VerificationSession result $session @{ExpectedRunIdentity=$run;JobId=$pending.jobId;Consume=$true}
        Write-VerificationJson "$Directory/cancellation.json" @{cancel=$cancel;receipt=$cancelled}
        if ($cancel.cancellation -ne 'Cancelled' -or $cancelled.status -ne 'cancelled') { throw 'Pending capture cancellation failed.' }
        $effect = Set-Reference $session $run @{material_case=3}
        Write-VerificationJson "$Directory/replacement-effect.json" $effect
        $replaced = Wait-VerificationCondition -Poll {
            Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
        } -Until {param($s) @($s.frame.gates | Where-Object {
            $_.name -eq 'gi-reference' -and $_.state -eq 'pending' -and
            $_.detail -match 'Observation ([0-9]+)' -and [uint64]$Matches[1] -gt $oldId}).Count -eq 1
        } -TimeoutSeconds $TimeoutSeconds
        Write-VerificationJson "$Directory/replaced-status.json" $replaced
        $targets = @('GI.Reference.Irradiance','GI.Reference.NextSunSum','GI.Reference.NextEnvironmentSum',
            'GI.Reference.NextOutcomeSum','GI.Reference.NextRandomState','GI.Reference.NextSunCompensation',
            'GI.Reference.NextEnvironmentCompensation','GI.Reference.PathDiagnostics') | ForEach-Object {@{resource=$_}}
        $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'}) | ConvertTo-Json -Compress -Depth 8
        $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';
            SettleFrames=0;Products=$products;OutputDirectory="$Directory/replaced";TimeoutSeconds=$TimeoutSeconds}
        Write-VerificationJson "$Directory/replaced/capture.json" $capture
        if ($capture.publication -ne 'Published') { throw 'Partial replacement capture failed.' }
        $same = Set-Reference $session $run @{material_case=3}
        Write-VerificationJson "$Directory/no-change.json" $same
        if ($same.batch.items[0].state -ne 'NoChange') { throw 'Repeated material input was not NoChange.' }
        $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
        Write-VerificationJson "$Directory/no-change-status.json" $status
    }
    finally {
        if ($start -and $start.ownership -eq 'created') {
            $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
            Write-VerificationJson "$Directory/stop.json" $stop
            Copy-Item -LiteralPath $start.outputLog -Destination "$Directory/output.log"
            if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Partial material shutdown failed.' }
            $log = Get-Content -LiteralPath $start.outputLog -Raw
            if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Partial material native validation failed.' }
        }
    }
}
try {
    Invoke-PythonCheck "$PSScriptRoot/ValidateGIReferenceMaterials.py" @('--generate') "$output/fixtures-generation.json"
    Copy-Item -LiteralPath "$env:GGLAB_GI_MATERIAL_FIXTURE_ROOT/manifest.json" -Destination "$output/fixtures.json"
    Write-VerificationJson "$output/execution.json" @{shaderCacheDirectory=$ShaderCacheDirectory;smoke=[bool]$Smoke}
    $implementation = Get-Implementation
    Write-VerificationJson "$output/source-identity.json" $implementation
    foreach ($config in $Configuration) {
        $binaryInputs = foreach ($name in @('GraphicsGadgetLab.exe','dxcompiler.dll','dxil.dll')) {
            @{file=$name;sha256=(Hash-File "$root/Build/Output/x64/$config/$name")}
        }
        $cases = @()
        foreach ($backend in $Rhi) {
            $compiledShaderState = if ($ShaderCacheDirectory) { Join-Path $ShaderCacheDirectory $backend } else { '' }
            $groups = @(@{name='wall';view='Contract_MaterialWall';depth=1;samples=64;
                cases=@(0,1,2,3,4,5,6,7,8,9,10,11,12,13,15,16,18,19,20,21,0)})
            if ($Smoke) { $groups[0].cases = @(0,2,3,9,13,16,19,21) }
            else { foreach ($depth in @(1,2)) {
                $groups += @{name="floor-d$depth";view='Contract_MaterialFloor';depth=$depth;samples=4096;
                    cases=@(0,1,2,3,12,13,14,17,18,21)}
            } }
            foreach ($group in $groups) {
                $directory = "$output/$config/$backend/$($group.name)"
                $null = New-Item -ItemType Directory -Path $directory -Force
                Copy-ShaderState $compiledShaderState "$directory/state"
                $session = 'gi-material-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
                $start = $null
                try {
                    $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                        Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                        StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/start.json" $start
                    if ($start.ownership -ne 'created') { throw 'Material test requires an owned session.' }
                    $run = $start.runIdentity
                    $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                        SettleFrames=1;OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds}
                    if ($warmup.publication -ne 'Published') { throw 'Material warmup failed.' }
                    $effect = Set-Reference $session $run @{fixture=9;depth=$group.depth;samples=$group.samples;
                        batch_samples=64;seed=61051;tile_pixels=0}
                    Write-VerificationJson "$directory/configuration-effect.json" $effect
                    if ($group.name -eq 'wall') {
                        $names = @('GI.Reference.Irradiance','GI.Reference.Position','RayScene.HitAlbedo',
                            'RayScene.HitNormal','RayScene.HitIdentity','GI.Receiver.HitAlbedo',
                            'GI.Reference.Diagnostics','GI.Reference.PathDiagnostics')
                    } else {
                        $names = @('GI.Reference.Irradiance','GI.Reference.Position','GI.Reference.Identity',
                            'GI.Receiver.HitAlbedo','GI.Reference.Diagnostics','GI.Reference.PathDiagnostics',
                            'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance')
                    }
                    $targets = @($names | ForEach-Object {@{resource=$_}})
                    $products = @(@{kind='resource';targets=$targets},@{kind='graph'}) | ConvertTo-Json -Compress -Depth 8
                    $index = 0
                    foreach ($value in $group.cases) {
                        $case = @{directory="$backend/$($group.name)/case-$index";rhi=$backend;group=$group.name;
                            material_case=$value;depth=$group.depth;samples=$group.samples;view=$group.view}
                        $captureDirectory = "$output/$config/$($case.directory)"
                        $effect = Set-Reference $session $run @{material_case=$value}
                        if ($effect.batch.items[0].declaredClass -ne 'Immediate') { throw 'Material update restarted the Lab.' }
                        $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                            SettleFrames=1;View=$group.view;Products=$products;RequiredContentId='gglab.lab.gi_reference';
                            OutputDirectory=$captureDirectory;TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$captureDirectory/capture.json" $capture
                        Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                        if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Material capture incomplete.' }
                        if ($group.name -eq 'wall') {
                            $tableProducts = ConvertTo-Json -InputObject @(
                                @{kind='resource';targets=@(@{resource='RayScene.MaterialTable'})}) -Compress -Depth 6
                            $tables = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                                Timing='after-ready';SettleFrames=1;Products=$tableProducts;
                                OutputDirectory="$captureDirectory/tables";TimeoutSeconds=$TimeoutSeconds}
                            Write-VerificationJson "$captureDirectory/table-capture.json" $tables
                            if ($tables.publication -ne 'Published') { throw 'Material table provenance incomplete.' }
                        }
                        if ($index -eq 2) {
                            $image = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                                SettleFrames=1;OutputDirectory="$directory/preview";TimeoutSeconds=$TimeoutSeconds}
                            if ($image.publication -ne 'Published') { throw 'Material preview incomplete.' }
                        }
                        $cases += $case
                        $index++
                    }
                }
                finally {
                    if ($start -and $start.ownership -eq 'created') {
                        $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                        Write-VerificationJson "$directory/stop.json" $stop
                        Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                        if (-not $stop.ok -or $stop.exitCode -ne 0) { throw ('Material teardown failed. Prior error: '+[string]$Error[0]) }
                        $log = Get-Content -LiteralPath $start.outputLog -Raw
                        if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Material native/runtime validation failed.' }
                    }
                }
                $compiledShaderState = "$directory/state"
            }
            if (-not $Smoke) { Test-MaterialTransactions $config $backend "$output/$config/$backend/transactions" $compiledShaderState }
        }
        Write-VerificationJson "$output/$config/cases.json" @($cases)
        Invoke-PythonCheck "$PSScriptRoot/ValidateGIReferenceMaterials.py" @("$output/$config") "$output/$config/validation.json"
        foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
            Invoke-PythonCheck "$root/Scripts/Verification/$validator" @("$output/$config") "$output/$config/$validator.result.json"
        }
        $binaries = foreach ($name in @('GraphicsGadgetLab.exe','dxcompiler.dll','dxil.dll')) {
            @{file=$name;sha256=(Hash-File "$root/Build/Output/x64/$config/$name")}
        }
        if (($binaryInputs | ConvertTo-Json -Compress) -cne ($binaries | ConvertTo-Json -Compress)) {
            throw 'Executable/compiler inputs changed during verification.'
        }
        Write-VerificationJson "$output/$config/binaries.json" @($binaries)
    }
    $finalImplementation = Get-Implementation
    if (($implementation | ConvertTo-Json -Compress) -cne ($finalImplementation | ConvertTo-Json -Compress)) {
        throw 'Implementation changed during the verification run.'
    }
    $result = @{ok=$true;directory=$output;smoke=[bool]$Smoke;scope='Reduced GI materials and snapshot coherence'}
}
catch { $result = @{ok=$false;directory=$output;error=$_.Exception.Message} }
finally { $env:GGLAB_GI_MATERIAL_FIXTURE_ROOT = $previousFixtures }
Write-VerificationJson "$output/result.json" $result
$result | ConvertTo-Json -Depth 8
if (-not $result.ok) { exit 1 }
