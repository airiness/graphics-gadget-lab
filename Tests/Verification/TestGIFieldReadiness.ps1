<# Fence-completed field readiness and capture gates; quality failures remain explicit. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    # Where the calibration measures steady change RMS; see GIFieldReadinessCalibration.psm1.
    [ValidateSet('after-timeout','after-handoff')][string]$SteadyCalibration = 'after-handoff',
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
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Readiness verification requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldReadiness" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory
$cases = @(
    @{id='excluded';fixture=14;view='Probe_Contract';counts=@(2,2,2);mustReady=$true},
    @{id='plane';fixture=1;view='F1_Plane';counts=@(13,13,13);mustReady=$false},
    @{id='sealed';fixture=2;view='F2_Interior';counts=@(9,9,9);mustReady=$false},
    @{id='sunroom';fixture=4;view='F4_Floor';counts=@(18,18,18);mustReady=$false},
    @{id='black';fixture=19;view='F2_Interior';counts=@(9,9,9);mustReady=$true}
)
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/cases.json" $cases
    Write-VerificationJson "$output/backends.json" $Rhi
    Write-GIFieldCalibrationRecipe $output $SteadyCalibration
    foreach ($backend in $Rhi) {
        $session = 'gi-ready-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Readiness verification requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll {
                Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
            } -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                New-Item -ItemType Directory -Path "$directory/gated","$directory/diagnostic","$directory/baseline-gated" -Force | Out-Null
                $changes = @(
                    @{id='gi_reference.fixture';type='enum';value=$case.fixture},
                    @{id='gi_reference.seed';type='uint';value=61051},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$true},
                    @{id='gi_reference.probe_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_refresh_noise';type='float';value=0.0},
                    @{id='gi_reference.field_steady_noise';type='float';value=0.0}
                )
                $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
                if (-not $effect.batch.succeeded) { throw 'Readiness parameters were not consumed.' }
                Write-VerificationJson "$directory/effect.json" $effect
                $baselineJob = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                    SettleFrames=0;NoWait=$true;OutputDirectory="$directory/baseline-gated";TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/baseline-admission.json" $baselineJob
                # Frozen-input calibration reports separate RMS noise estimates for
                # refresh and steady alpha. It never changes this owner's thresholds.
                $baseline = Wait-VerificationCondition -Poll {
                    Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                } -Until {
                    param($r)
                    $r.frame -and $r.frame.giField -and
                    ($r.frame.giField.refreshNoiseSamples -ge 30 -or
                        ($r.frame.giField.activeBlocks -eq 0 -and $r.frame.giField.excludedBlocks -gt 0)) -and
                    $r.frame.giField.steadyNoiseSamples -eq 64
                } -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/noise-baseline.json" $baseline
                $baselineResult = Invoke-VerificationSession result $session @{ExpectedRunIdentity=$run;
                    JobId=$baselineJob.jobId;Consume=$true} -AllowFailure
                if ($baselineResult.status -notin @('completed','failed')) { throw 'Baseline readiness gate did not terminate.' }
                Write-VerificationJson "$directory/baseline-gated/receipt.json" $baselineResult
                $noise = $baseline.frame.giField
                $steady = $null
                if ($SteadyCalibration -eq 'after-handoff') {
                    $steady = Measure-GIFieldSteadyAfterHandoff -SetInputs {
                        param($v)
                        $sigmaChanges = @($v.Keys | Sort-Object | ForEach-Object { @{id="gi_reference.$_";type='float';value=$v[$_]} })
                        $sigmaEffect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                            Changes=(ConvertTo-Json -InputObject $sigmaChanges -Compress);TimeoutSeconds=$TimeoutSeconds}
                        if (-not $sigmaEffect.batch.succeeded) { throw 'Steady calibration inputs were not consumed.' }
                        $sigmaEffect
                    } -Status { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                        -Calibration $noise -Directory $directory -TimeoutSeconds $TimeoutSeconds
                }
                $adopted = Get-GIFieldAdoptedInputs hardReset $noise $steady
                $noiseChanges = @($adopted.Keys | Sort-Object | ForEach-Object {
                    @{id="gi_reference.$_";type='float';value=$adopted[$_]} }) +
                    @(@{id='gi_reference.seed';type='uint';value=61052})
                $calibrated = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject $noiseChanges -Compress);TimeoutSeconds=$TimeoutSeconds}
                if (-not $calibrated.batch.succeeded) { throw 'Measured noise profile was not consumed.' }
                Write-VerificationJson "$directory/calibrated-effect.json" $calibrated
                # This must either publish after Ready or explicitly fail on Unconverged.
                $gated = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                    SettleFrames=128;OutputDirectory="$directory/gated";TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                Write-VerificationJson "$directory/gated/receipt.json" $gated
                if ($gated.status -notin @('completed','failed')) { throw 'Readiness capture did not reach a terminal state.' }
                if ($case.mustReady -and $gated.status -ne 'completed') { throw "$($case.id) did not reach Ready." }
                $targets = @(
                    @{resource='GI.Field.Readiness';at=@{before='GI.Field.BlendShared'}},
                    @{resource='GI.Field.Readiness'},
                    @{resource='GI.Field.Classification'}
                )
                if ($case.id -ne 'sunroom') {
                    $targets += @(
                        @{resource='GI.Field.Irradiance';at=@{before='GI.Field.BlendShared'}},
                        @{resource='GI.Field.Irradiance'},
                        @{resource='GI.Field.Distance';at=@{before='GI.Field.BlendShared'}},
                        @{resource='GI.Field.Distance'}
                    )
                }
                $products = @(@{kind='resource';targets=$targets},@{kind='graph'},@{kind='image';source='scene'})
                # Explicit diagnostic timing observes a timed-out estimator without bypassing an after-ready gate.
                $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';SettleFrames=0;
                    Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);
                    OutputDirectory="$directory/diagnostic";TimeoutSeconds=$TimeoutSeconds}
                if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Readiness diagnostic did not publish.' }
                Write-VerificationJson "$directory/diagnostic/receipt.json" $capture
                Write-VerificationJson "$directory/status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Readiness session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Readiness native validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIFieldReadiness.py" @('--root',$output) "$output/oracle-result.json" 'Readiness verification failed'
    Assert-VerificationArtifacts $root $output $Python 'Readiness artifact verification failed'
    $finalSource = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    Assert-VerificationIdentity $source $finalSource 'Readiness sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Readiness binaries changed during verification.' -Depth 2
    Write-VerificationJson "$output/result.json" @{status='passed';backends=$Rhi;productionCases=($Rhi.Count*$cases.Count);
        referenceConvergenceQualified=$false;temporalStabilityQualified=$false;noiseCalibrationQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=$_.Exception.Message}
    throw
}
