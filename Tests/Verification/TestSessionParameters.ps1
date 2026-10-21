<#
.SYNOPSIS
Exercises Immediate Lab session batches, receipt ownership and native GTAO changes.
No deferred effects, GPU fault injection or sequence event qualification is claimed.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
Add-Type -AssemblyName System.Drawing
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/SessionParameters' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Set-Values($Items, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $optionsCopy = @{ Changes=(ConvertTo-Json -InputObject @($Items) -Compress -Depth 8) }
    foreach ($key in $Options.Keys) { $optionsCopy[$key] = $Options[$key] }
    return Invoke-VerificationSession set $session $optionsCopy -AllowFailure:$AllowFailure
}
function Check-Consumed($Receipt, [string]$Name) {
    Check ($Receipt.batch.terminal -and $Receipt.batch.succeeded) "$Name-terminal-success"
    foreach ($item in $Receipt.batch.items) {
        if ($item.state -eq 'NoChange') {
            Check ($item.applied -eq $item.requested -and $null -eq $item.published -and $null -eq $item.firstConsumed) "$Name-nochange-stamps"
        } else {
            Check ($item.state -eq 'Consumed' -and $item.requested -gt 0 -and
                $item.applied -eq $item.requested -and $item.published -eq $item.applied -and
                $item.firstConsumed -ge $item.published -and $item.identity.revision -eq $Receipt.batch.revision -and
                $item.identity.domain -eq '' -and $item.identity.generation -eq 0) "$Name-exact-immediate-stamps"
        }
    }
}
function Capture-AO([string]$Label, [double]$Radius) {
    $capture = Invoke-VerificationSession capture $session @{ Source='diagnostic'; DiagnosticTap='gtao-final-ao';
        SettleFrames=8; OutputDirectory=$directory; Label=$Label; TimeoutSeconds=$TimeoutSeconds }
    Write-VerificationJson "$directory/$Label-receipt.json" $capture
    $metadata = Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
    Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published' -and
        [Math]::Abs($metadata.temporal.gtao.radius - $Radius) -lt 0.00001 -and
        -not $metadata.temporal.gtao.temporalAccumulation) "$backend-$Label-built-settings"
    return Get-VerificationCaptureFile $capture '.png'
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $directory = Join-Path $output "$config-$backend"
            $null = New-Item -ItemType Directory -Path $directory
            $session = "params-$runId-$config-$backend"
            $start = $null; $caseFailure = $null
            try {
                $start = Invoke-VerificationSession start $session @{ Configuration=$config; Rhi=$backend;
                    Lab='gglab.lab.gtao'; WindowSize='640x360'; NoDevTools=$true; Validation='standard';
                    StateRoot="$directory/state"; StartTimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/start.json" $start
                $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                    -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
                Check ($status.capabilities.labParameterControl.maxBatches -eq 1 -and
                    $status.parameterControl.available -and $null -eq $status.parameterControl.batch) "$backend-capabilities"
                $parameters = Invoke-VerificationSession parameters $session
                Write-VerificationJson "$directory/parameters.json" $parameters
                $radius = @($parameters.parameters | Where-Object id -eq 'gtao.radius')[0]
                Check ($radius.type -eq 'float' -and $radius.source -eq 'lab' -and $radius.setSupported -and
                    [Math]::Abs($radius.min - 0.01) -lt 0.00001 -and $radius.max -eq 10 -and $radius.declaredClass -eq 'Immediate') "$backend-discovery"
                foreach ($bad in @(
                    @(@{id='gtao.radius';type='float';value=2},@{id='unknown';type='bool';value=$true}),
                    @(@{id='gtao.radius';type='float';value=2},@{id='gtao.enabled';type='bool';value=1}),
                    @(@{id='gtao.radius';type='float';value=100}),
                    @(@{id='gtao.radius';type='uint';value=2}),
                    @(@{id='gtao.preview_tap';type='enum';value=2147483647}),
                    @(@{id='gtao.radius';type='float';value=2},@{id='gtao.radius';type='float';value=3})
                )) {
                    $rejected = Set-Values $bad -AllowFailure
                    Check (-not $rejected.ok) "$backend-invalid-whole-batch"
                }
                $unchanged = Invoke-VerificationSession parameters $session
                Check ($unchanged.revision -eq $parameters.revision -and
                    @($unchanged.parameters | Where-Object id -eq 'gtao.radius')[0].current -eq $radius.current) "$backend-rejection-does-not-mutate"
                $small = Set-Values @(@{id='gtao.radius';type='float';value=0.1},
                    @{id='gtao.temporal_accumulation';type='bool';value=$false}) @{NoWait=$true}
                Write-VerificationJson "$directory/admission.json" $small
                Check ($small.admission -eq 'Accepted' -and $small.batch.state -eq 'Queued' -and
                    $null -eq $small.batch.revision -and $small.batch.values.Count -eq 2 -and -not $small.consumed) "$backend-nowait-is-admission"
                $busy = Set-Values @(@{id='gtao.radius';type='float';value=4}) -AllowFailure
                Check (-not $busy.ok -and $busy.error -match 'Busy') "$backend-retained-result-blocks-new-batch"
                $wrong = Invoke-VerificationSession parameter-result $session @{BatchId=($small.batchId+1);Consume=$true} -AllowFailure
                Check (-not $wrong.ok -and $wrong.status -eq 'not-found') "$backend-wrong-id-cannot-consume"
                $done = Wait-VerificationCondition -Poll { Invoke-VerificationSession parameter-result $session @{BatchId=$small.batchId} } `
                    -Until { param($r) $r.batch.terminal } -TimeoutSeconds $TimeoutSeconds
                Check-Consumed $done "$backend-small"
                $borrowed = Invoke-VerificationSession parameter-result $session @{BatchId=$small.batchId}
                $status = Invoke-VerificationSession status $session
                Check (-not $borrowed.consumed -and ($borrowed.batch | ConvertTo-Json -Compress -Depth 12) -eq
                    ($status.parameterControl.batch | ConvertTo-Json -Compress -Depth 12)) "$backend-status-receipt-agrees"
                $taken = Invoke-VerificationSession parameter-result $session @{BatchId=$small.batchId;Consume=$true}
                Check ($taken.consumed -and (Invoke-VerificationSession status $session).parameterControl.batch -eq $null) "$backend-terminal-consumption-releases-slot"
                $again = Invoke-VerificationSession parameter-result $session @{BatchId=$small.batchId;Consume=$true} -AllowFailure
                Check (-not $again.ok -and $again.status -eq 'not-found') "$backend-repeated-consumption-is-not-found"
                $smallImage = Capture-AO 'small-radius' 0.1
                $changesFile = Join-Path $directory 'large radius.json'
                Write-VerificationJson $changesFile @(@{id='gtao.radius';type='float';value=4.0})
                $large = Invoke-VerificationSession set $session @{ChangesFile=$changesFile;WaitConsumed=$true;KeepResult=$true}
                Write-VerificationJson "$directory/large-result.json" $large
                Check-Consumed $large "$backend-large"
                Check (-not $large.consumed -and $large.batch.revision -gt $done.batch.revision) "$backend-wait-consumed-keeps-result"
                $null = Invoke-VerificationSession parameter-result $session @{BatchId=$large.batchId;Consume=$true}
                $largeImage = Capture-AO 'large-radius' 4
                $a = New-Object Drawing.Bitmap($smallImage); $b = New-Object Drawing.Bitmap($largeImage)
                try {
                    [double]$difference=0; [int]$changed=0; [int]$samples=0
                    for ($y=0; $y -lt $a.Height; $y+=4) {
                        for ($x=0; $x -lt $a.Width; $x+=4) {
                            $delta=[Math]::Abs([int]$a.GetPixel($x,$y).R-[int]$b.GetPixel($x,$y).R)
                            $difference+=$delta; $samples++; if ($delta -gt 1) { $changed++ }
                        }
                    }
                    Write-VerificationJson "$directory/ao-response.json" @{sampledPixels=$samples;changedPixels=$changed;meanAbsolute8BitDifference=($difference/$samples)}
                    Check ($changed -gt 10 -and $difference/$samples -gt 0.1) "$backend-actual-ao-output-changes"
                } finally { $a.Dispose(); $b.Dispose() }
                $nochange = Set-Values @(@{id='gtao.radius';type='float';value=4})
                Check ($nochange.consumed -and $nochange.batch.revision -eq $large.batch.revision -and
                    $nochange.batch.items[0].state -eq 'NoChange') "$backend-default-wait-consumes-nochange-without-revision"
                Write-VerificationJson "$directory/nochange-result.json" $nochange
                Check-Consumed $nochange "$backend-nochange"
                $waiting = Invoke-VerificationSession sequence $session @{CameraPath='SEQ_GTAOLab_Static';RequiredContentId='Missing.Parameters.Content';NoWait=$true}
                $during = Set-Values @(@{id='gtao.radius';type='float';value=2}) -AllowFailure
                Check (-not $during.ok -and $during.error -match 'sequence') "$backend-sequence-blocks-external-set"
                $null = Invoke-VerificationSession sequence-cancel $session
                Write-VerificationJson "$directory/final-status.json" (Invoke-VerificationSession status $session)
            } catch { $caseFailure=$_; throw }
            finally {
                if ($start) {
                    try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                    catch {
                        if ($null -eq $caseFailure) { throw }
                        [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                    }
                }
            }
            $log=Get-Content -LiteralPath $start.outputLog -Raw
            if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native validation errors: $($start.outputLog)" }
            $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
        }
    }
    $result=@{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result=@{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
