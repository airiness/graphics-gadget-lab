<# Checks full-resolution bounded high depth and exact sample/batch/tile equivalence. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$output = New-VerificationRunDirectory -RunDirectory $RunDirectory
try {
    foreach ($config in $Configuration) {
        $cases = @()
        foreach ($backend in $Rhi) {
            $directory = "$output/$config/$backend"
            $null = New-Item -ItemType Directory -Path $directory -Force
            $session = 'gi-tiles-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
            $start = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Tiling test must own its session.' }
                $run = $start.runIdentity
                $targets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                    'GI.Reference.Identity','GI.Receiver.HitAlbedo','GI.Reference.PathDiagnostics',
                    'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance') | ForEach-Object {@{resource=$_}}
                $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'}) | ConvertTo-Json -Compress -Depth 8
                $variants = @()
                foreach ($depth in @(1,3)) {
                    foreach ($partition in @(@{tile=0;batch=7},@{tile=4096;batch=7},@{tile=32000;batch=1})) {
                        $variants += @{fixture=8;view='Contract_Sun';depth=$depth;samples=8;
                            batch_samples=$partition.batch;tile_pixels=$partition.tile;seed=61051;
                            albedo=0;secondary_albedo=0;environment=0}
                    }
                }
                foreach ($depth in @(64,128,256)) {
                    $variants += @{fixture=1;view='F1_Plane';depth=$depth;samples=2;batch_samples=64;
                        tile_pixels=0;seed=61051;albedo=0;secondary_albedo=0;environment=0}
                    foreach ($fixture in @(2,3)) {
                        $variants += @{fixture=$fixture;view="F$($fixture)_Floor";depth=$depth;samples=1;batch_samples=64;
                            tile_pixels=0;seed=61051;albedo=0;secondary_albedo=0;environment=0}
                    }
                }
                $index = 0
                foreach ($case in $variants) {
                    $case.directory = "$backend/case-$index"
                    $index++
                    $changes = foreach ($name in @('fixture','depth','samples','batch_samples','tile_pixels','seed','albedo','secondary_albedo','environment')) {
                        $type = if ($name -in @('depth','samples','batch_samples','tile_pixels','seed')) {'uint'} else {'enum'}
                        @{id="gi_reference.$name";type=$type;value=$case[$name]}
                    }
                    $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                        Changes=($changes | ConvertTo-Json -Compress);TimeoutSeconds=$TimeoutSeconds}
                    if (-not $effect.batch.succeeded) { throw 'Tile parameters not consumed.' }
                    $captureDirectory = "$output/$config/$($case.directory)"
                    $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                        SettleFrames=1;View=$case.view;Products=$products;RequiredContentId='gglab.lab.gi_reference';
                        OutputDirectory=$captureDirectory;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$captureDirectory/capture.json" $capture
                    Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Tile capture incomplete.' }
                    $cases += $case
                }
            }
            finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                    Write-VerificationJson "$directory/stop.json" $stop
                    if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Tile session teardown failed.' }
                    $log = Get-Content -LiteralPath $start.outputLog -Raw
                    if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Native tile validation failed.' }
                }
            }
        }
        Write-VerificationJson "$output/$config/cases.json" @($cases)
        $lines = @(& $Python "$PSScriptRoot/ValidateGIReferenceConvergence.py" "$output/$config" --tiles)
        Write-VerificationJson "$output/$config/validation.json" @{exitCode=$LASTEXITCODE;output=($lines -join "`n")}
        if ($LASTEXITCODE -ne 0) { throw 'Tile numerical comparison failed.' }
    }
    $result = @{ok=$true;directory=$output}
}
catch { $result = @{ok=$false;directory=$output;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
$result | ConvertTo-Json -Depth 8
if (-not $result.ok) { exit 1 }
