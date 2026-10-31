<# Exact full-array controls for receiver preparation, partitioning and repeated resolves. #>
[CmdletBinding()]
param(
    [string]$Configuration = 'Develop',
    [string[]]$Rhi = @('dx12','vulkan'),
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [string]$BaselineDirectory,
    [string]$Python = 'python',
    [switch]$RequireCache = $true,
    [uint32]$ReceiverQueryWork = 169984,
    [switch]$Coastal,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$output = New-VerificationRunDirectory -RunDirectory $RunDirectory
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
$environmentWarnings = @()
$windowSize = if ($Coastal) {'1280x800'} else {'129x81'}
$pixelCount = if ($Coastal) {1024000} else {10449}
$cases = @(
    @{id='diffuse-d1';fixture=8;depth=1;view='Contract_Sun';batch=7;tile=0},
    @{id='diffuse-d1-tiled';fixture=8;depth=1;view='Contract_Sun';batch=1;tile=4096},
    @{id='diffuse-d3';fixture=8;depth=3;view='Contract_Sun';batch=7;tile=0},
    @{id='diffuse-d3-tiled';fixture=8;depth=3;view='Contract_Sun';batch=1;tile=4096},
    @{id='small';fixture=13;depth=3;view='Contract_SmallReceiver';batch=7;tile=4096},
    @{id='grazing';fixture=13;depth=3;view='Contract_SmallReceiverGrazing';batch=7;tile=4096},
    @{id='visibility';fixture=10;depth=1;view='Contract_VisibilityNormal';batch=7;tile=0},
    @{id='visibility-tiled';fixture=10;depth=1;view='Contract_VisibilityNormal';batch=1;tile=4096}
)
if ($Coastal) {
    $cases = @('Retreat_SeaPassage','Retreat_Courtyard','GI_InteriorExterior','Retreat_Lounge' | ForEach-Object {
        @{id=$_;fixture=12;depth=1;view=$_;batch=7;tile=0}
    })
}
function Get-CacheSourceIdentity {
    $names = @(& git -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Projects PropertySheets Tests/Verification Scripts/Verification)
    if ($LASTEXITCODE -ne 0) { throw 'Receiver-cache source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object {
        [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash}
    })
}
function Get-CacheBinaryIdentity {
    return @(@('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') | ForEach-Object {
        [ordered]@{file=$_;sha256=(Get-FileHash "$root/Build/Output/x64/$Configuration/$_").Hash}
    })
}
function Invoke-CachePython([string]$Script, [string[]]$Arguments, [string]$Receipt) {
    $lines = @(& $Python $Script @Arguments)
    $code = $LASTEXITCODE
    Write-VerificationJson $Receipt @{exitCode=$code;output=($lines -join "`n")}
    if ($code -ne 0) { throw "Receiver-cache validation failed: $Script" }
}
$previousFixtures = $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT
$env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT = "$root/Build/Fixtures/GIVisibility"
try {
    Invoke-CachePython "$PSScriptRoot/ValidateGISpecularVisibility.py" @('--generate', $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT) "$output/fixtures.json"
    $sources = Get-CacheSourceIdentity
    $binaries = Get-CacheBinaryIdentity
    Write-VerificationJson "$output/source-identity.json" $sources
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $id = 'gi-cache-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $id @{Configuration=$Configuration;Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize=$windowSize;NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Receiver-cache checks require an owned session.' }
            $run = $start.runIdentity
            $warmup = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;Timing='after-ready';
                SettleFrames=1;OutputDirectory="$output/$backend/warmup";TimeoutSeconds=$TimeoutSeconds}
            if ($warmup.publication -ne 'Published') { throw 'Receiver-cache warmup failed.' }
            foreach ($case in $cases) {
                $changes = @(
                    @{id='gi_reference.fixture';type='enum';value=$case.fixture},
                    @{id='gi_reference.depth';type='uint';value=$case.depth},
                    @{id='gi_reference.samples';type='uint';value=8},
                    @{id='gi_reference.batch_samples';type='uint';value=$case.batch},
                    @{id='gi_reference.tile_pixels';type='uint';value=$case.tile},
                    @{id='gi_reference.seed';type='uint';value=61051}
                )
                if ($ReceiverQueryWork) {
                    $changes += @{id='gi_reference.receiver_query_work';type='uint';value=$ReceiverQueryWork}
                }
                $effect = Invoke-VerificationSession set $id @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
                if (-not $effect.batch.succeeded) { throw 'Receiver-cache parameters were not consumed.' }
                $names = if ($case.fixture -eq 10) {
                    @('Visibility','Base','Coat','Weights','Position','Diagnostics','F0','LayerEnergy' | ForEach-Object {"GI.SpecularVisibility.$_"})
                } else {
                    @('Irradiance','Diagnostics','Position','Identity','PathDiagnostics','SunIrradiance','EnvironmentIrradiance','LambertRadiance' | ForEach-Object {"GI.Reference.$_"})
                }
                # A second capture uses the same completed observation but a different frame.
                foreach ($repeat in 0..1) {
                    $directory = "$output/$backend/$($case.id)/resolve-$repeat"
                    $products = @(@{kind='resource';targets=@($names | ForEach-Object {@{resource=$_}})},@{kind='graph'}) | ConvertTo-Json -Depth 8 -Compress
                    $capture = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;Timing='after-ready';
                        SettleFrames=2;View=$case.view;Products=$products;OutputDirectory=$directory;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/capture.json" $capture
                    Write-VerificationJson "$directory/effect.json" $effect
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Receiver-cache capture failed.' }
                    if ($RequireCache) {
                        $historyNames = if ($case.fixture -eq 10) {
                            @('BaseSum','CoatSum','BaseCompensation','CoatCompensation' | ForEach-Object {"GI.SpecularVisibility.Previous$_"})
                        } else {
                            @('SunSum','EnvironmentSum','OutcomeSum','RandomState','SunCompensation','EnvironmentCompensation' | ForEach-Object {"GI.Reference.Previous$_"})
                        }
                        $historyNames += @('GI.Receiver.FaceNormal','GI.Receiver.Position')
                        $historyProducts = ConvertTo-Json -InputObject @(@{kind='resource';targets=@($historyNames | ForEach-Object {@{resource=$_}})}) -Depth 8 -Compress
                        $history = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;Timing='after-ready';
                            SettleFrames=2;View=$case.view;Products=$historyProducts;
                            OutputDirectory="$directory/history";TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$directory/history/capture.json" $history
                        if ($history.status -ne 'completed' -or $history.publication -ne 'Published') { throw 'Receiver-cache history capture failed.' }
                    }
                }
                if ($RequireCache) {
                    $status = Invoke-VerificationSession status $id @{ExpectedRunIdentity=$run}
                    Write-VerificationJson "$output/$backend/$($case.id)/status.json" $status
                    $gateName = if ($case.fixture -eq 10) {'gi-specular-visibility'} else {'gi-reference'}
                    $gate = @($status.frame.gates | Where-Object {$_.name -eq $gateName})[0]
                    if ($gate.detail -notmatch "receiver cache: $pixelCount/$pixelCount pixels, 0 query work") {
                        throw 'Completed resolve did not report a reusable, query-free receiver cache.'
                    }
                }
            }
        }
        finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $id @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$output/$backend/stop.json" $stop
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Receiver-cache session shutdown failed.' }
                $log = Get-Content -LiteralPath $start.outputLog -Raw
                # Preserve the raw log and report this known non-GPU startup limitation;
                # hidden control sessions do not need GameInput keyboard/mouse devices.
                if ($log -match 'GameInput is unavailable; continuing with UI-only input') {
                    $environmentWarnings += "$backend`: GameInput unavailable; UI-only input."
                }
                $log = ($log -split "`n" | Where-Object {$_ -notmatch 'GameInput is unavailable; continuing with UI-only input'}) -join "`n"
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-') { throw 'Receiver-cache native validation failed.' }
            }
        }
    }
    $arguments = @($output)
    if ($BaselineDirectory) { $arguments += @('--baseline', (Resolve-Path $BaselineDirectory).Path) }
    Invoke-CachePython "$PSScriptRoot/ValidateGIReceiverCache.py" $arguments "$output/arrays.json"
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        Invoke-CachePython "$root/Scripts/Verification/$validator" @($output) "$output/$validator.result.json"
    }
    if ((ConvertTo-Json -InputObject $sources -Compress) -cne
        (ConvertTo-Json -InputObject @(Get-CacheSourceIdentity) -Compress) -or
        (ConvertTo-Json -InputObject $binaries -Compress) -cne
        (ConvertTo-Json -InputObject @(Get-CacheBinaryIdentity) -Compress)) {
        throw 'Receiver-cache source or binary identities changed during verification.'
    }
    $result = @{ok=$true;directory=$output;baseline=$BaselineDirectory;receiverQueryWork=$ReceiverQueryWork;
        coastal=[bool]$Coastal;environmentWarnings=$environmentWarnings}
}
catch { $result = @{ok=$false;directory=$output;error=$_.Exception.Message} }
finally { $env:GGLAB_GI_VISIBILITY_FIXTURE_ROOT = $previousFixtures }
Write-VerificationJson "$output/result.json" $result
$result | ConvertTo-Json -Depth 8
if (-not $result.ok) { exit 1 }
