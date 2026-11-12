<#
Static-geometry Physical Sky response experiment; deliberately not frozen F7 qualification.
The hard reset and the lighting refresh gate with their own calibration. The lighting
refresh calibrates on a field at the calibration seed that adopted the hard-reset
calibration, settled at steady Ready and then took the case's own step.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$Tap = @('gi','final'),
    [string[]]$Feedback = @('off','on'),
    [string[]]$Step = @('increase'),
    [switch]$QualifyReferences,
    [switch]$TransientAttribution,
    [switch]$ProgressiveRefresh,
    [switch]$ProgressiveSampling,
    [switch]$RapidFeedbackRefresh,
    # Control arm: the old endpoint's hard reset integrates at the refresh hysteresis.
    [switch]$RapidHardReset,
    [switch]$ProgressiveAttribution,
    # Where each reason's steady change RMS is calibrated; see GIFieldReadinessCalibration.psm1.
    [ValidateSet('after-timeout','after-handoff')][string]$SteadyCalibration = 'after-handoff',
    [ValidateRange(1,29)][int]$AttributionStartFrame = 29,
    [uint32[]]$TraceSeed = @(61052),
    [int[]]$PreEventSettleFrames = @(128),
    [ValidateRange(1,131072)][int]$ReferenceInitialSamples = 4096,
    [ValidateRange(2,262144)][int]$ReferenceMaximumSamples = 262144,
    [ValidateSet(16,32)][int]$ReferenceMaximumDepth = 32,
    [ValidateRange(1,3600)][int]$ReferenceMaximumSeconds = 3600,
    [string]$ShaderCacheDirectory,
    [string]$DerivedDataCacheDirectory,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
Import-Module "$PSScriptRoot/GIFieldReadinessCalibration.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Tap = @($Tap | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Feedback = @($Feedback | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Step = @($Step | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$stepCatalog = Get-Content -LiteralPath "$PSScriptRoot/GIFieldLightStepRecipes.json" -Raw | ConvertFrom-Json
if ($stepCatalog.schemaVersion -ne 1 -or $stepCatalog.steps.Count -ne 4 -or -not $Step.Count -or
    @($Step | Where-Object { $_ -cnotin $stepCatalog.steps.id }).Count) { throw 'Invalid bounded step recipe.' }
