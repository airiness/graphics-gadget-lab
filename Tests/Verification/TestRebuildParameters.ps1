<#
.SYNOPSIS
Verifies session Rebuild transactions against real Forward+ scene activations.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/RebuildParameters/Native' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
$fixtureId = 'forward_plus.fixture'
function Check([bool]$Condition, [string]$Name) {
    $script:checks += @{name=$Name;passed=$Condition}
    if (-not $Condition) { throw "Failed: $Name" }
}
function Set-Values($Values, [switch]$Keep, [switch]$AllowFailure) {
    Invoke-VerificationSession set $script:session @{Changes=(ConvertTo-Json -InputObject @($Values) -Compress -Depth 8);
        WaitConsumed=$true;KeepResult=[bool]$Keep;TimeoutSeconds=$TimeoutSeconds} -AllowFailure:$AllowFailure
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $case = "$config-$backend"
            $directory = Join-Path $output $case
            $null = New-Item -ItemType Directory -Path $directory
            $script:session = "rebuild-$runId-$case"
            $start = $null; $failure = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend;
                    Lab='gglab.lab.forward_plus';WindowSize='640x360';NoDevTools=$true;Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                $ready = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                    -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                $before = Invoke-VerificationSession parameters $session
                $desc = @($before.parameters | Where-Object id -eq $fixtureId)[0]
                Check ($desc.setSupported -and $desc.declaredClass -eq 'Rebuild' -and $desc.impact -eq 'RebuildScene' -and
                    'Rebuild' -in $ready.capabilities.sessionParameterControl.effectClasses) "$case-discovery"
                [double]$exposure = @($before.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current
                foreach ($bad in @(@{id=$fixtureId;type='enum';value=999},@{id=$fixtureId;type='float';value=0.0})) {
                    $invalid = Set-Values @(@{id='view.exposure.ev100';type='float';value=($exposure+2)},$bad) -AllowFailure
                    $after = Invoke-VerificationSession parameters $session
                    Check (-not $invalid.ok -and $before.revision -eq $after.revision -and
                        @($after.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current -eq $exposure) "$case-atomic-rejection"
                }
                foreach ($fixture in @(0,1,2)) {
                    $values = @(@{id=$fixtureId;type='enum';value=$fixture},
                        @{id='view.exposure.ev100';type='float';value=$exposure})
                    $done = Set-Values $values -Keep
                    Write-VerificationJson "$directory/fixture-$fixture-receipt.json" $done
                    $item = @($done.batch.items | Where-Object id -eq $fixtureId)[0]
                    Check ($done.batch.succeeded -and -not $done.consumed -and $done.batch.impact -eq 'RebuildScene' -and
                        $item.state -eq 'Consumed' -and $item.identity.domain -eq 'lab-session' -and
                        $item.identity.generation -gt 0 -and $item.identity.revision -eq $done.batch.revision -and
                        $item.requested -le $item.applied -and $item.applied -lt $item.published -and
                        $item.published -le $item.firstConsumed) "$case-fixture-$fixture-four-stamps"
                    $busy = Set-Values $values -AllowFailure
                    Check (-not $busy.ok -and $busy.error -match 'Busy') "$case-fixture-$fixture-retained-result"
                    $capture = Invoke-VerificationSession capture $session @{SettleFrames=2;OutputDirectory=$directory;
                        Label="fixture-$fixture";Products='[{"kind":"image","source":"scene"},{"kind":"graph"}]';
                        TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/fixture-$fixture-capture.json" $capture
                    Check ($capture.status -eq 'completed') "$case-fixture-$fixture-capture"
                    $metadata = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.json' 0) -Raw | ConvertFrom-Json
                    $graph = Get-Content -LiteralPath (Get-VerificationCaptureFile $capture '.json' 1) -Raw | ConvertFrom-Json
                    $expectedLights = @(0,1,64)[$fixture]
                    Check ($metadata.sceneInputs.localLightCount -eq $expectedLights -and
                        $metadata.sceneInputs.exposure.manualEV100 -eq $exposure -and
                        $metadata.parameters.contentId -eq 'gglab.lab.forward_plus' -and
                        ($metadata.parameters | ConvertTo-Json -Compress -Depth 24) -eq
                        ($graph.frame.parameters | ConvertTo-Json -Compress -Depth 24)) "$case-fixture-$fixture-built-scene-and-provenance"
                    $null = Invoke-VerificationSession parameter-result $session @{BatchId=$done.batchId;Consume=$true}
                    $same = Set-Values $values
                    Check ($same.batch.succeeded -and $same.batch.impact -eq 'Immediate' -and
                        $same.batch.items[0].state -eq 'NoChange' -and $null -eq $same.batch.items[0].published -and
                        $null -eq $same.batch.items[0].firstConsumed) "$case-fixture-$fixture-nochange"
                }
                $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
            } catch { $failure=$_; throw }
            finally {
                if ($start) {
                    try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                    catch { if ($null -eq $failure) { throw }; [Console]::Error.WriteLine($_.Exception.Message) }
                    Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                }
            }
            $log = Get-Content -LiteralPath "$directory/output.log" -Raw
            Check ($log -notmatch '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') "$case-native-validation"
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
