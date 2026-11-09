<# Paired D=1 reference, completed readiness and 120 consecutive fixed-receiver samples. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$CaseId = @('plane','sealed','sunroom','sunroom-back','black',
        'thin','thin-floor','thin-ceiling','thin-roof','doorway','parallel-floor'),
    # Initial reference samples for the sunlit F4 and F5 fixtures.
    [ValidateRange(64,524288)][int]$InitialF4Samples = 4096,
    # Optional predeclared dark-reference floors for F3/F5 stability and leakage.
    [string]$LimitPolicy,
    [ValidateRange(128,1048576)][int]$MaximumSamples = 32768,
    [string[]]$DiagnosticReadyOffsets = @(),
    # Where the calibration measures steady change RMS; see GIFieldReadinessCalibration.psm1.
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
$readyOffsets = @($DiagnosticReadyOffsets | ForEach-Object { $_ -split ',' } | ForEach-Object { [int]$_ })
if ($readyOffsets.Count -gt 4 -or @($readyOffsets | Where-Object { $_ -lt 512 -or $_ -gt 8192 }).Count -or
    (($readyOffsets | Sort-Object -Unique) -join ',') -ne ($readyOffsets -join ',')) {
    throw 'Diagnostic offsets must be at most four strictly increasing completed-update offsets in [512,8192].'
}
$cases = @(
    @{id='plane';fixture=1;view='F1_Plane';counts=@(13,13,13)},
    @{id='sealed';fixture=2;view='F2_Interior';counts=@(9,9,9)},
    @{id='sunroom';fixture=4;view='F4_Floor';counts=@(18,18,18)},
    @{id='sunroom-back';fixture=4;view='F4_Back';counts=@(18,18,18)},
    @{id='black';fixture=19;view='F2_Interior';counts=@(9,9,9)},
    # F3 interior patches plus the exterior roof that supplies the lit reference.
    @{id='thin';fixture=3;view='F3_Interior';counts=@(9,9,9)},
    @{id='thin-floor';fixture=3;view='F3_Floor';counts=@(9,9,9)},
    @{id='thin-ceiling';fixture=3;view='F3_Ceiling';counts=@(9,9,9)},
    @{id='thin-roof';fixture=3;view='F3_Roof';counts=@(9,9,9)},
    @{id='doorway';fixture=20;view='F5_Doorway';counts=@(11,7,7)},
    @{id='parallel-floor';fixture=20;view='F5_Floor';counts=@(11,7,7)}
)
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop','Release') -or
    -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -or
    -not $CaseId.Count -or @($CaseId | Where-Object { $_ -notin $cases.id }).Count -or
    2*$InitialF4Samples -gt $MaximumSamples) { throw 'Invalid field baseline configuration, backends, cases or sample bounds.' }
