<#
.SYNOPSIS
Collects F6 camera-independence evidence: pose-specific field runs held at one update,
per-view raster lookups, and the frozen world-space receiver list queried from both
poses on one exported field state.

.DESCRIPTION
For each backend, D=1 references are captured at both F6 poses with the field off.
Each readiness arm then runs one field per pose under the default recipe (feedback,
progressive sampling and progressive refresh off), held after -UpdateLimit full
updates. The pose takes effect at a recorded early update; at the held update both
poses' raster lookup groups and receiver-list queries are exported, so a camera
switch is observed on an unchanged field. ValidateGIFieldCameraIndependence.py
checks the lookup contracts and reports world-field differences and static gates.
This is a measurement path, not an F6 or G3 qualification.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$Readiness = @('off','on'),
    [ValidateRange(1, 1000000)][int]$UpdateLimit = 3072,
    [ValidateRange(64, 65536)][int]$ReferenceSamples = 2048,
    [float]$RefreshNoise = 0,
    [float]$SteadyNoise = 0,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
$Readiness = @($Readiness | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'F6 camera independence requires one configuration and explicit valid backends.'
}
if (-not $Readiness.Count -or @($Readiness | Where-Object { $_ -notin @('off','on') }).Count) {
    throw 'Readiness arms must be off and/or on.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldCameraIndependence" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory
$poses = @('PoseWest','PoseEast')
$seed = 61051
$rasterTargets = @('GI.Field.Irradiance','GI.Field.Distance','GI.Field.Classification',
    'GI.Field.ReceiverIrradiance','GI.Field.ReceiverQuery','GI.Receiver.Position',
    'GI.Receiver.VertexNormal','GI.Receiver.ShadingNormal')
$listTargets = @('GI.Field.ReceiverList','GI.Field.Irradiance','GI.Field.Distance','GI.Field.Classification')
$referenceTargets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
    'GI.Reference.Identity','GI.Receiver.Position','GI.Receiver.ShadingNormal',
    'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance')
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
function Set-Inputs([string]$Session, [string]$Run, [hashtable]$Values) {
    $changes = @($Values.Keys | ForEach-Object {
        $value = $Values[$_]
        $type = if ($_ -in @('fixture','ev100','albedo','environment','field_irradiance_width')) { 'enum' }
            elseif ($value -is [bool]) { 'bool' } elseif ($value -is [single] -or $value -is [double]) { 'float' } else { 'uint' }
        @{id="gi_reference.$_";type=$type;value=$value}
    })
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'F6 parameters were not consumed.' }
    return $effect
}
function Export-Group([string]$Session, [string]$Run, [string]$View, [string[]]$Resources, [string]$Directory) {
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $targets = @($Resources | ForEach-Object { @{resource=$_} })
    # The validator binds every group to its field update; readiness is not a gate here.
    $capture = Invoke-VerificationSession export $Session @{ExpectedRunIdentity=$Run;View=$View;Timing='next-frame';
        Targets=(ConvertTo-Json -InputObject $targets -Compress);IncludeGraph=$true;SettleFrames=0;
        OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw "F6 export did not publish: $Directory" }
    Write-VerificationJson "$Directory/receipt.json" $capture
}
function Field-Summary($Status) {
    if (-not $Status.frame -or -not $Status.frame.giField -or -not $Status.frame.giField.PSObject.Properties['summary']) {
        return $null
    }
    return $Status.frame.giField.summary
}
try {
    $sources = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $sources
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/plan.json" @{backends=$Rhi;readiness=$Readiness;poses=$poses;updateLimit=$UpdateLimit;
        referenceSamples=$ReferenceSamples;seed=$seed;refreshNoise=$RefreshNoise;steadyNoise=$SteadyNoise}
    foreach ($backend in $Rhi) {
        $session = 'gi-f6-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        $primaryFailure = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'F6 camera independence requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll {
                Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
            } -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            # One D=1 reference per pose; its primary identities certify visibility.
            $referenceEffect = Set-Inputs $session $run @{fixture=4;ev100=10;albedo=0;field_enabled=$false;
                probe_enabled=$true;samples=$ReferenceSamples;depth=1;batch_samples=64;seed=$seed;accumulate=$true}
            Write-VerificationJson "$output/$backend/reference-effect.json" $referenceEffect
            foreach ($pose in $poses) {
                $directory = "$output/$backend/reference/$pose"
                $products = @(@{kind='resource';targets=@($referenceTargets | ForEach-Object { @{resource=$_} })},@{kind='graph'})
                $reference = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                    Timing='after-ready';SettleFrames=1;View="F6_$pose";RequiredContentId='gglab.lab.gi_reference';
                    Products=($products | ConvertTo-Json -Compress -Depth 8);
                    OutputDirectory=$directory;TimeoutSeconds=$TimeoutSeconds}
                if ($reference.status -ne 'completed' -or $reference.publication -ne 'Published') { throw "F6 reference did not publish: $pose" }
                Write-VerificationJson "$directory/receipt.json" $reference
            }
            foreach ($arm in $Readiness) {
                foreach ($runPose in $poses) {
                    $directory = "$output/$backend/readiness-$arm/$runPose"
                    New-Item -ItemType Directory -Path $directory -Force | Out-Null
                    # Every run restarts a new field from identical inputs; only the pose differs.
                    $effect = Set-Inputs $session $run @{fixture=4;ev100=10;albedo=0;samples=64;seed=$seed;
                        field_enabled=$true;field_readiness=($arm -eq 'on');field_feedback=$false;
                        field_progressive_sampling=$false;field_progressive_refresh=$false;field_sampling=$true;
                        receiver_diagnostics=$true;probe_enabled=$true;field_irradiance_width=0;
                        field_update_limit=$UpdateLimit;field_refresh_noise=[float]$RefreshNoise;
                        field_steady_noise=[float]$SteadyNoise}
                    Write-VerificationJson "$directory/effect.json" $effect
                    Export-Group $session $run "F6_$runPose" @('GI.Field.ReceiverList') "$directory/pose"
                    $held = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                        -Until { param($r) $s = Field-Summary $r; $s -and $s.updateHeld -and $s.submittedUpdates -eq $UpdateLimit } `
                        -TimeoutSeconds $TimeoutSeconds -IntervalMilliseconds 500
                    Write-VerificationJson "$directory/held-status.json" $held
                    # The run's own pose first, then a camera switch on the held field.
                    foreach ($viewPose in @($runPose) + @($poses | Where-Object { $_ -ne $runPose })) {
                        Export-Group $session $run "F6_$viewPose" $rasterTargets "$directory/$viewPose-raster"
                        Export-Group $session $run "F6_$viewPose" $listTargets "$directory/$viewPose-list"
                    }
                    Write-VerificationJson "$directory/final-status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
                    $null = Set-Inputs $session $run @{field_enabled=$false}
                    Write-Verbose "$backend readiness-$arm $runPose held field exported"
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
                if ((-not $stop.ok -or $stop.exitCode -ne 0) -and -not $primaryFailure) { throw 'F6 session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if (-not $primaryFailure -and $log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') {
                    throw 'F6 native/runtime validation failed.'
                }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIFieldCameraIndependence.py" @('--root',$output) "$output/oracle-result.json" 'F6 camera independence failed'
    Assert-VerificationArtifacts $root $output $Python 'F6 camera independence failed'
    $finalSources = Source-Identity
    $finalBinaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity-final.json" $finalSources
    Write-VerificationJson "$output/binaries-final.json" $finalBinaries
    Assert-VerificationIdentity $sources $finalSources 'F6 sources changed during verification.'
    Assert-VerificationIdentity $binaries $finalBinaries 'F6 binaries changed during verification.' -Depth 2
    Write-VerificationJson "$output/result.json" @{status='passed';backends=$Rhi;readiness=$Readiness;
        observations=@(Get-ChildItem -LiteralPath $output -Recurse -Filter receipt.json).Count;lookupContractsPassed=$true;
        cameraIndependenceQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=[string]$_} | ConvertTo-Json -Compress))
    exit 1
}
[Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
