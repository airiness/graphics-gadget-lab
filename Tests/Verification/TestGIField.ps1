<# Static GI field full-update and lifecycle verification, separate from lookup/readiness acceptance. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or $Rhi.Count -lt 1 -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count -gt 0) {
    throw 'Field verification requires one configuration and valid backend names.'
}
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIField" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Check-Python([string]$Script, [string[]]$Arguments, [string]$Receipt) {
    $lines = @(& $Python $Script @Arguments)
    $code = $LASTEXITCODE
    Write-VerificationJson $Receipt @{exitCode=$code;output=($lines -join "`n")}
    if ($code -ne 0) { throw "Field verification failed: $Script" }
}
function Source-Identity {
    $names = @(& git -c "safe.directory=$($root.Replace('\','/'))" -C $root ls-files --cached --others --exclude-standard -- Sources Shaders Tests/Verification Scripts/Verification Schemas Projects PropertySheets)
    if ($LASTEXITCODE -ne 0) { throw 'Source enumeration failed.' }
    [Array]::Sort($names, [StringComparer]::Ordinal)
    return @($names | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$root/$_" -Algorithm SHA256).Hash} })
}
function Binary-Identity([string]$Config) {
    return @(@('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') | ForEach-Object {
        [ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath "$root/Build/Output/x64/$Config/$_" -Algorithm SHA256).Hash}
    })
}
function Set-Inputs([string]$Session, [string]$Run, [hashtable]$Values) {
    $changes = foreach ($key in $Values.Keys) {
        @{id="gi_reference.$key";type=$(if ($key -in @('fixture','ev100','environment')) {'enum'} elseif ($Values[$key] -is [bool]) {'bool'} else {'uint'});value=$Values[$key]}
    }
    $effect=Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject @($changes) -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Probe parameter batch was not consumed.' }
    return $effect
}
try {
    $source=Source-Identity
    Write-VerificationJson "$output/source-identity.json" $source
    $config=$Configuration[0]
    $binaries=Binary-Identity $config
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $directory="$output/$backend"
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        $session='gi-field-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start=$null
        try {
            $start=Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;Lab='gglab.lab.gi_reference';
                WindowSize='1280x800';NoDevTools=$true;Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Field verification requires an owned session.' }
            $run=$start.runIdentity
            foreach ($fixture in @(14,16,15)) {
                $effect=Set-Inputs $session $run @{fixture=$fixture;probe_enabled=$false;field_enabled=$true;
                    probe_rays=256;probe_allocation_diagnostics=$true;ev100=15}
                foreach ($step in @('off','activate','steady','replace','release','reactivate')) {
                    if ($step -in @('activate','reactivate')) { $effect=Set-Inputs $session $run @{probe_enabled=$true} }
                    if ($step -eq 'replace') {
                        $null=Set-Inputs $session $run @{field_enabled=$false}
                        $effect=Set-Inputs $session $run @{field_enabled=$true}
                    }
                    if ($step -eq 'release') { $effect=Set-Inputs $session $run @{probe_enabled=$false} }
                    $path="$directory/$fixture/$step"
                    $targets=@('Rays','Irradiance','Distance','State' | ForEach-Object {@{resource="GI.Field.$_"}})
                    $products=if ($step -in @('off','release')) { @(@{kind='image';source='scene'},@{kind='graph'}) } else {
                        @(@{kind='image';source='scene'},@{kind='resource';targets=$targets},@{kind='graph'}) }
                    $capture=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='after-ready';SettleFrames=32;
                        Products=(ConvertTo-Json -InputObject @($products) -Depth 8 -Compress);OutputDirectory=$path;TimeoutSeconds=$TimeoutSeconds}
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw "Field $step did not publish." }
                    Write-VerificationJson "$path/effect.json" $effect
                    Write-VerificationJson "$path/capture.json" $capture
                    Write-VerificationJson "$path/status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
                    Write-Verbose "$backend fixture $fixture field $step published"
                }
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop=Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$directory/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Field session shutdown failed.' }
                if ((Get-Content -LiteralPath $start.outputLog -Raw) -match '\[error\]|\[critical\]|Validation Error|VUID-') {
                    throw 'Field native/runtime validation failed.'
                }
            }
        }
    }
    Check-Python "$PSScriptRoot/ValidateGIField.py" @('--root',$output,'--rhi',($Rhi -join ',')) "$output/oracle-result.json"
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        Check-Python "$root/Scripts/Verification/$validator" @($output) "$output/$validator.result.json"
    }
    $finalSource=Source-Identity
    Write-VerificationJson "$output/source-identity-final.json" $finalSource
    $finalBinaries=Binary-Identity $config
    if ((ConvertTo-Json -InputObject @($source) -Depth 4 -Compress) -cne
        (ConvertTo-Json -InputObject @($finalSource) -Depth 4 -Compress) -or
        (ConvertTo-Json -InputObject @($binaries) -Compress) -cne
        (ConvertTo-Json -InputObject @($finalBinaries) -Compress)) { throw 'Sources or product/compiler binaries changed during field verification.' }
    Write-VerificationJson "$output/result.json" @{status='passed';observations=($Rhi.Count*18);backends=$Rhi;productionLookupQualified=$false;readinessQualified=$false}
} catch {
    Write-VerificationJson "$output/result.json" @{status='failed';error=[string]$_}
    [Console]::Out.WriteLine((@{ok=$false;directory=$output;error=[string]$_} | ConvertTo-Json -Compress))
    exit 1
}
[Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
