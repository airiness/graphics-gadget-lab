<# Probe update numerical contracts and Coastal memory evidence; timing runs are separate. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [switch]$Smoke,
    [switch]$SkipCoastal,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeUpdate" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Check-Python([string]$Script, [string[]]$Arguments, [string]$Receipt) {
    $lines = @(& $Python $Script @Arguments)
    $code = $LASTEXITCODE
    Write-VerificationJson $Receipt @{exitCode=$code;output=($lines -join "`n")}
    if ($code -ne 0) { throw "Probe verification failed: $Script" }
}
function Source-Identity {
    $names = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification Schemas Projects PropertySheets)
    if ($LASTEXITCODE -ne 0) { throw 'Source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash} })
}
function Binary-Identity([string]$Config) {
    return @(@('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') | ForEach-Object {
        [ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath "$root/Build/Output/x64/$Config/$_" -Algorithm SHA256).Hash}
    })
}
function Set-Inputs([string]$Session, [string]$Run, [hashtable]$Values) {
    $changes = foreach ($key in $Values.Keys) {
        @{id="gi_reference.$key";type=$(if ($key -in @('fixture','ev100','environment')) {'enum'} elseif ($Values[$key] -is [bool]) {'bool'} else {'uint'});value=$Values[$key]}
    }
    $effect=Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($changes) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Probe parameter batch was not consumed.' }
    return $effect
}
$cases=@()
try {
    Check-Python "$PSScriptRoot/ValidateGIProbeUpdate.py" @('--self-test') "$output/cpu-contracts.json"
    $source=Source-Identity
    Write-VerificationJson "$output/source-identity.json" $source
    foreach ($config in $Configuration) {
        New-Item -ItemType Directory -Path "$output/$config" -Force | Out-Null
        $binaries=Binary-Identity $config
        Write-VerificationJson "$output/$config/binaries.json" $binaries
        foreach ($backend in $Rhi) {
            $directory="$output/$config/$backend"
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
            $session='probe-update-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
            $start=$null
            try {
                $start=Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;Lab='gglab.lab.gi_reference';
                    WindowSize='1280x800';NoDevTools=$true;Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Probe verification requires an owned run.' }
                $run=$start.runIdentity
                $warmup=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                    SettleFrames=1;OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds}
                if ($warmup.publication -ne 'Published') { throw 'Probe content warmup failed.' }
                $matrix=@()
                $rayCounts=if ($Smoke) { @(256) } else { @(64,128,192,256) }
                foreach ($fixture in @(14,16)) {
                    foreach ($rays in $rayCounts) {
                        foreach ($shared in @($false,$true)) {
                            foreach ($schedule in @(@{divisor=1;updates=1;refresh=0},@{divisor=4;updates=1;refresh=0},
                                @{divisor=4;updates=4;refresh=0},@{divisor=4;updates=6;refresh=5})) {
                                $matrix+=@{fixture=$fixture;rays=$rays;shared=$shared;divisor=$schedule.divisor;
                                    updates=$schedule.updates;refresh=$schedule.refresh}
                            }
                        }
                    }
                }
                foreach ($fixture in @(14,16)) {
                    foreach ($divisor in @(1,4)) {
                        $matrix+=@{fixture=$fixture;rays=256;shared=$true;divisor=$divisor;updates=$divisor;refresh=0;rotate=$true}
                    }
                }
                if (-not $SkipCoastal) {
                    $coastalRays=if ($Smoke) { @(256) } else { @(128,192,256) }
                    foreach ($rays in $coastalRays) {
                        foreach ($shared in @($false,$true)) {
                            foreach ($divisor in @(1,4)) {
                                $matrix+=@{fixture=15;rays=$rays;shared=$shared;divisor=$divisor;updates=$divisor;refresh=0}
                            }
                        }
                    }
                }
                if (-not $SkipCoastal) {
                    # Two successive full refreshes on the same nonzero-hysteresis atlases.
                    foreach ($updates in @(6,11)) {
                        $matrix+=@{fixture=15;rays=256;shared=$true;divisor=4;updates=$updates;refresh=5}
                    }
                }
                $index=0
                foreach ($entry in $matrix) {
                    $case=@{directory="$config/$backend/c$index";rhi=$backend;fixture=$entry.fixture;rays=$entry.rays;
                        shared=$entry.shared;divisor=$entry.divisor;updates=$entry.updates;refresh=$entry.refresh;rotate=$entry.ContainsKey('rotate')}
                    $path="$output/$($case.directory)"
                    New-Item -ItemType Directory -Path $path -Force | Out-Null
                    $effect=Set-Inputs $session $run @{fixture=$case.fixture;probe_rays=$case.rays;probe_divisor=$case.divisor;
                        probe_updates=$case.updates;probe_refresh_period=$case.refresh;probe_shared_blend=$case.shared;
                        probe_rotate=$case.rotate;probe_traversal_only=$false;probe_enabled=$true;ev100=15}
                    Write-VerificationJson "$path/effect.json" $effect
                    $products=@(@{kind='resource';targets=@('Rays','Irradiance','Distance','State' | ForEach-Object {@{resource="GI.ProbeUpdate.$_"}})},@{kind='graph'}) | ConvertTo-Json -Depth 8 -Compress
                    $capture=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=12;
                        Products=$products;OutputDirectory=$path;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$path/capture.json" $capture
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Probe arrays were not published.' }
                    $status=Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                    Write-VerificationJson "$path/status.json" $status
                    $cases+=$case
                    Write-VerificationJson "$output/cases.json" @($cases)
                    Write-Verbose "$config/$backend probe case $index published"
                    $index++
                }
                foreach ($accumulate in @($true,$false)) {
                    $name=if ($accumulate) {'reference-bounded'} else {'reference-stateless'}
                    $path="$directory/$name"
                    New-Item -ItemType Directory -Path $path -Force | Out-Null
                    $effect=Set-Inputs $session $run @{fixture=7;environment=1;samples=4;batch_samples=4;accumulate=$accumulate}
                    Write-VerificationJson "$path/effect.json" $effect
                    $capture=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=12;
                        Products='[{"kind":"resource","targets":[{"resource":"GI.Reference.Irradiance"},{"resource":"GI.Reference.Diagnostics"}]}]';
                        OutputDirectory=$path;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$path/capture.json" $capture
                    if ($capture.publication -ne 'Published') {throw 'Reference cost control was not published'}
                }
                if (-not $SkipCoastal) {
                # Activation/release rebuild safely; translation is an Immediate CPU mutation
                # projected from frozen frame geometry, with RayScene owning immutable replacement.
                $effect=Set-Inputs $session $run @{fixture=15;probe_enabled=$false;probe_allocation_diagnostics=$true;
                    probe_updates=1;probe_divisor=1;probe_rays=256;probe_refresh_period=0;accumulate=$true}
                foreach ($step in @(@{id='off';values=@{}},@{id='activate';values=@{probe_enabled=$true}},
                    @{id='transform';values=@{probe_transform=1}},@{id='release';values=@{probe_enabled=$false}},
                    @{id='reactivate';values=@{probe_enabled=$true}})) {
                    if ($step.values.Count -gt 0) { $effect=Set-Inputs $session $run $step.values }
                    $capture=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=12;
                        Products='[{"kind":"graph"}]';OutputDirectory="$directory/life-$($step.id)";TimeoutSeconds=$TimeoutSeconds}
                    if ($capture.publication -ne 'Published') { throw 'Probe lifecycle graph was not published.' }
                    Write-VerificationJson "$directory/life-$($step.id)/effect.json" $effect
                    Write-VerificationJson "$directory/life-$($step.id)/capture.json" $capture
                    $status=Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                    Write-VerificationJson "$directory/life-$($step.id)/status.json" $status
                }
                }
            } finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop=Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                    Write-VerificationJson "$directory/stop.json" $stop
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                    if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Probe verification shutdown failed.' }
                    $log=Get-Content -LiteralPath $start.outputLog -Raw
                    if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Probe native/runtime validation failed.' }
                }
                Write-VerificationJson "$output/cases.json" @($cases)
            }
        }
        $finalBinaries=Binary-Identity $config
        if ((ConvertTo-Json -InputObject @($binaries) -Compress) -cne (ConvertTo-Json -InputObject @($finalBinaries) -Compress)) { throw 'Compiler/product changed during probe verification.' }
    }
    Check-Python "$PSScriptRoot/ValidateGIProbeUpdate.py" @('--root',$output) "$output/oracle-result.json"
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        Check-Python "$root/Scripts/Verification/$validator" @($output) "$output/$validator.result.json"
    }
    $finalSource=Source-Identity
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    if ((ConvertTo-Json -InputObject @($source) -Depth 4 -Compress) -cne (ConvertTo-Json -InputObject @($finalSource) -Depth 4 -Compress)) { throw 'Sources changed during probe verification.' }
    Write-VerificationJson "$output/result.json" @{status='passed';observations=$cases.Count;productionFieldQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=[string]$_} | ConvertTo-Json -Compress))
    exit 1
}
[Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
