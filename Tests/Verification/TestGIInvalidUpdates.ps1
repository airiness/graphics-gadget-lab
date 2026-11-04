<# Numerical failures must remain observable without entering persistent GI atlases. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [string]$ShaderCacheDirectory,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or $Configuration[0] -notin @('Debug','Develop')) {
    throw 'GI injection requires one Debug or Develop configuration.'
}
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIInvalidUpdates" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Prepare-ShaderState([string]$Backend, [string]$State) {
    if ($ShaderCacheDirectory) {
        $null = New-Item -ItemType Directory -Path $State -Force
        foreach ($name in @('ShaderCache','ShaderArtifacts')) {
            Copy-Item -LiteralPath "$ShaderCacheDirectory/$Backend/$name" -Destination "$State/$name" -Recurse
        }
    }
}
function Set-Inputs([hashtable]$Values) {
    $changes = foreach ($key in $Values.Keys) {
        @{id="gi_reference.$key";type=$(if ($key -in @('fixture','ev100')) {'enum'} elseif ($Values[$key] -is [bool]) {'bool'} else {'uint'});value=$Values[$key]}
    }
    $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($changes) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'GI input batch was not consumed.' }
}
function Capture([string]$Path, [string]$Prefix, [switch]$QueryDiagnostics) {
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
    $targets = @('Rays','Irradiance','Distance','State','UpdateStatus' | ForEach-Object { @{resource="$Prefix.$_"} })
    # Production evidence also reserves readback slots. Keep query diagnostics in
    # a separate four-payload group instead of crowding out the injection evidence.
    if ($Prefix -eq 'GI.Field' -and $QueryDiagnostics) {
        $targets = @('Classification','ReceiverIrradiance','ReceiverQuery','UpdateStatus' | ForEach-Object { @{resource="$Prefix.$_"} })
    }
    $products = @(@{kind='resource';targets=$targets},@{kind='graph'})
    $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=16;
        Products=(ConvertTo-Json -InputObject @($products) -Depth 8 -Compress);OutputDirectory=$Path;TimeoutSeconds=$TimeoutSeconds}
    Write-VerificationJson "$Path/capture.json" $capture
    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'GI evidence did not publish.' }
    Write-VerificationJson "$Path/status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
}
function Source-Identity {
    $names = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification Schemas Projects PropertySheets)
    if ($LASTEXITCODE -ne 0) { throw 'Source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash} })
}
function Binary-Identity {
    return @(@('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') | ForEach-Object {
        @{file=$_;sha256=(Get-FileHash -LiteralPath "$root/Build/Output/x64/$($Configuration[0])/$_" -Algorithm SHA256).Hash}
    })
}
function Test-ProductionRejection([string]$Backend, [string]$Directory, [bool]$Feedback = $false) {
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $session = 'gi-invalid-production-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $start = $null
    $process = $null
    try {
        Prepare-ShaderState $Backend "$Directory/state"
        $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$Backend;
            Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
            WatchNonFinite=$true;StateRoot="$Directory/state";StartTimeoutSeconds=$TimeoutSeconds}
        Write-VerificationJson "$Directory/start.json" $start
        if ($start.ownership -ne 'created') { throw 'Production rejection requires an owned session.' }
        $run = $start.runIdentity
        $process = [Diagnostics.Process]::GetProcessById($start.pid)
        # Retain the native process handle before exit so ExitCode remains available.
        $null = $process.Handle
        $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
            Timing='after-ready';SettleFrames=1;OutputDirectory="$Directory/warmup";TimeoutSeconds=$TimeoutSeconds}
        if ($warmup.publication -ne 'Published') { throw 'Production rejection content warmup failed.' }
        Write-VerificationJson "$Directory/warmup.json" $warmup
        Set-Inputs @{fixture=1;field_enabled=$true;field_feedback=$Feedback;field_readiness=$false;field_sampling=$true;probe_enabled=$false;ev100=10}
        $inject = @{point='pass-gi-probe-update.trace.sample';value='NaN';x=0;y=5;component=1} | ConvertTo-Json -Compress
        $null = Invoke-VerificationSession watch $session @{WatchMode='report';WatchClaims=@('production');Inject=$inject}
        $activationError = $null
        try {
            $activation = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;
                Changes='[{"id":"gi_reference.probe_enabled","type":"bool","value":true}]'}
            Write-VerificationJson "$Directory/activation.json" $activation
        } catch { $activationError = [string]$_ }
        if (-not $process.WaitForExit(30000)) { throw 'A failed production field continued rendering.' }
        $code = $process.ExitCode
        Copy-Item -LiteralPath $start.outputLog -Destination "$Directory/output.log"
        $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
        if ($null -eq $code -or $code -eq 0 -or $log -notmatch 'GI field numerical failure' -or
            $log -match 'Validation Error|VUID-|Assertion failed') {
            throw 'Production rejection did not report the expected numerical contract failure.'
        }
        Write-VerificationJson "$Directory/result.json" @{status='passed';expectedFailure=$true;
            exitCode=$code;activationError=$activationError;reason='GI field numerical failure'}
    } finally {
        if ($process -and -not $process.HasExited) {
            $null = Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$Directory/stop.json" -RequireCleanExit
        }
        if ($process) { $process.Dispose() }
    }
}
$cases = @(
    @{name='ray-nan';point='trace.sample';value='NaN';element=5;component=1;error=1},
    @{name='ray-positive-inf';point='trace.sample';value='PositiveInfinity';element=5;component=0;error=1},
    @{name='distance-negative-inf';point='trace.sample';value='NegativeInfinity';element=5;component=3;error=1},
    @{name='irradiance-history';point='storetexel.history';value='NaN';element=3;component=0;error=2},
    @{name='distance-history';point='storetexel.history';value='PositiveInfinity';element=40;component=1;error=2}
)
try {
    $source = Source-Identity
    $binaries = Binary-Identity
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $directory = "$output/$backend"
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $session = 'gi-invalid-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start = $null
        try {
            Prepare-ShaderState $backend "$directory/state"
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                WatchNonFinite=$true;StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') { throw 'GI injection requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll {
                Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
            } -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            foreach ($mode in @('direct','shared','field','field-feedback')) {
                $isField = $mode.StartsWith('field')
                $prefix = if ($isField) { 'GI.Field' } else { 'GI.ProbeUpdate' }
                foreach ($case in $cases) {
                    $path = "$directory/$mode/$($case.name)"
                    New-Item -ItemType Directory -Path $path -Force | Out-Null
                    Write-VerificationJson "$path/case.json" $case
                    $null = Invoke-VerificationSession watch $session @{WatchOff=$true}
                    $fixture = if ($isField) { 16 } else { 14 }
                    Set-Inputs @{fixture=$fixture;probe_enabled=$false;field_enabled=$isField;field_feedback=($mode -eq 'field-feedback');field_readiness=$false;field_sampling=$false;
                        probe_shared_blend=($mode -ne 'direct');probe_updates=0;probe_rays=256;ev100=15}
                    if ($case.error -eq 2) {
                        if ($isField) {
                            # The tiny cage is inside the cube. Seed finite, nonzero irradiance history
                            # through the existing Debug injection point before rejecting a bad read.
                            $prime = @{point='pass-gi-probe-update.storetexel.history';value='Overflow';
                                x=0;y=3;component=0} | ConvertTo-Json -Compress
                            $null = Invoke-VerificationSession watch $session @{WatchMode='report';WatchClaims=@('production');Inject=$prime}
                        }
                        Set-Inputs @{probe_enabled=$true}
                        Capture "$path/warm" $prefix
                    }
                    $injection = @{point="pass-gi-probe-update.$($case.point)";value=$case.value;x=0;
                        y=$case.element;component=$case.component} | ConvertTo-Json -Compress
                    $watch = Invoke-VerificationSession watch $session @{WatchMode='report';WatchClaims=@('production');Inject=$injection}
                    Write-VerificationJson "$path/watch.json" $watch
                    if ($case.error -eq 1) { Set-Inputs @{probe_enabled=$true} }
                    Capture "$path/bad" $prefix
                    $null = Invoke-VerificationSession watch $session @{WatchOff=$true}
                    Capture "$path/frozen" $prefix
                    if ($isField) { Capture "$path/query-frozen" $prefix -QueryDiagnostics }
                    Set-Inputs @{probe_enabled=$false}
                    Set-Inputs @{probe_enabled=$true}
                    Capture "$path/replaced" $prefix
                    if ($isField) { Capture "$path/query-replaced" $prefix -QueryDiagnostics }
                }
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $null = Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit
                Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                if ((Get-Content -LiteralPath $start.outputLog -Raw) -match '\[error\]|\[critical\]|Validation Error|VUID-') {
                    throw 'GI injection native/runtime validation failed.'
                }
            }
        }
    }
    foreach ($backend in $Rhi) {
        Test-ProductionRejection $backend "$output/$backend/production-rejection"
        Test-ProductionRejection $backend "$output/$backend/production-feedback-rejection" $true
    }
    $scripts = @("$PSScriptRoot/ValidateGIInvalidUpdates.py", "$root/Scripts/Verification/ValidateCaptureBundles.py",
        "$root/Scripts/Verification/ValidateResourceExports.py", "$root/Scripts/Verification/ValidateRenderGraphExports.py")
    foreach ($script in $scripts) {
        $arguments = @($output)
        if ($script -like '*ValidateGIInvalidUpdates.py') { $arguments = @('--root',$output,'--rhi',($Rhi -join ',')) }
        $lines = @(& $Python $script @arguments)
        $code = $LASTEXITCODE
        Write-VerificationJson "$output/$([IO.Path]::GetFileNameWithoutExtension($script)).json" @{exitCode=$code;output=($lines -join "`n")}
        if ($code -ne 0) { throw "GI injection validator failed: $script" }
    }
    $finalSource = Source-Identity
    $finalBinaries = Binary-Identity
    if ((ConvertTo-Json -InputObject @($source) -Depth 4 -Compress) -cne
        (ConvertTo-Json -InputObject @($finalSource) -Depth 4 -Compress) -or
        (ConvertTo-Json -InputObject @($binaries) -Compress) -cne
        (ConvertTo-Json -InputObject @($finalBinaries) -Compress)) { throw 'Sources or binaries changed during GI injection verification.' }
    Write-VerificationJson "$output/result.json" @{status='passed';cases=($Rhi.Count*4*$cases.Count);backends=$Rhi;productionRejections=(2*$Rhi.Count)}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=[string]$_} | ConvertTo-Json -Compress))
    exit 1
}
[Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
