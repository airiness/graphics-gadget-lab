<#
.SYNOPSIS
Qualifies fixed-camera reference ROIs with independent seeds, sample doubling and fixed-depth doubling.
Completed Runtime observations remain unqualified until both numerical gates pass.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string[]]$Views = @('F1_Plane','F2_Floor','F3_Floor','F4_Floor','F4_Back'),
    [switch]$Coastal,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [switch]$Resume,
    [string]$CancellationFile,
    [string]$Python = 'python',
    [ValidateRange(1,524288)][int]$InitialSamples = 64,
    [ValidateRange(1,524288)][int]$InitialF4Samples = 4096,
    [ValidateRange(1,524288)][int]$InitialCoastalSamples = 4096,
    [string]$ShaderCacheDirectory,
    [ValidateRange(2,1048576)][int]$MaximumSamples = 1048576,
    [ValidateSet(16,32,64,128,256)][int]$MaximumDepth = 256,
    [ValidateRange(1,4096)][int]$BatchSamples = 64,
    [ValidateRange(1,10000)][int]$MaximumObservations = 512,
    [ValidateRange(1,86400)][int]$MaximumSeconds = 14400,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
$Views = @($Views | ForEach-Object { $_ -split ',' })
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($Coastal -and -not $PSBoundParameters.ContainsKey('Views')) {
    $Views = @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge')
}
$coastalViews = @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge')
if (@($Views | Where-Object { $_ -in $coastalViews }).Count -gt 0 -and -not $Coastal) {
    throw 'Coastal views require -Coastal and the frozen 1280x800 extent.'
}
if ($Coastal -and @($Views | Where-Object { $_ -notin $coastalViews }).Count -gt 0) {
    throw 'Coastal and fixture views cannot share an extent.'
}
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIReferenceConvergence" }
if ($Resume) {
    if (-not $RunDirectory -or $OutputDirectory) { throw 'Resume requires an explicit existing RunDirectory.' }
    $output = (Resolve-Path -LiteralPath $RunDirectory).Path
} else {
    $output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N'))
}
$validator = "$PSScriptRoot/ValidateGIReferenceConvergence.py"
$summaries = @()
$failed = $false
$directory = $null
function Invoke-Planner([string]$Directory, [string[]]$Options = @()) {
    $lines = @(& $Python $validator $Directory @Options)
    if ($LASTEXITCODE -ne 0) { throw "Convergence planner failed: $Directory" }
    return ($lines -join "`n" | ConvertFrom-Json)
}
function Get-ReferenceFileHash([string]$Path) {
    # Avoid shell-module auto-loading differences between Windows PowerShell and pwsh.
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = $null
    try {
        $stream = [IO.File]::OpenRead($Path)
        return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','')
    }
    finally {
        if ($stream) { $stream.Dispose() }
        $hash.Dispose()
    }
}
function Get-SourceIdentity {
    $files = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard Sources Shaders Tests/Verification Scripts/Verification)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate source provenance.' }
    $files += @('Tests/Verification/TestGIReferenceConvergence.ps1','Tests/Verification/ValidateGIReferenceConvergence.py')
    [string[]]$orderedFiles = $files
    [Array]::Sort($orderedFiles, [StringComparer]::Ordinal)
    $lastFile = $null
    $rows = foreach ($file in $orderedFiles) {
        if ($file -ceq $lastFile) { continue }
        $lastFile = $file
        if (Test-Path -LiteralPath "$root/$file" -PathType Leaf) { "$file $(Get-ReferenceFileHash "$root/$file")" }
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
try {
    $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $sourceIdentity = Get-SourceIdentity
    foreach ($config in $Configuration) {
        $binaries = @{}
        foreach ($file in @('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll')) {
            $binaries[$file] = Get-ReferenceFileHash "$root/Build/Output/x64/$config/$file"
        }
        foreach ($backend in $Rhi) {
            $directory = "$output/$config/$backend"
            $null = New-Item -ItemType Directory -Path $directory -Force
            $session = 'gi-convergence-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
            $start = $null
            $attempt = [Guid]::NewGuid().ToString('N').Substring(0,10)
            $clock = [Diagnostics.Stopwatch]::StartNew()
            Write-VerificationJson "$directory/attempt-$attempt.json" @{
                maximumObservations=$MaximumObservations;maximumSeconds=$MaximumSeconds;
                timeoutSeconds=$TimeoutSeconds;resume=[bool]$Resume;cancellationFile=$CancellationFile}
            try {
                $stateRoot = "$directory/state-$attempt"
                if ($ShaderCacheDirectory) {
                    $null = New-Item -ItemType Directory -Path $stateRoot -Force
                    foreach ($name in @('ShaderCache','ShaderArtifacts')) {
                        $seed = "$ShaderCacheDirectory/$backend/$name"
                        if (Test-Path -LiteralPath $seed) { Copy-Item -LiteralPath $seed -Destination "$stateRoot/$name" -Recurse }
                    }
                }
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize=$(if ($Coastal) {'1280x800'} else {'640x400'});
                    NoDevTools=$true;Validation='standard';StateRoot=$stateRoot;StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start-$attempt.json" $start
                if ($start.ownership -ne 'created') { throw 'Convergence runner must own its session.' }
                $run = $start.runIdentity
                $warmup = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';
                    SettleFrames=1;OutputDirectory="$directory/warmup-$attempt";TimeoutSeconds=$TimeoutSeconds}
                $bundle = Get-Content -LiteralPath $warmup.manifest -Raw | ConvertFrom-Json
                $depths = @(8,16,32,64,128,256 | Where-Object { $_ -le $MaximumDepth })
                $recipe = @{schemaVersion=2;views=@($Views);depths=$depths;initialSamples=$InitialSamples;
                    initialF4Samples=$InitialF4Samples;maximumSamples=$MaximumSamples;batchSamples=$BatchSamples;
                    initialCoastalSamples=$InitialCoastalSamples;
                    inputsSha256=(Get-ReferenceFileHash "$PSScriptRoot/GlobalIllumination/Inputs.json").ToLowerInvariant();
                    relativeLimit=0.01;absoluteFloor=0.000001;configuration=$config;rhi=$backend;
                    extent=$(if ($Coastal) {@(1280,800)} else {@(640,400)});
                    sourceIdentity=$sourceIdentity;binaries=$binaries;
                    adapterDriverIdentity=$bundle.environment.device.compatibilityIdentity;
                    build=$bundle.environment.build;shaderArtifactIdentity='unavailable; source and compiler binaries hashed'}
                $candidate = "$directory/recipe-$attempt.json"
                Write-VerificationJson $candidate $recipe
                if ($Resume) {
                    $null = Invoke-Planner $directory @('--check-recipe',$candidate)
                } else { Copy-Item -LiteralPath $candidate -Destination "$directory/recipe.json" }
                $plan = Invoke-Planner $directory @('--initialize','--audit')
                $targets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                    'GI.Reference.Identity','GI.Receiver.HitAlbedo','GI.Reference.PathDiagnostics',
                    'GI.Reference.SunIrradiance','GI.Reference.EnvironmentIrradiance') | ForEach-Object {@{resource=$_}}
                $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'}) | ConvertTo-Json -Compress -Depth 8
                while ($plan.status -eq 'Running') {
                    if ($CancellationFile -and (Test-Path -LiteralPath $CancellationFile)) {
                        $plan.status = 'Cancelled'
                        Write-VerificationJson "$directory/report.json" $plan
                        break
                    }
                    if ($plan.observations -ge $MaximumObservations -or $clock.Elapsed.TotalSeconds -ge $MaximumSeconds) {
                        $plan.status = 'ReferenceUnconverged'
                        $plan | Add-Member -NotePropertyName budgetReason -NotePropertyValue 'ObservationOrTimeLimit'
                        foreach ($view in $Views) {
                            if (-not $plan.results.PSObject.Properties[$view]) {
                                $plan.results | Add-Member -NotePropertyName $view -NotePropertyValue @{
                                    status='ReferenceUnconverged';reason='ExecutionBudget';roiStatus='NotQualified'}
                            }
                        }
                        Write-VerificationJson "$directory/report.json" $plan
                        break
                    }
                    $case = $plan.request
                    $changes = @()
                    foreach ($name in @('fixture','depth','samples','batch_samples','tile_pixels','seed','albedo','secondary_albedo','environment','ev100')) {
                        $type = if ($name -in @('depth','samples','batch_samples','tile_pixels','seed')) {'uint'} else {'enum'}
                        $changes += @{id="gi_reference.$name";type=$type;value=$case.$name}
                    }
                    $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                        Changes=($changes | ConvertTo-Json -Compress);TimeoutSeconds=$TimeoutSeconds}
                    if (-not $effect.batch.succeeded) { throw 'Reference parameters were not consumed.' }
                    $captureDirectory = "$directory/$($case.directory)"
                    # An interrupted unpublished attempt may be left on disk. Never
                    # overwrite it; a new attempt directory keeps completed cache immutable.
                    if (Test-Path -LiteralPath $captureDirectory) {
                        $case.directory += '-retry-'+$attempt
                        $captureDirectory = "$directory/$($case.directory)"
                    }
                    $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                        Timing='after-ready';SettleFrames=1;View=$case.view;RequiredContentId='gglab.lab.gi_reference';
                        Products=$products;OutputDirectory=$captureDirectory;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$captureDirectory/capture.json" $capture
                    Write-VerificationJson "$captureDirectory/parameter-effect.json" $effect
                    Write-VerificationJson "$captureDirectory/case.json" $case
                    if ($capture.status -eq 'cancelled') { throw 'Reference capture cancelled.' }
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Incomplete reference capture.' }
                    if ($case.fixture -in @(12,13)) {
                        $receiverTargets = @('GI.Receiver.FaceNormal','GI.Receiver.VertexNormal','GI.Receiver.ShadingNormal',
                            'GI.Receiver.DiffuseWeight','GI.Receiver.Position','DisplayView.DepthBuffer') | ForEach-Object {@{resource=$_}}
                        $receiverProducts = @(@{kind='resource';targets=@($receiverTargets)},@{kind='graph'}) | ConvertTo-Json -Depth 8 -Compress
                        $receiverCapture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                            Timing='after-ready';SettleFrames=1;View=$case.view;RequiredContentId='gglab.lab.gi_reference';
                            Products=$receiverProducts;OutputDirectory="$captureDirectory/receivers";TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$captureDirectory/receivers/capture.json" $receiverCapture
                        if ($receiverCapture.status -ne 'completed' -or $receiverCapture.publication -ne 'Published') {
                            throw 'Incomplete paired receiver capture.'
                        }
                    }
                    $plan = Invoke-Planner $directory @('--ingest',"$captureDirectory/case.json")
                }
                if ((Get-SourceIdentity) -ne $sourceIdentity) { throw 'Source inputs changed during convergence attempt.' }
                foreach ($file in $binaries.Keys) {
                    if ((Get-ReferenceFileHash "$root/Build/Output/x64/$config/$file") -ne $binaries[$file]) {
                        throw 'Compiler/product binary changed during convergence attempt.'
                    }
                }
                $summaries += @{configuration=$config;rhi=$backend;status=$plan.status;observations=$plan.observations;
                    elapsedSeconds=$clock.Elapsed.TotalSeconds;report="$directory/report.json"}
                $null = Invoke-Planner $directory @('--audit')
                foreach ($name in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
                    if ($plan.observations -eq 0 -and $name -ne 'ValidateCaptureBundles.py') { continue }
                    $lines = @(& $Python "$root/Scripts/Verification/$name" $directory)
                    $validatorExit = $LASTEXITCODE
                    Write-VerificationJson "$directory/$name.result.json" @{exitCode=$validatorExit;output=($lines -join "`n")}
                    if ($validatorExit -ne 0) { throw "Invalid convergence artifacts: $name" }
                }
            }
            catch {
                $terminal = if ($_.Exception.Message -eq 'Reference capture cancelled.') {'Cancelled'} else {'Failed'}
                $failureRecord = @{status=$terminal;error=$_.Exception.Message;elapsedSeconds=$clock.Elapsed.TotalSeconds}
                Write-VerificationJson "$directory/failure-$attempt.json" $failureRecord
                if (Test-Path -LiteralPath "$directory/report.json") {
                    $failedReport = Get-Content -LiteralPath "$directory/report.json" -Raw | ConvertFrom-Json
                    $failedReport.status = $terminal
                    Write-VerificationJson "$directory/report.json" $failedReport
                }
                throw
            }
            finally {
                if ($start -and $start.ownership -eq 'created') {
                    $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                    Write-VerificationJson "$directory/stop-$attempt.json" $stop
                    if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Convergence session failed during teardown.' }
                    $log = Get-Content -LiteralPath $start.outputLog -Raw
                    if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Native/runtime validation failed.' }
                }
            }
        }
        if ('dx12' -in $Rhi -and 'vulkan' -in $Rhi) {
            $comparison = @(& $Python $validator "$output/$config" --compare-rhi)
            $comparisonExit = $LASTEXITCODE
            Write-VerificationJson "$output/$config/cross-rhi-validation.json" @{
                exitCode=$comparisonExit;output=($comparison -join "`n")}
            if ($comparisonExit -ne 0) { throw 'Cross-RHI convergence comparison failed.' }
        }
    }
}
catch {
    $failed = $true
    $failure = $_.Exception.Message
    if ($directory -and (Test-Path -LiteralPath "$directory/report.json")) {
        $failedReport = Get-Content -LiteralPath "$directory/report.json" -Raw | ConvertFrom-Json
        if ($failedReport.status -ne 'Cancelled') { $failedReport.status = 'Failed' }
        Write-VerificationJson "$directory/report.json" $failedReport
    }
}
$receipt = @{ok=(-not $failed);directory=$output;results=$summaries;schemaVersion=1}
if ($failed) { $receipt.error = $failure }
Write-VerificationJson "$output/result.json" $receipt
$receipt | ConvertTo-Json -Depth 12
if ($failed) { exit 1 }
