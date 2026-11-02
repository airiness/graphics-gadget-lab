<# Bounded DDGI probe observations, same-frame tile oracle and identity lifecycle. #>
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
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Probe inspection requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeInspection" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Set-Field($Session, $Run, $Fixture, [bool]$Readiness = $true) {
    $changes = @(
        @{id='gi_reference.fixture';type='enum';value=$Fixture},
        @{id='gi_reference.field_enabled';type='bool';value=$true},
        @{id='gi_reference.field_sampling';type='bool';value=$true},
        @{id='gi_reference.field_readiness';type='bool';value=$Readiness},
        @{id='gi_reference.probe_enabled';type='bool';value=$true},
        @{id='gi_reference.seed';type='uint';value=61051}
    )
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
    if (-not $effect.batch.succeeded) { throw 'Probe inspection field setup failed.' }
    return Wait-VerificationCondition -Poll {
        Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run}
    } -Until { param($r) $r.frame -and $r.frame.giField -and $r.frame.giField.summary.inspectionAvailable } -TimeoutSeconds $TimeoutSeconds
}
try {
    $source = Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    foreach ($backend in $Rhi) {
        $session = 'gi-inspect-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Probe verification requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll {
                Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
            } -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            $cases = @(
                @{id='plane';fixture=1;coordinate=@(6,9,6);readiness=$true},
                @{id='excluded';fixture=14;coordinate=@(0,0,0);readiness=$true},
                @{id='fixed';fixture=1;coordinate=@(6,9,6);readiness=$false},
                @{id='sunroom';fixture=4;coordinate=@(8,6,8);readiness=$true}
            )
            $previous = $null
            foreach ($case in $cases) {
                New-Item -ItemType Directory -Path "$output/$backend/$($case.id)" -Force | Out-Null
                $status = Set-Field $session $run $case.fixture $case.readiness
                Write-VerificationJson "$output/$backend/$($case.id)/status.json" $status
                if ($previous -and $previous -eq $status.frame.giField.generation) { throw 'Replacement reused a field generation.' }
                $previous = $status.frame.giField.generation
                $request = @{coordinate=$case.coordinate;expectedGeneration=$previous;
                    expectedRefreshEpoch=$status.frame.giField.summary.field.refreshEpoch}
                $products = @(@{kind='image';source='scene'},@{kind='graph'})
                # Full atlases are test-only oracles. Native inspection itself exports only the compact target.
                if ($case.id -eq 'plane') {
                    $products += @{kind='resource';targets=@(@{resource='GI.Field.Irradiance'},@{resource='GI.Field.Distance'},@{resource='GI.Field.Readiness'})}
                }
                $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';SettleFrames=0;
                    GIProbeInspection=(ConvertTo-Json -InputObject $request -Compress);
                    Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);
                    OutputDirectory="$output/$backend/$($case.id)";TimeoutSeconds=$TimeoutSeconds}
                if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Probe inspection did not publish.' }
                Write-VerificationJson "$output/$backend/$($case.id)/receipt.json" $capture -Depth 32
                # Old generations fail explicitly through the ordinary job result path.
                $request.expectedGeneration = [uint64]::MaxValue
                $stale = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';
                    GIProbeInspection=(ConvertTo-Json -InputObject $request -Compress);TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                Write-VerificationJson "$output/$backend/$($case.id)/stale.json" $stale
                if ($stale.status -ne 'failed' -or $stale.failureCode -ne 'GIProbeIdentityMismatch') { throw 'Stale identity did not fail explicitly.' }
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Probe session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Probe native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeInspection.py" @('--root',$output) "$output/oracle-result.json" 'Probe inspection failed'
    Assert-VerificationArtifacts $root $output $Python 'Probe inspection failed'
    Assert-VerificationIdentity $source (Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')) 'Probe source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Probe binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
