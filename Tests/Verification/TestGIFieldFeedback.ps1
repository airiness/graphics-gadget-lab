<# Previous-update feedback, safe toggle, support counters and bounded static observations. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$CaseId = @('plane','sealed','sunroom','unsupported','coverage','black'),
    [string]$ShaderCacheDirectory,
    [string]$ReferenceDirectory,
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
$CaseId = @($CaseId | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
$cases = @(
    @{id='plane';fixture=1;view='F1_Plane';counts=@(13,13,13)},
    @{id='sealed';fixture=2;view='F2_Interior';counts=@(9,9,9)},
    @{id='sunroom';fixture=4;view='F4_Floor';counts=@(18,18,18)},
    @{id='unsupported';fixture=18;view='Contract_FieldUnsupported';counts=@(5,5,5)},
    @{id='coverage';fixture=17;view='Contract_FieldCoverage';counts=@(5,5,5)},
    @{id='black';fixture=19;view='F2_Interior';counts=@(9,9,9)}
)
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop','Release') -or
    -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -or
    -not $CaseId.Count -or @($CaseId | Where-Object { $_ -notin $cases.id }).Count) { throw 'Invalid feedback configuration.' }
$cases = @($cases | Where-Object { $_.id -in $CaseId })
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldFeedback" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$validator = "$PSScriptRoot/ValidateGIFieldFeedback.py"
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
function Set-Inputs($Session, $Run, [hashtable]$Values) {
    $changes = @($Values.Keys | Sort-Object | ForEach-Object {
        $type = if ($_ -in @('fixture','environment','albedo','secondary_albedo','ev100','field_irradiance_width')) { 'enum' }
            elseif ($Values[$_] -is [bool]) { 'bool' }
            elseif ($_ -in @('field_refresh_noise','field_steady_noise')) { 'float' }
            else { 'uint' }
        @{id="gi_reference.$_";type=$type;value=$Values[$_]}
    })
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Feedback inputs were not consumed.' }
    return $effect
}
function Observe($Session, $Run, $Case, $Path, [bool]$Feedback, [int]$Settle = 0) {
    $null = New-Item -ItemType Directory -Path $Path -Force
    if ($Settle -gt 0) {
        # next-frame exports do not wait for settling. Wait for fence-completed
        # updates explicitly, without borrowing another field generation's age.
        $initial = (Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run}).frame.giField
        $generation = $initial.generation
        $minimum = [long]$initial.completedUpdates + $Settle
        $settled = Wait-VerificationCondition -Poll {
            Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run}
        } -Until {
            param($r)
            $field = $r.frame.giField
            if ($field.generation -ne $generation -or $field.failed) {
                throw 'Field changed or failed during feedback settling.'
            }
            $field.completedUpdates -ge $minimum
        } -TimeoutSeconds $TimeoutSeconds
        Write-VerificationJson "$Path/settle.json" @{generation=$generation;
            startCompletedUpdates=$initial.completedUpdates;settleUpdates=$Settle;
            minimumCompletedUpdates=$minimum;completedUpdates=$settled.frame.giField.completedUpdates}
    }
    $targets = @('GI.Field.State','GI.Field.Readiness','GI.Field.ReceiverIrradiance',
        'GI.Field.ReceiverQuery','GI.Receiver.Position' | ForEach-Object { @{resource=$_} })
    if ($Feedback) {
        $targets += @{resource='GI.Field.Feedback'}
        if ($Case.id -eq 'plane') {
            $targets = @('GI.Field.State','GI.Field.Readiness','GI.Field.Feedback' |
                ForEach-Object { @{resource=$_} })
            $targets += @('GI.Field.Irradiance','GI.Field.Distance','GI.Field.Classification' |
                ForEach-Object { @{resource=$_;at=@{before='GI.Field.TraceShade'}} })
            $targets += @{resource='GI.Field.Rays'}
        }
    }
    $capture = Invoke-VerificationSession export $Session @{ExpectedRunIdentity=$Run;View=$Case.view;
        Timing='next-frame';Targets=(ConvertTo-Json -InputObject $targets -Depth 8 -Compress);
        IncludeGraph=$true;OutputDirectory=$Path;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Path/receipt.json" $capture
    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Feedback observation did not publish.' }
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/cases.json" $cases
    Write-VerificationJson "$output/backends.json" $Rhi
    foreach ($backend in $Rhi) {
        $session = 'gi-feedback-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
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
            if ($start.ownership -ne 'created') { throw 'Feedback verification requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                $null = New-Item -ItemType Directory -Path $directory -Force
                Write-VerificationJson "$directory/case.json" $case
                $off = Set-Inputs $session $run @{fixture=$case.fixture;environment=0;albedo=0;secondary_albedo=0;ev100=10;
                    field_enabled=$true;field_feedback=$false;field_sampling=$true;field_readiness=$true;
                    receiver_diagnostics=$true;field_irradiance_width=0;probe_enabled=$true;accumulate=$true;
                    depth=1;seed=61051;field_refresh_noise=0.0;field_steady_noise=0.0}
                Write-VerificationJson "$directory/off-effect.json" $off
                Observe $session $run $case "$directory/off" $false 512
                $on = Set-Inputs $session $run @{field_feedback=$true}
                Write-VerificationJson "$directory/on-effect.json" $on
                Observe $session $run $case "$directory/warming" $true
                $noise = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $f=$r.frame.giField; $f -and
                        ($f.refreshNoiseSamples -ge 30 -or ($f.activeBlocks -eq 0 -and $f.excludedBlocks -gt 0)) -and
                        $f.steadyNoiseSamples -eq 64 } -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/noise-baseline.json" $noise
                $calibrated = Set-Inputs $session $run @{seed=61052;
                    field_refresh_noise=[float]$noise.frame.giField.refreshNoiseEstimate;
                    field_steady_noise=[float]$noise.frame.giField.steadyNoiseEstimate}
                Write-VerificationJson "$directory/calibrated-effect.json" $calibrated
                $ready = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;View=$case.view;
                    SettleFrames=128;OutputDirectory="$directory/ready";TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                Write-VerificationJson "$directory/ready/receipt.json" $ready
                if ($ready.status -ne 'completed' -or $ready.publication -ne 'Published') { throw 'Feedback did not establish its Ready gate.' }
                Observe $session $run $case "$directory/proof" $true
                $targets = @('GI.Field.ReceiverIrradiance','GI.Field.ReceiverQuery','GI.Receiver.Position',
                    'GI.Field.Readiness' | ForEach-Object { @{resource=$_} })
                $exports = @(0..119 | ForEach-Object { @{frame=$_;targets=$targets;graph=($_ -eq 0 -or $_ -eq 119)} })
                Write-VerificationJson "$directory/exports.json" $exports
                $sequence = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                    CameraPath="GI_Static_$($case.view)";CaptureFrames='0,119';Source='scene';
                    ExportsFile="$directory/exports.json";OutputDirectory="$directory/samples";
                    TimeoutSeconds=$TimeoutSeconds;Label='feedback'}
                Write-VerificationJson "$directory/sequence.json" $sequence
                if ($sequence.sequence.state -ne 'completed' -or
                    $sequence.sequence.parameterPublication.outcome -ne 'Published') { throw 'Feedback static window failed.' }
                $timing = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                    CameraPath="GI_Static_$($case.view)";CaptureFrames='';GpuTiming=$true;
                    OutputDirectory="$directory/timing";TimeoutSeconds=$TimeoutSeconds;Label='feedback-cost'}
                Write-VerificationJson "$directory/timing.json" $timing
                if ($timing.sequence.state -ne 'completed' -or
                    $timing.sequence.timingPublication.outcome -ne 'Published') { throw 'Feedback timing did not publish.' }
                $disabled = Set-Inputs $session $run @{field_feedback=$false}
                Write-VerificationJson "$directory/disabled-effect.json" $disabled
                Observe $session $run $case "$directory/disabled" $false 512
                $offTiming = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;
                    CameraPath="GI_Static_$($case.view)";CaptureFrames='';GpuTiming=$true;
                    OutputDirectory="$directory/off-timing";TimeoutSeconds=$TimeoutSeconds;Label='feedback-off-cost'}
                Write-VerificationJson "$directory/off-timing.json" $offTiming
                if ($offTiming.sequence.state -ne 'completed' -or
                    $offTiming.sequence.timingPublication.outcome -ne 'Published') { throw 'Feedback-off timing did not publish.' }
                [Console]::Error.WriteLine("$backend $($case.id) feedback collected")
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Feedback shutdown failed.' }
                if ([IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'') -match
                    '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Feedback native validation failed.' }
            }
        }
    }
    $finalSource = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $source $finalSource 'Feedback sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'Feedback binaries changed during verification.' -Depth 2
    Assert-VerificationArtifacts $root $output $Python 'Feedback artifact audit failed'
    $arguments = @('--root',$output)
    if ($ReferenceDirectory) { $arguments += @('--reference-root',[IO.Path]::GetFullPath($ReferenceDirectory)) }
    Invoke-VerificationPython $Python $validator $arguments "$output/oracle-result.json" 'Feedback gates failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Feedback sources changed during artifact/oracle validation.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) `
        'Feedback binaries changed during artifact/oracle validation.' -Depth 2
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;wholeG3Qualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message;wholeG3Qualified=$false}
    throw
}
