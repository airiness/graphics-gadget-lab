<#
.SYNOPSIS
Checks instrumented fixed-depth reference, pending cancellation and deep invalidity.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Develop')][string]$Configuration = 'Develop',
    [string[]]$Rhi = @('dx12','vulkan'),
    [Parameter(Mandatory=$true)][string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$output = New-VerificationRunDirectory -RunDirectory $RunDirectory
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Set-Reference([string]$Session, [string]$Run, [uint32]$Depth, [uint32]$Samples,
    [uint32]$Batch, [uint32]$Seed, [uint32]$TilePixels = 0) {
    $changes = @(@{id='gi_reference.fixture';type='enum';value=8},
        @{id='gi_reference.depth';type='uint';value=$Depth},
        @{id='gi_reference.samples';type='uint';value=$Samples},
        @{id='gi_reference.batch_samples';type='uint';value=$Batch},
        @{id='gi_reference.tile_pixels';type='uint';value=$TilePixels},
        @{id='gi_reference.seed';type='uint';value=$Seed},
        @{id='gi_reference.secondary_albedo';type='enum';value=1})
    $effect = Invoke-VerificationSession set $Session @{ExpectedRunIdentity=$Run;WaitConsumed=$true;
        Changes=($changes | ConvertTo-Json -Compress);TimeoutSeconds=$TimeoutSeconds}
    Check ($effect.batch.succeeded) "$Session-depth-$Depth-parameters"
    return $effect
}
try {
    foreach ($backend in $Rhi) {
        $directory = Join-Path $output $backend
        $null = New-Item -ItemType Directory -Path $directory
        $id = 'gi-transactions-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $id @{Configuration=$Configuration;Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;WatchNonFinite=$true;
                Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            $run = $start.runIdentity
            $warmup = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;
                Timing='after-ready';SettleFrames=1;OutputDirectory="$directory/warmup";TimeoutSeconds=$TimeoutSeconds}
            Check ($warmup.status -eq 'completed') "$backend-instrumented-ready"
            $targets = @('GI.Reference.Irradiance','GI.Reference.Diagnostics','GI.Reference.Position',
                'GI.Reference.Identity','GI.Receiver.HitAlbedo','GI.Reference.PathDiagnostics') | ForEach-Object {@{resource=$_}}
            # Boundary and production watch each retain one payload; six exports
            # keep their combined request within the Runtime's eight-payload limit.
            $products = @(@{kind='resource';targets=@($targets)},@{kind='graph'}) | ConvertTo-Json -Compress -Depth 6
            $watchTargets = @(@{resource='GI.Reference.Irradiance'},@{resource='GI.Reference.PathDiagnostics'}) |
                ConvertTo-Json -Compress
            $watch = Invoke-VerificationSession watch $id @{ExpectedRunIdentity=$run;WatchMode='report';
                WatchClaims=@('boundary','production');Targets=$watchTargets}
            Write-VerificationJson "$directory/watch-control.json" $watch
            $effect = Set-Reference $id $run 3 64 7 61051
            Write-VerificationJson "$directory/control-effect.json" $effect
            $control = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;
                Timing='after-ready';SettleFrames=1;View='Contract_Sun';Products=$products;
                OutputDirectory="$directory/control";TimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/control-receipt.json" $control
            Check ($control.status -eq 'completed' -and $control.publication -eq 'Published') "$backend-control-published"
            $status = Invoke-VerificationSession status $id @{ExpectedRunIdentity=$run}
            Write-VerificationJson "$directory/control-status.json" $status

            $effect = Set-Reference $id $run 4 1048576 1 61051 1024
            Write-VerificationJson "$directory/pending-effect.json" $effect
            $pending = Wait-VerificationCondition -Poll {
                Invoke-VerificationSession status $id @{ExpectedRunIdentity=$run}
            } -Until {param($s) @($s.frame.gates | Where-Object {
                $_.name -eq 'gi-reference' -and $_.state -eq 'pending' -and $_.detail -match 'D=4,' -and
                $_.detail -match 'tiles ([1-9][0-9]*)/([0-9]+) submitted' -and
                [int]$Matches[1] -lt [int]$Matches[2]}).Count -eq 1
            } -TimeoutSeconds $TimeoutSeconds
            Write-VerificationJson "$directory/pending-status.json" $pending
            $waiting = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;NoWait=$true;
                Timing='after-ready';OutputDirectory="$directory/cancelled";TimeoutSeconds=$TimeoutSeconds}
            $cancel = Invoke-VerificationSession capture-cancel $id @{ExpectedRunIdentity=$run;JobId=$waiting.jobId}
            $receipt = Invoke-VerificationSession result $id @{ExpectedRunIdentity=$run;JobId=$waiting.jobId;Consume=$true}
            Write-VerificationJson "$directory/cancellation.json" @{cancel=$cancel;receipt=$receipt}
            Check ($cancel.cancellation -eq 'Cancelled' -and $receipt.status -eq 'cancelled') "$backend-pending-cancelled"
            $effect = Set-Reference $id $run 2 64 7 61051
            Write-VerificationJson "$directory/reset-effect.json" $effect
            $reset = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;
                Timing='after-ready';SettleFrames=1;View='Contract_Sun';Products=$products;
                OutputDirectory="$directory/reset";TimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/reset-receipt.json" $reset
            Check ($reset.status -eq 'completed' -and $reset.publication -eq 'Published') "$backend-depth-reset-published"

            $inject = @{point='pass-gi-reference.csmain.throughput';value='NaN';x=160;y=100;component=0} |
                ConvertTo-Json -Compress
            $watch = Invoke-VerificationSession watch $id @{ExpectedRunIdentity=$run;WatchMode='report';
                WatchClaims=@('boundary','production');Targets=$watchTargets;Inject=$inject}
            Write-VerificationJson "$directory/watch-injection.json" $watch
            $effect = Set-Reference $id $run 3 64 7 61053
            Write-VerificationJson "$directory/injected-effect.json" $effect
            $injected = Invoke-VerificationSession capture $id @{ExpectedRunIdentity=$run;
                Timing='after-ready';SettleFrames=1;View='Contract_Sun';Products=$products;
                OutputDirectory="$directory/injected";TimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/injected-receipt.json" $injected
            Check ($injected.status -eq 'completed' -and $injected.publication -eq 'Published') "$backend-injected-evidence-published"
            $status = Invoke-VerificationSession status $id @{ExpectedRunIdentity=$run}
            Write-VerificationJson "$directory/injected-status.json" $status
        }
        finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $id @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$directory/stop.json" $stop
                Check ($stop.ok -and $stop.exitCode -eq 0) "$backend-exited-cleanly"
                $log = Get-Content -LiteralPath $start.outputLog -Raw
                Check ($log -notmatch 'Validation Error|VUID-|\[critical\]') "$backend-no-native-errors"
            }
        }
    }
    $lines = @(& $Python "$PSScriptRoot/ValidateGIReferenceTransport.py" --transactions $output 2>&1)
    $code = $LASTEXITCODE
    Write-VerificationJson "$output/validation.json" @{exitCode=$code;output=($lines -join "`n")}
    Check ($code -eq 0) 'transaction-and-deep-invalid-numerical-contracts'
    foreach ($validator in @('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')) {
        $lines = @(& $Python "$root/Scripts/Verification/$validator" $output 2>&1)
        $code = $LASTEXITCODE
        Write-VerificationJson "$output/$validator.result.json" @{exitCode=$code;output=($lines -join "`n")}
        Check ($code -eq 0) "$validator-transaction-evidence"
    }
    $result = @{ok=$true;directory=$output;checks=$checks}
}
catch { $result = @{ok=$false;directory=$output;checks=$checks;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 8))
if (-not $result.ok) { exit 1 }
