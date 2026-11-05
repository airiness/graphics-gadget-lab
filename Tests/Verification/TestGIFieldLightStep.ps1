<# Static-geometry Physical Sky response experiment; deliberately not frozen F7 qualification. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$Tap = @('gi','final'),
    [string[]]$Feedback = @('off','on'),
    [switch]$QualifyReferences,
    [ValidateRange(1,131072)][int]$ReferenceInitialSamples = 4096,
    [ValidateRange(2,262144)][int]$ReferenceMaximumSamples = 262144,
    [ValidateSet(16,32)][int]$ReferenceMaximumDepth = 32,
    [ValidateRange(1,3600)][int]$ReferenceMaximumSeconds = 3600,
    [string]$ShaderCacheDirectory,
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
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Tap = @($Tap | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Feedback = @($Feedback | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop','Release') -or
    -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -or
    -not $Tap.Count -or @($Tap | Where-Object { $_ -notin @('gi','final') }).Count -or
    -not $Feedback.Count -or @($Feedback | Where-Object { $_ -notin @('off','on') }).Count) {
    throw 'Invalid light-step configuration.'
}
if ($QualifyReferences -and 'gi' -notin $Tap) { throw 'Independent reference qualification requires a GI-only arm.' }
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldLightStep" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
function Set-Inputs($Values) {
    $changes = @($Values.Keys | Sort-Object | ForEach-Object {
        $type = if ($_ -in @('fixture','environment','albedo','secondary_albedo','ev100','sun_step_lux','field_irradiance_width')) { 'enum' }
            elseif ($Values[$_] -is [bool]) { 'bool' }
            elseif ($_ -in @('field_refresh_noise','field_steady_noise')) { 'float' }
            else { 'uint' }
        @{id="gi_reference.$_";type=$type;value=$Values[$_]}
    })
    $effect = Invoke-VerificationSession set $script:session @{ExpectedRunIdentity=$script:run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Light-step inputs were not consumed.' }
    $effect
}
$cases = @()
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    if ($QualifyReferences) {
        foreach ($feedbackMode in $Feedback) {
            $mode = if ($feedbackMode -eq 'on') { 'multibounce' } else { 'single-bounce' }
            foreach ($lux in @(10000,20000)) {
                $parameters = @{Configuration=$Configuration;Rhi=$Rhi;Views=@('F4_Floor');FixtureExtent='320x200';
                    LightingStepLux=$lux;SingleBounce=($feedbackMode -eq 'off');Python=$Python;
                    InitialF4Samples=$ReferenceInitialSamples;MaximumSamples=$ReferenceMaximumSamples;
                    MaximumDepth=$ReferenceMaximumDepth;MaximumSeconds=$ReferenceMaximumSeconds;
                    TimeoutSeconds=$TimeoutSeconds;RunDirectory="$output/refs/$feedbackMode-$lux"}
                if ($ShaderCacheDirectory) { $parameters.ShaderCacheDirectory = $ShaderCacheDirectory }
                $execution = Invoke-VerificationScript "$PSScriptRoot/TestGIReferenceConvergence.ps1" $parameters
                Write-VerificationJson "$output/refs/$feedbackMode-$lux/runner.json" $execution
                if ($execution.exitCode -ne 0) { throw "Reference acquisition failed: $mode $lux lux" }
                # ReferenceUnconverged is retained as unavailable quality, not a GI failure.
            }
        }
    }
    foreach ($backend in $Rhi) {
        $script:session = 'gi-step-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null; $failure = $null
        try {
            $state = "$output/$backend/state"
            if ($ShaderCacheDirectory) {
                $null = New-Item -ItemType Directory -Path $state -Force
                foreach ($name in @('ShaderCache','ShaderArtifacts')) {
                    Copy-Item -LiteralPath "$ShaderCacheDirectory/$backend/$name" -Destination "$state/$name" -Recurse
                }
            }
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot=$state;StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Light-step verification requires an owned session.' }
            $script:run = $start.runIdentity
            $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            if (-not $status.capabilities.sequenceGIFieldRefresh) { throw 'The executable has no GI field refresh binding.' }
            foreach ($feedbackMode in $Feedback) {
                foreach ($tapMode in $Tap) {
                    $directory = "$output/$backend/$feedbackMode-$tapMode"
                    $null = New-Item -ItemType Directory -Path $directory -Force
                    $case = @{backend=$backend;configuration=$Configuration[0];feedback=($feedbackMode -eq 'on');tap=$tapMode;
                        fixture=4;environment=3;view='F4_Floor';frameCount=360;eventFrame=30;oldLux=10000;newLux=20000;
                        directory="$backend/$feedbackMode-$tapMode"}
                    $cases += $case
                    Write-VerificationJson "$output/cases.json" $cases
                    Write-VerificationJson "$directory/inputs.json" (Set-Inputs @{fixture=4;environment=3;lighting_step_experiment=$true;
                        albedo=0;secondary_albedo=0;ev100=10;field_enabled=$true;field_feedback=$case.feedback;
                        field_sampling=$true;field_readiness=$true;receiver_diagnostics=$true;
                        field_irradiance_width=0;probe_enabled=$true;accumulate=$true;seed=61051;
                        field_refresh_noise=0.0;field_steady_noise=0.0})
                    $noise = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                        -Until { param($r) $f=$r.frame.giField; $f -and $f.refreshNoiseSamples -ge 30 -and
                            $f.steadyNoiseSamples -eq 64 } -TimeoutSeconds $TimeoutSeconds
                    Write-VerificationJson "$directory/noise.json" $noise
                    Write-VerificationJson "$directory/calibration.json" (Set-Inputs @{seed=61052;
                        field_refresh_noise=[float]$noise.frame.giField.refreshNoiseEstimate;
                        field_steady_noise=[float]$noise.frame.giField.steadyNoiseEstimate})
                    $mask = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View='F4_Floor';
                        Timing='after-ready';SettleFrames=128;Targets='[{"resource":"GI.Receiver.Position"}]';
                        OutputDirectory="$directory/mask";TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/mask/receipt.json" $mask
                    if ($mask.status -ne 'completed' -or $mask.publication -ne 'Published') { throw 'Receiver mask did not publish.' }
                    if ($tapMode -eq 'final') {
                        Write-VerificationJson "$directory/final-mode.json" (Set-Inputs @{receiver_diagnostics=$false})
                    }
                    $ready = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View='F4_Floor';
                        SettleFrames=128;OutputDirectory="$directory/ready";TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/ready/receipt.json" $ready
                    if ($ready.status -ne 'completed' -or $ready.publication -ne 'Published') { throw 'Old endpoint did not become Ready.' }
                    $tapResource = if ($tapMode -eq 'gi') { 'GI.Field.ReceiverIrradiance' } else { 'TAA.ResolvedSceneColor' }
                    $targets = @(@{resource=$tapResource},@{resource='GI.Field.Readiness'})
                    $exports = @(0..359 | ForEach-Object { @{frame=$_;targets=$targets;graph=($_ -in @(0,30,359))} })
                    Write-VerificationJson "$directory/exports.json" $exports
                    $events = @(@{frame=30;set=@{'gi_reference.sun_step_lux'=20000};expectEffects=@('lighting')})
                    $sequence = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                        CameraPath='GI_LightStep_F4_Floor';CaptureFrames='0,29,359';
                        TemporalAA="enabled=$($tapMode -eq 'final')";Events=(ConvertTo-Json -InputObject $events -Depth 8 -Compress);
                        ExportsFile="$directory/exports.json";OutputDirectory="$directory/samples";
                        TimeoutSeconds=$TimeoutSeconds;Label='light-step'}
                    Write-VerificationJson "$directory/sequence.json" $sequence
                    if ($sequence.sequence.state -ne 'completed' -or $sequence.sequence.submittedFrames -ne 360 -or
                        $sequence.sequence.parameterPublication.outcome -ne 'Published') { throw 'Light-step sequence did not publish the complete range.' }
                    $repeat = Set-Inputs @{sun_step_lux=20000}
                    Write-VerificationJson "$directory/repeat.json" $repeat
                    if ($repeat.batch.items[0].state -ne 'NoChange' -or $null -ne $repeat.batch.items[0].firstConsumed) {
                        throw 'Identical sun illuminance drifted or started a new lighting effect.'
                    }
                    Write-VerificationJson "$directory/reset.json" (Set-Inputs @{sun_step_lux=10000})
                }
            }
        } catch { $failure=$_; throw }
        finally {
            if ($start) {
                try { $null = Stop-VerificationSession $session -StartedSession $start `
                    -ReceiptPath "$output/$backend/stop.json" -RequireCleanExit }
                catch { if ($null -eq $failure) { throw }; [Console]::Error.WriteLine($_.Exception.Message) }
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
            }
        }
        $log = Get-Content -LiteralPath "$output/$backend/output.log" -Raw
        if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native validation failed: $backend" }
    }
    $finalSource = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $source $finalSource 'Light-step sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Light-step binaries changed during verification.' -Depth 2
    Assert-VerificationArtifacts $root $output $Python 'Light-step artifact audit failed'
    $analysisOptions = @('--root',$output)
    if ($QualifyReferences) { $analysisOptions += '--qualify-references' }
    & $Python "$PSScriptRoot/ValidateGIFieldLightStep.py" @analysisOptions
    if ($LASTEXITCODE -ne 0) { throw 'Light-step evidence validation failed.' }
    Assert-VerificationIdentity $source (Source-Identity) 'Light-step sources changed during analysis.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) `
        'Light-step binaries changed during analysis.' -Depth 2
    $validation = Get-Content -LiteralPath "$output/validation.json" -Raw | ConvertFrom-Json
    $passed = $validation.observedResponseTargetsPassed -and (-not $QualifyReferences -or $validation.responseQualityQualified)
    $status = if ($passed) { 'passed' } elseif ($validation.referenceStatus -eq 'ReferenceUnconverged') {
        'ReferenceUnconverged'
    } else { 'failed' }
    $result = @{ok=[bool]$passed;status=$status;experimentOnly=$true;
        responseQualityQualified=$validation.responseQualityQualified;referenceStatus=$validation.referenceStatus;
        referenceCorrelatedReadinessQualified=$validation.referenceCorrelatedReadinessQualified;
        wholeG3Qualified=$false;directory=$output}
} catch { $result = @{ok=$false;status='failed';experimentOnly=$true;responseQualityQualified=$false;
    wholeG3Qualified=$false;directory=$output;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 10 -Compress))
if ($result.status -ne 'passed') { exit 1 }
