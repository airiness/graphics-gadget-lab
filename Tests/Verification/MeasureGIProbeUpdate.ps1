<# Quiet Release cost samples. Numerical and activation evidence belongs to TestGIProbeUpdate.ps1. #>
[CmdletBinding()]
param(
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [switch]$Smoke,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3.0
$Rhi=@($Rhi | ForEach-Object {$_ -split ','})
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) {$OutputDirectory="$root/Build/Validation/GIProbeCost"}
$output=New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Identity {
    # A typed array unwraps native-command PSObjects before appending managed strings.
    [string[]]$names=@(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification Schemas Projects PropertySheets)
    if ($LASTEXITCODE -ne 0) {throw 'Source enumeration failed'}
    $names+=@('Build/Output/x64/Release/GraphicsGadgetLab.exe','Build/Output/x64/Release/gglab-shaderc.exe','Build/Output/x64/Release/dxcompiler.dll','Build/Output/x64/Release/dxil.dll')
    [Array]::Sort($names,[StringComparer]::Ordinal)
    return @($names | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash} })
}
function Inputs([string]$Session,[string]$Run,[hashtable]$Values) {
    $changes=@(foreach ($key in $Values.Keys) {
        @{id="gi_reference.$key";type=$(if ($key -eq 'fixture' -or $key -eq 'ev100') {'enum'} elseif ($Values[$key] -is [bool]) {'bool'} else {'uint'});value=$Values[$key]}
    })
    $effect=Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) {throw 'Timing inputs were not consumed'}
    return $effect
}
$cases=@()
try {
    $identity=Identity
    Write-VerificationJson "$output/identity.json" $identity
    foreach ($backend in $Rhi) {
        $directory="$output/$backend"
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $session='probe-cost-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start=$null
        try {
            $start=Invoke-VerificationSession start $session @{Configuration='Release';Rhi=$backend;Lab='gglab.lab.gi_reference';WindowSize='1920x1080';Validation='off';NoDevTools=$true;StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') {throw 'Timing requires an owned quiet process'}
            $run=$start.runIdentity
            $ready=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=1;OutputDirectory="$directory/initial-ready";TimeoutSeconds=$TimeoutSeconds}
            if ($ready.publication -ne 'Published') {throw 'Initial content was not ready'}
            $matrix=@(@{kind='off';rays=256;divisor=1;shared=$true})
            foreach ($rays in $(if ($Smoke) {@(256)} else {@(128,192,256)})) {
                foreach ($divisor in @(1,4)) {
                    foreach ($shared in @($false,$true)) {$matrix+=@{kind='probe';rays=$rays;divisor=$divisor;shared=$shared}}
                }
            }
            foreach ($divisor in @(1,4)) {$matrix+=@{kind='traversal';rays=256;divisor=$divisor;shared=$true}}
            $matrix+=@{kind='reference';rays=256;divisor=1;shared=$true}
            if (-not $Smoke) {
                foreach ($divisor in @(1,4)) {
                    foreach ($shared in @($true,$false)) {$matrix+=@{kind='probe';rays=256;divisor=$divisor;shared=$shared;repeat=1}}
                }
            }
            $matrix+=@{kind='off';rays=256;divisor=1;shared=$true;repeat=1}
            $index=0
            foreach ($entry in $matrix) {
                $path="$directory/c$index"
                New-Item -ItemType Directory -Path $path -Force | Out-Null
                $values=@{fixture=$(if ($entry.kind -eq 'reference') {12} else {15});probe_enabled=($entry.kind -ne 'off');
                    probe_allocation_diagnostics=$false;probe_rays=$entry.rays;probe_divisor=$entry.divisor;probe_updates=0;
                    probe_refresh_period=0;probe_shared_blend=$entry.shared;probe_rotate=$false;
                    probe_traversal_only=($entry.kind -eq 'traversal');ev100=15;samples=1;batch_samples=1;depth=1;tile_pixels=8388608;accumulate=$false}
                $effect=Inputs $session $run $values
                Write-VerificationJson "$path/effect.json" $effect
                $warmup=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=120;OutputDirectory="$path/warmup";TimeoutSeconds=$TimeoutSeconds}
                if ($warmup.publication -ne 'Published') {throw 'Timing warmup did not publish'}
                # No images, graph exports, resource exports or watches in the timed range.
                $response=Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;CameraPath='GI_CostStatic';GpuTiming=$true;OutputDirectory="$path/samples";TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$path/sequence.json" $response
                if ($response.sequence.state -ne 'completed' -or $response.sequence.timingPublication.outcome -ne 'Published' -or
                    $response.sequence.parameterPublication.outcome -ne 'Published') {throw 'Incomplete timing range'}
                $status=Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                Write-VerificationJson "$path/status.json" $status
                $case=@{directory="$backend/c$index";rhi=$backend;kind=$entry.kind;rays=$entry.rays;divisor=$entry.divisor;shared=$entry.shared;repeat=$(if ($entry.ContainsKey('repeat')) {$entry.repeat} else {0})}
                $cases+=$case
                Write-VerificationJson "$output/cases.json" @($cases)
                Write-Output "$backend timing $index ($($entry.kind), $($entry.rays) rays, /$($entry.divisor), shared=$($entry.shared)) published"
                $index++
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit
                Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                if ((Get-Content -LiteralPath $start.outputLog -Raw) -match '\[error\]|\[critical\]|Validation Error|VUID-') {throw 'Native timing run failed'}
            }
        }
    }
    & $Python "$PSScriptRoot/ValidateGIProbeCost.py" $output
    if ($LASTEXITCODE -ne 0) {throw 'Timing qualification failed'}
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateParameterFrames.py','ValidateGpuTiming.py')) {
        $arguments=@($output)
        if ($validator -eq 'ValidateParameterFrames.py') {
            # This validator accepts files, unlike the directory-aware bundle/timing auditors.
            $arguments=@(Get-ChildItem -LiteralPath $output -Recurse -File -Filter '*.parameter-frames.json' |
                ForEach-Object {$_.FullName})
            if ($arguments.Count -ne $cases.Count) {throw 'Missing parameter-frame audit inputs'}
        }
        $previous=$ErrorActionPreference
        try {
            $ErrorActionPreference='Continue'
            $lines=@(& $Python "$root/Scripts/Verification/$validator" @arguments 2>&1)
            $auditExit=$LASTEXITCODE
        } finally {
            $ErrorActionPreference=$previous
        }
        [IO.File]::WriteAllLines("$output/$validator.log",[string[]]$lines,[Text.UTF8Encoding]::new($false))
        if ($auditExit -ne 0) {throw "Timing artifact audit failed: $validator (see $output/$validator.log)"}
    }
    $final=Identity
    Write-VerificationJson "$output/identity-final.json" $final
    if ((ConvertTo-Json -InputObject @($identity) -Depth 4 -Compress) -cne (ConvertTo-Json -InputObject @($final) -Depth 4 -Compress)) {throw 'Source/product/compiler changed during timing'}
    Write-VerificationJson "$output/result.json" @{status='passed';observations=$cases.Count;productionFieldQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    throw
}
