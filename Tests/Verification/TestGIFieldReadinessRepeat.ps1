<#
Hard-reset readiness distribution over trace seeds, with one same-seed repeat.

The default hard reset grows real irradiance mass from its entry update. -RapidHardReset
selects the experimental rapid hard reset that reweights every refresh update at 0.5.
-SteadyCalibration after-handoff (the default) measures steady change RMS in a second
calibration run whose refresh ends at its calibrated Ready, as the trace runs' does;
after-timeout measures it after the calibration run's timeout.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$CaseId = @('sphere','plane','sealed','thin','sunroom','parallel','camera'),
    [uint32[]]$TraceSeed = @(61052,71052,81052),
    [uint32]$RepeatSeed = 61052,
    [switch]$RapidHardReset,
    [ValidateSet('after-timeout','after-handoff')][string]$SteadyCalibration = 'after-handoff',
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
Import-Module "$PSScriptRoot/GIFieldReadinessCalibration.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$CaseId = @($CaseId | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$TraceSeed = @($TraceSeed | ForEach-Object { [uint32]$_ })
$cases = @(
    @{id='sphere';fixture=0;view='F1_Sphere';counts=@(13,13,13)},
    @{id='plane';fixture=1;view='F1_Plane';counts=@(13,13,13)},
    @{id='sealed';fixture=2;view='F2_Interior';counts=@(9,9,9)},
    @{id='thin';fixture=3;view='F3_Interior';counts=@(9,9,9)},
    @{id='sunroom';fixture=4;view='F4_Floor';counts=@(18,18,18)},
    @{id='parallel';fixture=20;view='F5_Doorway';counts=@(11,7,7)},
    # F6 reuses the F4 field; its own reference view makes camera independence observable.
    @{id='camera';fixture=4;view='F6_PoseWest';counts=@(18,18,18)}
)
# Each run must follow a different seed, so the seed change itself is the hard reset.
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop','Release') -or
    -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -or
    -not $CaseId.Count -or @($CaseId | Where-Object { $_ -notin $cases.id }).Count -or
    $TraceSeed.Count -lt 2 -or @($TraceSeed | Select-Object -Unique).Count -ne $TraceSeed.Count -or
    @($TraceSeed | Where-Object { $_ -eq 61051 }).Count -or $RepeatSeed -notin $TraceSeed -or
    $RepeatSeed -eq $TraceSeed[-1]) {
    throw 'Invalid readiness repeat configuration, backends, cases or seeds (distinct trace seeds, not calibration seed 61051; the repeat seed must be traced and differ from the last trace seed).'
}
$cases = @($cases | Where-Object { $_.id -in $CaseId })
$runs = @($TraceSeed | ForEach-Object { @{id="seed-$_";seed=$_} }) +
    @(@{id="repeat-$RepeatSeed";seed=$RepeatSeed;repeatOf="seed-$RepeatSeed"})
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldReadinessRepeat" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$validator = "$PSScriptRoot/ValidateGIFieldReadinessRepeat.py"
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
# Status records carry a complete readiness trace; indentation would multiply their size.
function Write-CompactJson([string]$Path, $Value) { Write-GIFieldCalibrationJson $Path $Value }
# Capture metadata and status serialize equal coordinates differently (0 or 0.0).
function Get-CameraKey($Camera) {
    (@($Camera.position) + @($Camera.forward) + @($Camera.verticalFovDegrees) | ForEach-Object {
        ([double]$_).ToString('R', [Globalization.CultureInfo]::InvariantCulture)
    }) -join ','
}
function Set-Inputs($Session, $Run, [hashtable]$Values) {
    $changes = @($Values.Keys | Sort-Object | ForEach-Object {
        $type = if ($_ -in @('fixture','environment','albedo','secondary_albedo','ev100','field_irradiance_width')) { 'enum' }
            elseif ($Values[$_] -is [bool]) { 'bool' }
            elseif (Test-GIFieldSigmaInput $_) { 'float' }
            else { 'uint' }
        @{id="gi_reference.$_";type=$type;value=$Values[$_]}
    })
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Readiness repeat inputs were not consumed.' }
    return $effect
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/cases.json" $cases
    Write-VerificationJson "$output/backends.json" $Rhi
    Write-VerificationJson "$output/runs.json" $runs
    $hardReset = if ($RapidHardReset) { 'rapid' } else { 'progressive' }
    Write-VerificationJson "$output/recipe.json" @{hardResetIntegration=$hardReset;steadyCalibration=$SteadyCalibration}
    foreach ($backend in $Rhi) {
        $session = 'gi-repeat-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        $primaryFailure = $null
        try {
            if ($ShaderCacheDirectory) {
                $null = New-Item -ItemType Directory -Path "$output/$backend/state" -Force
                foreach ($name in @('ShaderCache','ShaderArtifacts')) {
                    Copy-Item -LiteralPath "$ShaderCacheDirectory/$backend/$name" `
                        -Destination "$output/$backend/state/$name" -Recurse
                }
            }
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Readiness repeat requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                $null = New-Item -ItemType Directory -Path $directory -Force
                Write-VerificationJson "$directory/case.json" $case
                # The default single-bounce recipe, or its declared refresh arm, with the
                # observation-only trace; calibration uses the independent seed and the
                # uncalibrated minimum.
                $inputs = Set-Inputs $session $run @{fixture=$case.fixture;environment=0;albedo=0;secondary_albedo=0;
                    ev100=10;field_enabled=$true;field_feedback=$false;field_sampling=$true;field_readiness=$true;
                    field_progressive_sampling=$false;field_progressive_refresh=$false;
                    field_rapid_hard_reset=[bool]$RapidHardReset;receiver_diagnostics=$true;
                    field_irradiance_width=0;field_update_limit=0;field_readiness_trace=$true;probe_enabled=$true;
                    accumulate=$true;depth=1;batch_samples=64;seed=61051;field_refresh_noise=0.0;field_steady_noise=0.0}
                Write-VerificationJson "$directory/calibration-effect.json" $inputs
                $noise = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $r.frame -and $r.frame.giField -and
                        ($r.frame.giField.refreshNoiseSamples -ge 30 -or
                            ($r.frame.giField.activeBlocks -eq 0 -and $r.frame.giField.excludedBlocks -gt 0)) -and
                        $r.frame.giField.steadyNoiseSamples -eq 64 } -TimeoutSeconds $TimeoutSeconds
                Write-CompactJson "$directory/calibration.json" $noise
                $steady = $null
                if ($SteadyCalibration -eq 'after-handoff') {
                    # The sigma change restarts the field at the calibration seed. Its refresh
                    # ends at the calibrated Ready, so steady RMS follows the trace runs' handoff.
                    $steady = Measure-GIFieldSteadyAfterHandoff -SetInputs { param($v) Set-Inputs $session $run $v } `
                        -Status { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                        -Calibration $noise.frame.giField -Directory $directory -TimeoutSeconds $TimeoutSeconds
                }
                $adopted = Get-GIFieldAdoptedInputs hardReset $noise.frame.giField $steady
                foreach ($spec in $runs) {
                    $runDirectory = "$directory/$($spec.id)"
                    $null = New-Item -ItemType Directory -Path $runDirectory -Force
                    $effect = Set-Inputs $session $run (@{seed=$spec.seed} + $adopted)
                    Write-VerificationJson "$runDirectory/effect.json" $effect
                    # The rebuild restores the fixture's first view. Pose the case view on the next
                    # frame, whatever its readiness; its metadata records the field update reached.
                    $pose = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                        Timing='next-frame';View=$case.view;OutputDirectory="$runDirectory/pose";
                        TimeoutSeconds=$TimeoutSeconds;Label="pose-$($case.id)-$($spec.id)"}
                    Write-CompactJson "$runDirectory/pose.json" $pose
                    if ($pose.status -ne 'completed' -or $pose.publication -ne 'Published') {
                        throw 'Readiness repeat pose capture did not publish.'
                    }
                    $camera = Get-CameraKey $pose.products[0].metadata.camera
                    # The trace holds every update since the hard reset; wait for its full window
                    # so that runs with different Ready updates compare over the same updates.
                    $field = @{generation=$null}
                    $outcome = Wait-VerificationCondition -Poll {
                        $status = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                        if ($null -eq $field.generation) { $field.generation = $status.frame.giField.generation }
                        elseif ($status.frame.giField.generation -ne $field.generation) {
                            throw 'Readiness repeat field was replaced before its trace completed.'
                        }
                        $status
                    } -Until { param($r) $r.frame.giField.failed -or ($r.frame.giField.summary.readinessTrace -and
                        @($r.frame.giField.summary.readinessTrace.updates).Count -ge 240) } `
                        -TimeoutSeconds $TimeoutSeconds
                    Write-CompactJson "$runDirectory/outcome.json" $outcome
                    if ($outcome.frame.giField.failed) { throw 'Readiness repeat field failed.' }
                    if ((Get-CameraKey $outcome.frame.camera) -cne $camera) {
                        throw 'Readiness repeat camera left its posed reference view.'
                    }
                    $traced = @($outcome.frame.giField.summary.readinessTrace.updates)
                    $ready = @($traced | Where-Object { $_.state -eq 'Ready' } | Select-Object -First 1)
                    $steady = @($traced | Where-Object { $_.state -eq 'Ready' -and $_.phase -eq 'steady' } |
                        Select-Object -First 1)
                    $verdict = if ($ready.Count) { "Ready at $($ready[0].update)" } else { 'not Ready' }
                    $verdict += if ($steady.Count) { ", steady Ready at $($steady[0].update)" } else { ', no steady Ready' }
                    [Console]::Error.WriteLine("$backend $($case.id) $($spec.id): $verdict")
                }
            }
        } catch {
            $primaryFailure = $_
            throw
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Readiness repeat shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Readiness repeat native validation failed.' }
            }
        }
    }
    # Pose captures are this runner's only bundles; its traces are status evidence.
    Invoke-VerificationPython $Python "$root/Scripts/Verification/ValidateCaptureBundles.py" @($output) `
        "$output/ValidateCaptureBundles.py.result.json" 'Readiness repeat capture verification failed'
    $finalSource = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $source $finalSource 'Readiness repeat sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Readiness repeat binaries changed during verification.' -Depth 2
    Invoke-VerificationPython $Python $validator @('--root',$output) "$output/oracle-result.json" 'Readiness repeat contracts failed'
    $validation = Get-Content -LiteralPath "$output/validation.json" -Raw | ConvertFrom-Json
    $summary = @{ok=$true;directory=$output;hardResetIntegration=$hardReset;
        steadyCalibration=$SteadyCalibration;allSeedsReadyWithin120=$validation.allSeedsReadyWithin120;
        allSeedsSteadyReadyWithin120=$validation.allSeedsSteadyReadyWithin120;
        allHandoffsFollowRefreshEnd=$validation.allHandoffsFollowRefreshEnd}
    Write-VerificationJson "$output/result.json" ($summary + @{readinessRepeatabilityQualified=$false})
    [Console]::Out.WriteLine(($summary | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message;readinessRepeatabilityQualified=$false}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=$_.Exception.Message} | ConvertTo-Json -Compress))
    exit 1
}