$selectedSteps = @($Step | ForEach-Object { $id=$_; $stepCatalog.steps | Where-Object { $_.id -ceq $id } })
$referenceLux = @($selectedSteps | ForEach-Object { $_.oldLux; $_.newLux } | Sort-Object -Unique)
$arms = @($selectedSteps | ForEach-Object {
    $recipe=$_
    foreach ($feedbackMode in $Feedback) {
        foreach ($seed in $TraceSeed) {
            foreach ($settle in $PreEventSettleFrames) {
                @{recipe=$recipe;feedback=$feedbackMode;seed=$seed;settle=$settle}
            }
        }
    }
})
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop','Release') -or
    -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -or
    -not $Tap.Count -or @($Tap | Where-Object { $_ -notin @('gi','final') }).Count -or
    -not $Feedback.Count -or @($Feedback | Where-Object { $_ -notin @('off','on') }).Count) {
    throw 'Invalid light-step configuration.'
}
if ($QualifyReferences -and 'gi' -notin $Tap) { throw 'Independent reference qualification requires a GI-only arm.' }
if ($RapidFeedbackRefresh -and (-not $ProgressiveSampling -or $ProgressiveRefresh -or
    $Feedback.Count -ne 1 -or $Feedback[0] -cne 'on')) {
    throw 'Rapid feedback refresh requires progressive sampling, feedback-on only and no progressive refresh.'
}
if ($ProgressiveSampling -and ((-not $ProgressiveRefresh -and -not $RapidFeedbackRefresh) -or $Feedback.Count -ne 1)) {
    throw 'Progressive sampling requires explicit progressive or rapid feedback refresh and one selected feedback arm.'
}
if ($ProgressiveSampling -and $Feedback[0] -ceq 'on' -and
    ($Tap.Count -ne 1 -or $Tap[0] -cne 'gi' -or $TransientAttribution -or $ProgressiveAttribution)) {
    throw 'Progressive sampling feedback-on requires GI-only response without attribution.'
}
$attribution = $TransientAttribution -or $ProgressiveAttribution
if ($AttributionStartFrame -ne 29 -and -not $ProgressiveAttribution) {
    throw 'Earlier attribution exports require explicit ProgressiveAttribution mode.'
}
if ($ProgressiveAttribution -and (-not $ProgressiveRefresh -or $TransientAttribution)) {
    throw 'Progressive attribution requires ProgressiveRefresh and excludes historical attribution.'
}
if ($TransientAttribution) {
    if ($ProgressiveRefresh) { throw 'The historical attribution oracle requires the unchanged fixed refresh filter.' }
}
if ($attribution) {
    if ($QualifyReferences -or $Tap.Count -ne 1 -or $Tap[0] -cne 'gi' -or
        $Feedback.Count -ne 1 -or $Feedback[0] -cne 'off' -or
        @($Step | Where-Object { $_ -cnotin @('small-increase','small-decrease') }).Count -or
        -not $TraceSeed.Count -or -not $PreEventSettleFrames.Count -or
        @($TraceSeed | Where-Object { $_ -eq 61051 }).Count -or
        @($PreEventSettleFrames | Where-Object { $_ -lt 128 -or $_ -gt 512 }).Count -or
        @($TraceSeed | Select-Object -Unique).Count -ne $TraceSeed.Count -or
        @($PreEventSettleFrames | Select-Object -Unique).Count -ne $PreEventSettleFrames.Count) {
        throw 'Attribution requires small steps, GI-only, feedback-off, distinct trace seeds (not calibration seed 61051), and 128..512 settling frames; no reference qualification.'
    }
} elseif (-not ($ProgressiveRefresh -or $RapidFeedbackRefresh) -and ($TraceSeed.Count -ne 1 -or $TraceSeed[0] -ne 61052 -or
          $PreEventSettleFrames.Count -ne 1 -or $PreEventSettleFrames[0] -ne 128)) {
    throw 'Seed/settling variations require explicit transient attribution mode.'
}
if (($ProgressiveRefresh -or $RapidFeedbackRefresh) -and (-not $TraceSeed.Count -or -not $PreEventSettleFrames.Count -or
    @($TraceSeed | Where-Object { $_ -eq 61051 }).Count -or
    @($PreEventSettleFrames | Where-Object { $_ -lt 128 -or $_ -gt 512 }).Count -or
    @($TraceSeed | Select-Object -Unique).Count -ne $TraceSeed.Count -or
    @($PreEventSettleFrames | Select-Object -Unique).Count -ne $PreEventSettleFrames.Count)) {
    throw 'Refresh studies require distinct trace seeds (not calibration seed 61051) and 128..512 settling frames.'
}
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
            elseif (Test-GIFieldSigmaInput $_) { 'float' }
            else { 'uint' }
        @{id="gi_reference.$_";type=$type;value=$Values[$_]}
    })
    $effect = Invoke-VerificationSession set $script:session @{ExpectedRunIdentity=$script:run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Light-step inputs were not consumed.' }
    $effect
}
function Get-Status {
    Invoke-VerificationSession status $script:session @{ExpectedRunIdentity=$script:run}
}
# Settles a new field at steady Ready, takes the case's step and returns its lighting refresh epoch.
function Start-LightingCalibration([string]$Directory, [string]$Name, $Replaced, [int]$Settle, [int]$NewLux) {
    $settled = Wait-VerificationCondition -Poll { Get-Status } -Until { param($r) $f=$r.frame.giField
        $f -and $f.generation -ne $Replaced -and $r.frame.ready -and $f.state -eq 'Ready' -and -not $f.refreshing -and
            $f.completedUpdates -ge $f.readyUpdate + $Settle } -TimeoutSeconds $TimeoutSeconds
    Write-GIFieldCalibrationJson "$Directory/$Name-settled.json" $settled
    Write-VerificationJson "$Directory/$Name-step.json" (Set-Inputs @{sun_step_lux=$NewLux})
    $field = $settled.frame.giField
    $started = Wait-VerificationCondition -Poll { Get-Status } -Until { param($r) $f=$r.frame.giField
        $f -and $f.generation -eq $field.generation -and
            $f.summary.field.refreshEpoch -gt $field.summary.field.refreshEpoch } -TimeoutSeconds $TimeoutSeconds
    return $started.frame.giField.summary.field.refreshEpoch
}
$cases = @()
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-GIFieldCalibrationRecipe $output $SteadyCalibration
    if ($QualifyReferences) {
        foreach ($feedbackMode in $Feedback) {
            $mode = if ($feedbackMode -eq 'on') { 'multibounce' } else { 'single-bounce' }
            foreach ($lux in $referenceLux) {
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
            if ($DerivedDataCacheDirectory) {
                $null = New-Item -ItemType Directory -Path $state -Force
                Copy-Item -LiteralPath "$DerivedDataCacheDirectory/$backend/DerivedDataCache" `
                    -Destination "$state/DerivedDataCache" -Recurse
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
            Write-VerificationJson "$output/$backend/experiment.json" (Set-Inputs @{
                fixture=4;environment=3;lighting_step_experiment=$true;field_enabled=$false;field_feedback=$false})
            foreach ($arm in $arms) {
                $feedbackMode = $arm.feedback
                $recipe = $arm.recipe
                $stepId = $recipe.id
                foreach ($tapMode in $Tap) {
                    $caseName = "$stepId-$feedbackMode-$tapMode"
                    if ($attribution -or $ProgressiveRefresh -or $RapidFeedbackRefresh) { $caseName += "-s$($arm.seed)-p$($arm.settle)" }
                    $directory = "$output/$backend/$caseName"
                    $null = New-Item -ItemType Directory -Path $directory -Force
                    $case = @{backend=$backend;configuration=$Configuration[0];feedback=($feedbackMode -eq 'on');tap=$tapMode;
                        fixture=4;environment=3;view='F4_Floor';frameCount=360;eventFrame=30;
                        step=$stepId;oldLux=$recipe.oldLux;newLux=$recipe.newLux;
                        directory="$backend/$caseName"}
                    if ($ProgressiveRefresh -or $RapidFeedbackRefresh) {
                        $case.progressiveRefresh = [bool]$ProgressiveRefresh
                        $case.traceSeed = $arm.seed
                        $case.preEventSettleFrames = $arm.settle
                    }
                    if ($ProgressiveSampling) { $case.progressiveSampling = $true }
                    if ($RapidFeedbackRefresh) { $case.rapidFeedbackRefresh = $true }
                    if ($RapidHardReset) { $case.rapidHardReset = $true }
                    if ($TransientAttribution) {
                        $case.transientAttribution = $true
                        $case.traceSeed = $arm.seed
                        $case.preEventSettleFrames = $arm.settle
                    }
                    if ($ProgressiveAttribution) {
                        $case.progressiveAttribution = $true
                        if ($AttributionStartFrame -ne 29) { $case.attributionStartFrame = $AttributionStartFrame }
                    }
                    $cases += $case
                    Write-VerificationJson "$output/cases.json" $cases
                    # Consume the starting illumination before constructing the calibration field.
                    Write-VerificationJson "$directory/old-lighting.json" (Set-Inputs @{sun_step_lux=$case.oldLux})
                    Write-VerificationJson "$directory/inputs.json" (Set-Inputs (@{fixture=4;environment=3;
                        lighting_step_experiment=$true;albedo=0;secondary_albedo=0;ev100=10;field_enabled=$true;
                        field_feedback=$case.feedback;field_sampling=$true;field_readiness=$true;
                        field_progressive_refresh=[bool]$ProgressiveRefresh;
                        field_progressive_sampling=[bool]$ProgressiveSampling;
                        field_rapid_hard_reset=[bool]$RapidHardReset;
                        receiver_diagnostics=$true;field_irradiance_width=0;probe_enabled=$true;accumulate=$true;
                        seed=61051} + (Get-GIFieldUncalibratedInputs hardReset) + (Get-GIFieldUncalibratedInputs lighting)))
                    $noise = Wait-VerificationCondition -Poll { Get-Status } `
                        -Until { param($r) $f=$r.frame.giField; $f -and $f.refreshNoiseSamples -ge 30 -and
                            $f.steadyNoiseSamples -eq 64 } -TimeoutSeconds $TimeoutSeconds
                    Write-VerificationJson "$directory/noise.json" $noise
                    $setCalibration = { param($v) Set-Inputs $v }
                    $hardSteady = $null
                    if ($SteadyCalibration -eq 'after-handoff') {
                        $hardSteady = Measure-GIFieldSteadyAfterHandoff -SetInputs $setCalibration -Status { Get-Status } `
                            -Calibration $noise.frame.giField -Directory $directory -TimeoutSeconds $TimeoutSeconds
                    }
                    $hardSigma = Get-GIFieldAdoptedInputs hardReset $noise.frame.giField $hardSteady
                    $replaced = if ($hardSteady) { $hardSteady.generation } else { $noise.frame.giField.generation }
                    Write-VerificationJson "$directory/lighting-field.json" (Set-Inputs $hardSigma)
                    $lightingEpoch = Start-LightingCalibration $directory 'lighting' $replaced $arm.settle $case.newLux
                    $lightingNoise = Wait-VerificationCondition -Poll { Get-Status } -Until { param($r) $f=$r.frame.giField
                        $f -and $f.summary.field.refreshEpoch -eq $lightingEpoch -and $f.refreshNoiseSamples -ge 30 -and
                            $f.steadyNoiseSamples -eq 64 } -TimeoutSeconds $TimeoutSeconds
                    Write-VerificationJson "$directory/lighting-noise.json" $lightingNoise
                    Write-VerificationJson "$directory/lighting-reset.json" (Set-Inputs @{sun_step_lux=$case.oldLux})
                    $lightingSteady = $null
                    if ($SteadyCalibration -eq 'after-handoff') {
                        $prepare = { param($replaced)
                            Start-LightingCalibration $directory 'lighting-handoff' $replaced $arm.settle $case.newLux }
                        $lightingSteady = Measure-GIFieldSteadyAfterHandoff -SetInputs $setCalibration `
                            -Status { Get-Status } -Calibration $lightingNoise.frame.giField -Directory $directory `
                            -Reason lighting -Prepare $prepare -TimeoutSeconds $TimeoutSeconds
                        Write-VerificationJson "$directory/lighting-handoff-reset.json" (Set-Inputs @{sun_step_lux=$case.oldLux})
                    }
                    Write-VerificationJson "$directory/calibration.json" (Set-Inputs (@{seed=$arm.seed} + $hardSigma +
                        (Get-GIFieldAdoptedInputs lighting $lightingNoise.frame.giField $lightingSteady)))
                    $mask = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View='F4_Floor';
                        Timing='after-ready';SettleFrames=128;Targets=$(if ($attribution) {
                            '[{"resource":"GI.Receiver.Position"},{"resource":"GI.Receiver.VertexNormal"},{"resource":"GI.Receiver.ShadingNormal"}]'
                        } else { '[{"resource":"GI.Receiver.Position"}]' });
                        OutputDirectory="$directory/mask";TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/mask/receipt.json" $mask
                    if ($mask.status -ne 'completed' -or $mask.publication -ne 'Published') { throw 'Receiver mask did not publish.' }
                    if ($attribution) {
                        & $Python "$PSScriptRoot/AnalyzeGIFieldTransient.py" --geometry "$directory/mask" --output "$directory/layers.json"
                        if ($LASTEXITCODE -ne 0) { throw 'Unable to plan complete ROI cage layers.' }
                        $case.atlasSlices = (Get-Content "$directory/layers.json" -Raw | ConvertFrom-Json).atlasSlices
                        Write-VerificationJson "$output/cases.json" $cases
                    }
                    if ($tapMode -eq 'final') {
                        Write-VerificationJson "$directory/final-mode.json" (Set-Inputs @{receiver_diagnostics=$false})
                    }
                    $ready = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View='F4_Floor';
                        SettleFrames=$arm.settle;OutputDirectory="$directory/ready";TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/ready/receipt.json" $ready
                    if ($ready.status -ne 'completed' -or $ready.publication -ne 'Published') { throw 'Old endpoint did not become Ready.' }
                    $tapResource = if ($tapMode -eq 'gi') { 'GI.Field.ReceiverIrradiance' } else { 'TAA.ResolvedSceneColor' }
                    $targets = @(@{resource=$tapResource},@{resource='GI.Field.Readiness'})
                    $exports = @(0..359 | ForEach-Object {
                        $frameTargets = $targets
                        if ($attribution -and (($_ -ge $AttributionStartFrame -and $_ -le 80) -or $_ -eq 359)) {
                            # Eight payload slots: no image on these frames. Geometry is captured
                            # separately from the same sealed authored view before the event.
                            $frameTargets = $targets + @(
                                @{resource='GI.Field.Irradiance';at=@{before='GI.Field.BlendShared'};slices=$case.atlasSlices},
                                @{resource='GI.Field.Irradiance';slices=$case.atlasSlices},
                                @{resource='GI.Field.Distance';slices=$case.atlasSlices},
                                @{resource='GI.Field.Classification';slices=$case.atlasSlices},@{resource='GI.Field.Rays'},
                                @{resource='GI.Field.ReceiverQuery'})
                        }
                        @{frame=$_;targets=$frameTargets;graph=($_ -in @(0,30,359))}
                    })
                    Write-VerificationJson "$directory/exports.json" $exports
                    $events = @(@{frame=30;set=@{'gi_reference.sun_step_lux'=$case.newLux};expectEffects=@('lighting')})
                    $sequence = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                        CameraPath='GI_LightStep_F4_Floor';CaptureFrames=$(if ($attribution) { '0' } else { '0,29,359' });
                        Parameters=(ConvertTo-Json -Compress @{'view.taa.enabled'=($tapMode -eq 'final')});Events=(ConvertTo-Json -InputObject $events -Depth 8 -Compress);
                        ExportsFile="$directory/exports.json";OutputDirectory="$directory/samples";
                        TimeoutSeconds=$TimeoutSeconds;Label='light-step'}
                    Write-VerificationJson "$directory/sequence.json" $sequence
                    if ($sequence.sequence.state -ne 'completed' -or $sequence.sequence.submittedFrames -ne 360 -or
                        $sequence.sequence.parameterPublication.outcome -ne 'Published') { throw 'Light-step sequence did not publish the complete range.' }
                    $repeat = Set-Inputs @{sun_step_lux=$case.newLux}
                    Write-VerificationJson "$directory/repeat.json" $repeat
                    if ($repeat.batch.items[0].state -ne 'NoChange' -or $null -ne $repeat.batch.items[0].firstConsumed) {
                        throw 'Identical sun illuminance drifted or started a new lighting effect.'
                    }
                    Write-VerificationJson "$directory/reset.json" (Set-Inputs @{sun_step_lux=$case.oldLux})
                }
            }
        } catch { $failure=$_; throw }
        finally {
            if ($start) {
                try { $null = Stop-VerificationSession $session -StartedSession $start `
                    -ReceiptPath "$output/$backend/stop.json" -RequireCleanExit -ForceOwnedProcess }
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
    if ($attribution) {
        & $Python "$PSScriptRoot/AnalyzeGIFieldTransient.py" --root $output --output "$output/attribution"
        if ($LASTEXITCODE -ne 0) { throw 'Transient attribution contracts failed.' }
        Assert-VerificationIdentity $source (Source-Identity) 'Sources changed during attribution.'
        Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) `
            'Binaries changed during attribution.' -Depth 2
    }
    $passed = $validation.observedResponseTargetsPassed -and (-not $QualifyReferences -or $validation.responseQualityQualified)
    $status = if ($attribution) { 'engineering-complete' } elseif ($passed) { 'passed' } elseif ($validation.referenceStatus -eq 'ReferenceUnconverged') {
        'ReferenceUnconverged'
    } else { 'failed' }
    $result = @{ok=[bool]($passed -or $attribution);status=$status;experimentOnly=$true;
        observedResponseTargetsPassed=$validation.observedResponseTargetsPassed;engineeringOnly=[bool]$attribution;
        responseQualityQualified=$validation.responseQualityQualified;referenceStatus=$validation.referenceStatus;
        referenceCorrelatedReadinessQualified=$validation.referenceCorrelatedReadinessQualified;
        wholeG3Qualified=$false;progressiveRefresh=[bool]$ProgressiveRefresh;steadyCalibration=$SteadyCalibration;
        progressiveSampling=[bool]$ProgressiveSampling;rapidFeedbackRefresh=[bool]$RapidFeedbackRefresh;
        rapidHardReset=[bool]$RapidHardReset;directory=$output}
} catch { $result = @{ok=$false;status='failed';experimentOnly=$true;responseQualityQualified=$false;
    wholeG3Qualified=$false;directory=$output;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 10 -Compress))
if (-not $result.ok) { exit 1 }
