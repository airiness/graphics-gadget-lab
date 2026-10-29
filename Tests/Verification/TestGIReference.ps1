<#
.SYNOPSIS
Checks stateless one-bounce F1/F2 reference exports. Does not qualify DDGI or Final.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/GIReference' }
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
                $targets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                    'GI.Reference.Identity','GI.Receiver.FaceNormal','GI.Receiver.ShadingNormal',
                    'GI.Receiver.HitAlbedo','GI.Reference.LambertRadiance') | ForEach-Object { @{resource=$_} }
                $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'})
                foreach ($observation in $observations) {
                    $changes = @()
                    foreach ($key in @('fixture','samples','seed','albedo','ev100')) {
                        $type = if ($key -in @('samples','seed')) { 'uint' } else { 'enum' }
                        $changes += @{id="gi_reference.$key";type=$type;value=$observation[$key]}
                    }
                    $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                        Changes=($changes | ConvertTo-Json -Compress -Depth 6);TimeoutSeconds=$TimeoutSeconds}
                    Check ($effect.batch.succeeded) "$($observation.directory)-parameters-consumed"
                    $captureDirectory = Join-Path $configDirectory $observation.directory
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
            "$configDirectory/validation.json" "$config-F1-F2-numerical-gates"
        foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
            Check-Python "$root/Scripts/Verification/$validator" $configDirectory `
                "$configDirectory/$validator.result.json" "$config-$validator"
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;scope='F1/F2 one-bounce, not DDGI or Final'}
}
catch {
    $result = @{ok=$false;directory=$output;checks=$checks;error=$_.Exception.Message}
}
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
