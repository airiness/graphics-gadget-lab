<#
.SYNOPSIS
Runs the locked Coastal GPU oracle and checks stable/dirty/disabled scene behavior.
The Lab enforces fixed hit references and GPU table digests before frames can continue.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/CoastalRayScene' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Validate-Bundle($Capture, [string]$Directory) {
    Check ($Capture.status -eq 'completed' -and $Capture.publication -eq 'Published') "$Directory-published"
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        $validation = @(& $Python (Join-Path $root "Scripts/Verification/$validator") $Directory 2>&1)
        $code = $LASTEXITCODE
        Write-VerificationJson (Join-Path $Directory "$validator.result.json") @{exitCode=$code;output=($validation -join "`n")}
        Check ($code -eq 0) "$Directory-$validator"
    }
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $case = "$config-$backend"
            $directory = Join-Path $output $case
            $null = New-Item -ItemType Directory -Path $directory
            $session = "coastal-oracle-$runId-$case"
            $start = $null; $caseFailure = $null
            try {
                $start = Invoke-VerificationSession start $session @{ Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.coastal_ray_scene';WindowSize='1280x800';NoDevTools=$true;
                    Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/start.json" $start
                Check ($start.ownership -eq 'created') "$case-owned-run"
                $run = $start.runIdentity
                $warmup = Invoke-VerificationSession capture $session @{ ExpectedRunIdentity=$run;
                    Timing='after-ready';SettleFrames=1;RequiredContentId='gglab.lab.coastal_ray_scene';
                    OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/warmup.json" $warmup
                Check ($warmup.status -eq 'completed' -and $warmup.publication -eq 'Published') "$case-ready-before-parameters"
                $unjittered = Invoke-VerificationSession set $session @{ ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes='[{"id":"view.taa.enabled","type":"bool","value":false}]';TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/unjittered.json" $unjittered
                Check ($unjittered.batch.succeeded) "$case-unjittered-raster-samples"
                $products = @(@{kind='image';source='scene'},@{kind='resource';targets=@(
                    @{resource='CoastalRayScene.Hits'},@{resource='RayScene.GeometryTable'},
                    @{resource='RayScene.InstanceTable'},@{resource='RayScene.SunVisibility'},
                    @{resource='DisplayView.DepthBuffer'})},@{kind='graph'})
                foreach ($view in @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge')) {
                    $captures = "$directory/$view"
                    $capture = Invoke-VerificationSession capture $session @{ ExpectedRunIdentity=$run;
                        Timing='after-ready';SettleFrames=32;View=$view;RequiredContentId='gglab.lab.coastal_ray_scene';
                        Products=($products | ConvertTo-Json -Compress -Depth 8);OutputDirectory=$captures;TimeoutSeconds=$TimeoutSeconds }
                    Write-VerificationJson "$directory/$view.json" $capture
                    Validate-Bundle $capture $captures
                    $metadata = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
                    Check ($metadata.content.assetBundle.id -eq 'gglab.coastal-retreat' -and
                        $metadata.content.assetBundle.revision -eq '79092da0d167d3bd041de9ead925c45d9f5ed8083cb07f2c0f5351afb94467e9') "$case-asset-lock-$view"
                    $graph = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.graph.json' 2) -Raw | ConvertFrom-Json
                    Check (@($graph.compile.passes | Where-Object name -in @('RayScene.Copy','RayScene.BuildBLAS','RayScene.BuildTLAS')).Count -eq 0) "$case-static-cache-$view"
                }
                $sun = @(& $Python "$PSScriptRoot/ValidateCoastalSunVisibility.py" --root $directory --output "$directory/sun-visibility.json" 2>&1)
                if ($LASTEXITCODE -ne 0) { throw "Sun visibility validation failed: $($sun -join ' ')" }
                Check $true "$case-sun-visibility"
                $exercise = Invoke-VerificationSession set $session @{ ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes='[{"id":"coastal_ray_scene.exercise_updates","type":"bool","value":true}]';TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/exercise.json" $exercise
                Check ($exercise.batch.succeeded) "$case-exercise-rebuild"
                $capture = Invoke-VerificationSession capture $session @{ ExpectedRunIdentity=$run;
                    Timing='after-ready';SettleFrames=64;View='Retreat_SeaPassage';
                    OutputDirectory="$directory/exercise";TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/exercise-capture.json" $capture
                Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published') "$case-updates-submitted"
                $disabled = Invoke-VerificationSession set $session @{ ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes='[{"id":"coastal_ray_scene.enabled","type":"bool","value":false}]';TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/disable.json" $disabled
                Check ($disabled.batch.succeeded) "$case-disable-rebuild"
                $capture = Invoke-VerificationSession graph $session @{ ExpectedRunIdentity=$run;
                    Timing='after-ready';SettleFrames=16;OutputDirectory="$directory/disabled";TimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/disabled-graph.json" $capture
                $graph = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.graph.json') -Raw | ConvertFrom-Json
                Check (@($graph.compile.passes | Where-Object name -like '*RayScene*').Count -eq 0 -and
                    @($graph.compile.resources | Where-Object name -like '*RayScene*').Count -eq 0) "$case-disabled-no-ray-work"
            } catch { $caseFailure=$_;throw }
            finally {
                if ($start -and $start.ownership -eq 'created') {
                    try {
                        $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                        Write-VerificationJson "$directory/stop.json" $stop
                        if ($stop.exitCode -ne 0) { throw "Session exited with code $($stop.exitCode)" }
                    }
                    catch {
                        if ($null -eq $caseFailure) { throw }
                        [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                    }
                }
            }
            $log = Get-Content -LiteralPath $start.outputLog -Raw
            Check ($log -notmatch '\[(error|critical)\]|D3D12 ERROR|Validation Error|VUID-|GPU_BASED_VALIDATION_|ASSERT') "$case-native-validation"
            Check ($log -match 'Coastal RayScene update checks: passed=true, blasBuilds=59, tlasBuilds=4\.') "$case-dirty-tlas-reuses-blas"
            $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
        }
        if ('dx12' -in $Rhi -and 'vulkan' -in $Rhi) {
            $sun = @(& $Python "$PSScriptRoot/ValidateCoastalSunVisibility.py" --left "$output/$config-dx12" --right "$output/$config-vulkan" --output "$output/$config-cross-rhi.json" 2>&1)
            if ($LASTEXITCODE -ne 0) { throw "Cross-RHI visibility validation failed: $($sun -join ' ')" }
            Check $true "$config-sun-visibility-agreement"
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