$cases = @($cases | Where-Object { $_.id -in $CaseId })
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldBaseline" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$validator = "$PSScriptRoot/ValidateGIFieldBaseline.py"
$limitsValidator = "$PSScriptRoot/ValidateGIFieldLimits.py"
if ($LimitPolicy -and -not (Test-Path -LiteralPath $LimitPolicy -PathType Leaf)) { throw 'Limit policy file does not exist.' }
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
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
    if (-not $effect.batch.succeeded) { throw 'Field baseline inputs were not consumed.' }
    return $effect
}
function Require-Publication($Capture) {
    if ($Capture.status -ne 'completed' -or $Capture.publication -ne 'Published') {
        throw 'Required baseline observation did not publish.'
    }
}
function Capture-Window($Session, $Run, $Case, $Directory, $Label) {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $targets = @('GI.Field.ReceiverIrradiance','GI.Field.ReceiverQuery','GI.Receiver.Position',
        'GI.Field.Readiness' | ForEach-Object { @{resource=$_} })
    $exports = @(0..119 | ForEach-Object {
        $frameTargets = @($targets)
        if ($_ -eq 0) { $frameTargets += @{resource='GI.Field.Classification'} }
        @{frame=$_;targets=$frameTargets;graph=($_ -eq 0 -or $_ -eq 119)}
    })
    Write-VerificationJson "$Directory/exports.json" $exports
    $sequence = Invoke-VerificationSession sequence $Session @{ExpectedRunIdentity=$Run;
        CameraPath="GI_Static_$($Case.view)";CaptureFrames='0,119';Source='scene';
        ExportsFile="$Directory/exports.json";
        OutputDirectory="$Directory/samples";TimeoutSeconds=$TimeoutSeconds;Label=$Label}
    Write-VerificationJson "$Directory/sequence.json" $sequence
    if ($sequence.sequence.state -ne 'completed' -or
        $sequence.sequence.parameterPublication.outcome -ne 'Published') { throw 'Static baseline range did not complete.' }
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/cases.json" @($cases.id)
    Write-VerificationJson "$output/backends.json" $Rhi
    Write-VerificationJson "$output/diagnostic-ready-offsets.json" $readyOffsets
    Write-GIFieldCalibrationRecipe $output $SteadyCalibration
    if ($LimitPolicy) { Copy-Item -LiteralPath $LimitPolicy -Destination "$output/limit-policy.json" }
    foreach ($backend in $Rhi) {
        $session = 'gi-baseline-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
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
            if ($start.ownership -ne 'created') { throw 'Field baseline requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                $null = New-Item -ItemType Directory -Path "$directory/baseline-gated","$directory/settled" -Force
                Write-VerificationJson "$directory/case.json" $case
                $referenceInputs = @{fixture=$case.fixture;environment=0;albedo=0;secondary_albedo=0;ev100=10;
                    field_enabled=$false;field_feedback=$false;field_sampling=$true;field_readiness=$true;receiver_diagnostics=$true;
                    field_irradiance_width=0;probe_enabled=$true;accumulate=$true;depth=1;batch_samples=64;
                    field_refresh_noise=0.0;field_steady_noise=0.0}
                $null = Set-Inputs $session $run $referenceInputs
                $referenceTargets = @('GI.Reference.Irradiance','GI.Reference.SunIrradiance',
                    'GI.Reference.EnvironmentIrradiance','GI.Reference.Position','GI.Reference.Identity',
                    'GI.Reference.Diagnostics','GI.Reference.PathDiagnostics','GI.Receiver.Position' | ForEach-Object { @{resource=$_} })
                $observations = @()
                $samples = if ($case.fixture -in @(4,20)) { $InitialF4Samples } else { 64 }
                do {
                    foreach ($count in @($samples, (2*$samples))) {
                        foreach ($seed in @(61051,61052)) {
                            if (@($observations | Where-Object { $_.samples -eq $count -and $_.seed -eq $seed }).Count) { continue }
                            $relative = "reference/n$count-s$seed"
                            $effect = Set-Inputs $session $run @{samples=$count;seed=$seed}
                            $capture = Invoke-VerificationSession export $session @{ExpectedRunIdentity=$run;View=$case.view;
                                Targets=(ConvertTo-Json -InputObject $referenceTargets -Compress);IncludeGraph=$true;SettleFrames=0;
                                OutputDirectory="$directory/$relative";TimeoutSeconds=$TimeoutSeconds}
                            Require-Publication $capture
                            Write-VerificationJson "$directory/$relative/receipt.json" $capture
                            Write-VerificationJson "$directory/$relative/effect.json" $effect
                            $observations += @{samples=$count;seed=$seed;directory=$relative}
                        }
                    }
                    Write-VerificationJson "$directory/references.json" @($observations)
                    Invoke-VerificationPython $Python $validator @('--reference',$directory) `
                        "$directory/reference-n$samples.result.json" 'D1 reference validation failed'
                    $qualification = Get-Content -LiteralPath "$directory/reference-validation.json" -Raw | ConvertFrom-Json
                    Copy-Item -LiteralPath "$directory/reference-validation.json" -Destination "$directory/reference-n$samples.json"
                    $samples *= 2
                } while (-not $qualification.referenceSamplingQualified -and 2*$samples -le $MaximumSamples)
                # Keep all failed sampling attempts. Unqualified reference data may be
                # measured diagnostically, but can never grant a quality pass below.
                $fieldEffect = Set-Inputs $session $run @{field_enabled=$true;seed=61051;
                    field_refresh_noise=0.0;field_steady_noise=0.0}
                Write-VerificationJson "$directory/field-effect.json" $fieldEffect
                $job = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                    SettleFrames=0;NoWait=$true;OutputDirectory="$directory/baseline-gated";TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/baseline-admission.json" $job
                $noise = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $r.frame -and $r.frame.giField -and
                        $r.frame.giField.refreshNoiseSamples -ge 30 -and $r.frame.giField.steadyNoiseSamples -eq 64 } `
                    -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/noise-baseline.json" $noise
                $baseline = Invoke-VerificationSession result $session @{ExpectedRunIdentity=$run;JobId=$job.jobId;Consume=$true} -AllowFailure
                if ($baseline.status -notin @('completed','failed')) { throw 'Uncalibrated readiness gate did not terminate.' }
                Write-VerificationJson "$directory/baseline-gated/receipt.json" $baseline
                $steady = $null
                if ($SteadyCalibration -eq 'after-handoff') {
                    $steady = Measure-GIFieldSteadyAfterHandoff -SetInputs { param($v) Set-Inputs $session $run $v } `
                        -Status { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                        -Calibration $noise.frame.giField -Directory $directory -TimeoutSeconds $TimeoutSeconds
                }
                $effect = Set-Inputs $session $run (@{seed=61052} + (Get-GIFieldAdoptedInputs hardReset $noise.frame.giField $steady))
                Write-VerificationJson "$directory/calibrated-effect.json" $effect
                $settled = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                    SettleFrames=128;OutputDirectory="$directory/settled";TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                Write-VerificationJson "$directory/settled/receipt.json" $settled
                if ($settled.status -eq 'completed') {
                    Require-Publication $settled
                    Capture-Window $session $run $case $directory 'baseline'
                    $anchor = $settled.products[0].metadata.readiness.giField
                    foreach ($offset in $readyOffsets) {
                        $observed = Wait-VerificationCondition -Poll {
                            $current = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                            $field = $current.frame.giField
                            if ($field.generation -ne $anchor.generation -or $field.state -ne 'Ready' -or
                                $field.readyUpdate -ne $anchor.readyUpdate -or
                                $field.summary.field.refreshEpoch -ne $anchor.summary.field.refreshEpoch) {
                                throw 'Diagnostic window lost its original field epoch or Ready stamp.'
                            }
                            $current
                        } -Until { param($r) $r.frame.giField.completedUpdates -ge $anchor.readyUpdate+$offset } `
                            -TimeoutSeconds $TimeoutSeconds
                        $diagnostic = "$directory/diagnostics/ready-$offset"
                        $null = New-Item -ItemType Directory -Path $diagnostic -Force
                        Write-VerificationJson "$diagnostic/admission.json" @{readyOffset=$offset;
                            readyUpdate=$anchor.readyUpdate;field=$anchor.summary.field;status=$observed}
                        Capture-Window $session $run $case $diagnostic "diagnostic-$offset"
                    }
                } elseif ($settled.status -ne 'failed' -or ($settled | ConvertTo-Json -Depth 24) -notmatch 'Unconverged') {
                    throw 'Settled baseline failed for a reason other than Unconverged.'
                }
                [Console]::Error.WriteLine("$backend $($case.id) baseline collected")
            }
        } catch {
            $primaryFailure = $_
            throw
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'Field baseline shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Field baseline native validation failed.' }
            }
        }
    }
    Assert-VerificationArtifacts $root $output $Python 'Field baseline artifact verification failed'
    $finalSource = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $source $finalSource 'Field baseline sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Field baseline binaries changed during verification.' -Depth 2
    # Limits are measurements gated at G4; record them even when a G3 baseline gate fails.
    if (@($cases | Where-Object { $_.fixture -in @(3,20) }).Count) {
        Invoke-VerificationPython $Python $limitsValidator @('--root',$output) "$output/limits-result.json" 'F3/F5 limit measurement contracts failed'
    }
    Invoke-VerificationPython $Python $validator @('--root',$output) "$output/oracle-result.json" 'Single-bounce baseline gates failed'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;wholeG3Qualified=$false}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message;wholeG3Qualified=$false}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=$_.Exception.Message} | ConvertTo-Json -Compress))
    exit 1
}
