<#
.SYNOPSIS
Exercises Immediate batches and bound Deferred lighting publication/consumption.
Use -Scope lighting for the two lighting fixtures only. No GPU fault injection or
sequence event qualification is claimed.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 300,
    [ValidateSet('all','lighting')]
    [string]$Scope = 'all'
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
function Test-LightingParameters([string]$Config, [string]$Backend, [string]$Parent) {
    foreach ($fixture in @(
        @{lab='gglab.lab.gtao';parameter='gtao.ibl.sample_count';immediate='gtao.radius';type='float';value=2.0;name='texture'},
        @{lab='gglab.lab.atmosphere_range';parameter='atmosphere_range.ibl.sample_count';immediate='atmosphere_range.aerial_perspective';type='bool';value=$false;name='physical'}
    )) {
        $directory=Join-Path $Parent $fixture.name
        $null=New-Item -ItemType Directory -Path $directory
        $session="lighting-$runId-$Config-$Backend-$($fixture.name)"
        $start=$null; $caseFailure=$null; $arms=@()
        try {
            $start=Invoke-VerificationSession start $session @{Configuration=$Config;Rhi=$Backend;Lab=$fixture.lab;
                WindowSize='640x360';NoDevTools=$true;Validation='standard';StateRoot="$directory/state";
                StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            $status=Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session} `
                -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
            Check ($status.capabilities.labParameterControl.effectClasses -contains 'Deferred' -and
                $status.capabilities.labParameterControl.maxLightingItemsPerBatch -eq 1) "$Backend-$($fixture.name)-deferred-capability"
            $before=Invoke-VerificationSession parameters $session
            $desc=@($before.parameters | Where-Object id -eq $fixture.parameter)[0]
            Check ($desc.declaredClass -eq 'Deferred' -and $desc.type -eq 'uint' -and $desc.setSupported) "$Backend-$($fixture.name)-bound-descriptor"
            $rejected=Set-Values @(@{id=$fixture.immediate;type=$fixture.type;value=$fixture.value},
                @{id=$fixture.parameter;type='uint';value=4097}) -AllowFailure
            $after=Invoke-VerificationSession parameters $session
            Check (-not $rejected.ok -and $after.revision -eq $before.revision -and
                @($after.parameters | Where-Object id -eq $fixture.immediate)[0].current -eq
                @($before.parameters | Where-Object id -eq $fixture.immediate)[0].current) "$Backend-$($fixture.name)-invalid-mixed-batch-is-atomic"
            $first=if ($desc.current -eq 32) {64} else {32}
            $second=if ($first -eq 32) {64} else {32}
            foreach ($samples in @($first,$second,$first)) {
                $index=$arms.Count
                $items=@(@{id=$fixture.parameter;type='uint';value=$samples})
                if ($index -eq 0) { $items+=@{id=$fixture.immediate;type=$fixture.type;value=$fixture.value} }
                $options=if ($index -eq 0) {@{KeepResult=$true}} else {@{WaitConsumed=$true;KeepResult=$true}}
                $applied=Set-Values $items $options
                Write-VerificationJson "$directory/arm-$index-applied.json" $applied
                $lighting=@($applied.batch.items | Where-Object id -eq $fixture.parameter)[0]
                Check ($null -ne $lighting.applied -and $lighting.identity.domain -eq 'lighting' -and
                    $lighting.identity.generation -gt 0) "$Backend-$($fixture.name)-$index-default-application-has-real-identity"
                $done=Wait-VerificationCondition -Poll {Invoke-VerificationSession parameter-result $session @{BatchId=$applied.batchId}} `
                    -Until {param($r) $r.batch.terminal} -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/arm-$index-result.json" $done
                $lighting=@($done.batch.items | Where-Object id -eq $fixture.parameter)[0]
                Check ($done.batch.succeeded -and $lighting.state -eq 'Consumed' -and
                    $lighting.requested -eq $lighting.applied -and $lighting.published -ge $lighting.applied -and
                    $lighting.firstConsumed -ge $lighting.published -and $lighting.identity.revision -eq $done.batch.revision) "$Backend-$($fixture.name)-$index-deferred-stamps"
                if ($index -eq 0) {
                    $immediate=@($done.batch.items | Where-Object id -eq $fixture.immediate)[0]
                    Check ($immediate.state -eq 'Consumed' -and $immediate.published -eq $immediate.applied -and
                        $immediate.identity.domain -eq '' -and $immediate.firstConsumed -le $lighting.firstConsumed) "$Backend-$($fixture.name)-mixed-items-retain-their-own-classes"
                }
                $capture=Invoke-VerificationSession capture $session @{SettleFrames=8;OutputDirectory=$directory;Label="arm-$index"}
                Write-VerificationJson "$directory/arm-$index-capture.json" $capture
                $metadata=Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
                Check ($metadata.sceneInputs.environment.activeIBLGeneration -eq $lighting.identity.generation -and
                    $metadata.sceneInputs.environment.iblConfig.specularSamples -eq $samples -and
                    $capture.frameSerial -ge $lighting.firstConsumed) "$Backend-$($fixture.name)-$index-captured-frame-uses-published-generation"
                $taken=Invoke-VerificationSession parameter-result $session @{BatchId=$done.batchId;Consume=$true}
                Check $taken.consumed "$Backend-$($fixture.name)-$index-explicit-terminal-consumption"
                $revision=$done.batch.revision
                $same=Set-Values @(@{id=$fixture.parameter;type='uint';value=$samples}) @{WaitConsumed=$true}
                Check ($same.consumed -and $same.batch.succeeded -and $same.batch.revision -eq $revision -and
                    $same.batch.items[0].state -eq 'NoChange' -and $null -eq $same.batch.items[0].published -and
                    $null -eq $same.batch.items[0].firstConsumed) "$Backend-$($fixture.name)-$index-nochange-keeps-generation-and-revision"
                $arms+=@{samples=$samples;generation=$lighting.identity.generation;revision=$revision}
            }
        } catch {$caseFailure=$_;throw}
        finally {
            if ($start) {
                try {$null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit}
                catch {
                    if ($null -eq $caseFailure) {throw}
                    [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                }
            }
        }
        $log=Get-Content -LiteralPath $start.outputLog -Raw
        if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') {throw "Native validation errors: $($start.outputLog)"}
        foreach ($arm in $arms) {
            $pattern='IBL bake generation '+$arm.generation+' published atomically \(cacheStages=(\d+), gpuStages=(\d+)'
            $match=[regex]::Match($log,$pattern)
            Check $match.Success "$Backend-$($fixture.name)-publication-log-matches-identity"
            $arm.cacheStages=[int]$match.Groups[1].Value; $arm.gpuStages=[int]$match.Groups[2].Value
        }
        if ($fixture.name -eq 'texture') {
            Check ($arms[0].gpuStages -gt 0 -and $arms[2].cacheStages -eq 4 -and $arms[2].gpuStages -eq 0 -and
                $arms[2].generation -gt $arms[0].generation) "$Backend-cache-restoration-publishes-a-new-generation-without-gpu-build-stages"
        } else {
            Check (@($arms | Where-Object {$_.cacheStages -ne 0 -or $_.gpuStages -eq 0}).Count -eq 0) "$Backend-physical-sky-uses-actual-uncached-gpu-bakes"
        }
        Write-VerificationJson "$directory/lighting-arms.json" $arms
    }
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $directory = Join-Path $output "$config-$backend"
            $null = New-Item -ItemType Directory -Path $directory
            if ($Scope -eq 'lighting') {
                Test-LightingParameters $config $backend $directory
                $cases += @{configuration=$config;backend=$backend;scope=$Scope;passed=$true;directory=$directory}
                continue
            }
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
                # Every admitted Immediate binding must prove its distinct value from the frozen frame.
                $projectedValues = @(
                    @{id='gtao.enabled';type='bool';value=$true},
                    @{id='gtao.final_ao_format';type='enum';value=1},
                    @{id='gtao.radius';type='float';value=2.5},
                    @{id='gtao.falloff_start';type='float';value=0.4},
                    @{id='gtao.falloff_end';type='float';value=1.7},
                    @{id='gtao.power';type='float';value=2.0},
                    @{id='gtao.direction_count';type='uint';value=3},
                    @{id='gtao.step_count';type='uint';value=4},
                    @{id='gtao.denoise_radius';type='uint';value=2},
                    @{id='gtao.temporal_accumulation';type='bool';value=$false},
                    @{id='gtao.temporal_max_samples';type='uint';value=7},
                    @{id='gtao.camera.near';type='float';value=0.1},
                    @{id='gtao.camera.far';type='float';value=500.0}
                )
                $projected = Set-Values $projectedValues @{WaitConsumed=$true}
                Write-VerificationJson "$directory/projected-bindings-result.json" $projected
                Check-Consumed $projected "$backend-every-projected-binding"
                $projectionState = Invoke-VerificationSession parameters $session
                $preview = @($projectionState.parameters | Where-Object id -eq 'gtao.preview_tap')[0]
                $unprojected = Set-Values @(@{id='gtao.radius';type='float';value=1.5},
                    @{id=$preview.id;type='enum';value=$preview.current}) -AllowFailure
                $afterUnprojected = Invoke-VerificationSession parameters $session
                Check (-not $preview.setSupported -and -not $unprojected.ok -and
                    $afterUnprojected.revision -eq $projectionState.revision -and
                    @($afterUnprojected.parameters | Where-Object id -eq 'gtao.radius')[0].current -eq 2.5) "$backend-unprojected-binding-rejects-whole-batch"
                # A normalized falloff differs from parameter storage, including a repeated unchanged request.
                $masked = Set-Values @(@{id='gtao.falloff_start';type='float';value=4.0}) @{WaitConsumed=$true}
                $maskedAgain = Set-Values @(@{id='gtao.falloff_start';type='float';value=4.0}) @{WaitConsumed=$true}
                Write-VerificationJson "$directory/normalized-falloff-result.json" $masked
                Write-VerificationJson "$directory/normalized-falloff-repeat.json" $maskedAgain
                Check (-not $masked.batch.succeeded -and $masked.batch.items[0].state -eq 'Superseded' -and
                    $null -eq $masked.batch.items[0].firstConsumed -and -not $maskedAgain.batch.succeeded -and
                    $maskedAgain.batch.items[0].state -eq 'Superseded' -and
                    $maskedAgain.batch.revision -eq $masked.batch.revision -and
                    $null -eq $maskedAgain.batch.items[0].firstConsumed) "$backend-normalized-frame-value-cannot-report-consumed-or-nochange"
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
            Test-LightingParameters $config $backend $directory
            $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
        }
    }
    $result=@{ok=$true;directory=$output;scope=$Scope;checks=$checks;cases=$cases}
} catch { $result=@{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
