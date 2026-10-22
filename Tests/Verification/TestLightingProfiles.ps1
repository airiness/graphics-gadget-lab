<#
.SYNOPSIS
Qualifies content-registered lighting profiles and real Physical Sky generation
receipts. Does not qualify sequence events or persisted profile metadata.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/LightingProfiles' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @(); $cases = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Set-Values($Items, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $arguments = @{ Changes=(ConvertTo-Json -InputObject @($Items) -Compress -Depth 8); TimeoutSeconds=$TimeoutSeconds }
    foreach ($key in $Options.Keys) { $arguments[$key] = $Options[$key] }
    return Invoke-VerificationSession set $session $arguments -AllowFailure:$AllowFailure
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($content in @('coastal', 'fixture')) {
                $case = "$config-$backend-$content"
                $directory = Join-Path $output $case
                $null = New-Item -ItemType Directory -Path $directory
                $session = "profiles-$runId-$case"
                $start = $null; $caseFailure = $null; $arms = @()
                try {
                    $options = @{ Configuration=$config;Rhi=$backend;WindowSize='640x360';NoDevTools=$true;
                        Validation='standard';StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds }
                    if ($content -eq 'coastal') { $options.Demo='Demo.Playground.CoastalAtrium' }
                    else { $options.Lab='gglab.lab.atmosphere_range' }
                    $start = Invoke-VerificationSession start $session $options
                    Write-VerificationJson "$directory/start.json" $start
                    $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session } `
                        -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
                    $before = Invoke-VerificationSession parameters $session
                    Write-VerificationJson "$directory/discovery.json" $before
                    $desc = @($before.parameters | Where-Object id -eq 'environment.profile')[0]
                    Check ($status.capabilities.sessionParameterControl.lightingProfiles -and
                        $desc.source -eq 'host' -and $desc.type -eq 'enum' -and
                        $desc.declaredClass -eq 'Deferred' -and $desc.setSupported) "$case-profile-contract"
                    $ids = @($before.lightingProfiles | ForEach-Object { $_.id })
                    $expected = if ($content -eq 'coastal') { @('coastal.daylight') }
                        else { @('atmosphere_range.baseline', 'atmosphere_range.oblique') }
                    Check (($ids -join ',') -eq ($expected -join ',') -and
                        @($before.lightingProfiles | Where-Object { $_.hashVersion -ne 2 -or $_.hash -notmatch '^[0-9a-f]{64}$' }).Count -eq 0) "$case-content-registration-and-stable-hash"
                    if ($content -eq 'coastal') {
                        Check ($before.lightingProfiles[0].recommendedEV100 -eq 15) "$case-advisory-exposure"
                    }
                    foreach ($invalid in @(-1, 99)) {
                        $rejected = Set-Values @(@{id='environment.profile';type='enum';value=$invalid},
                            @{id='view.exposure.ev100';type='float';value=7.0}) -AllowFailure
                        $after = Invoke-VerificationSession parameters $session
                        Check (-not $rejected.ok -and $after.revision -eq $before.revision -and
                            @($after.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current -eq
                            @($before.parameters | Where-Object id -eq 'view.exposure.ev100')[0].current) "$case-invalid-$invalid-is-atomic"
                    }
                    if ($content -eq 'fixture') {
                        $rejected = Set-Values @(@{id='environment.profile';type='enum';value=1},
                            @{id='atmosphere_range.ibl.sample_count';type='uint';value=32}) -AllowFailure
                        Check (-not $rejected.ok) "$case-two-lighting-items-rejected"
                    }
                    $exposure = Set-Values @(@{id='view.exposure.ev100';type='float';value=11.0}) @{WaitConsumed=$true}
                    Check ($exposure.batch.succeeded -and $exposure.consumed) "$case-manual-exposure"
                    # Select baseline even if initial content inputs are currently custom.
                    $indices = if ($content -eq 'coastal') { @(0) } else { @(0, 1, 0) }
                    foreach ($index in $indices) {
                        $label = "arm-$($arms.Count)"
                        $changes = @(@{id='environment.profile';type='enum';value=$index})
                        $mixed = $content -eq 'fixture' -and $arms.Count -gt 0
                        if ($mixed) {
                            $changes += @{id='atmosphere_range.aerial_perspective';type='bool';value=($index -eq 0)}
                            $changes += @{id='view.exposure.ev100';type='float';value=11.0}
                        }
                        $selection = Set-Values $changes @{WaitConsumed=$true;KeepResult=$true}
                        Write-VerificationJson "$directory/$label-result.json" $selection
                        $item = $selection.batch.items[0]
                        Check ($selection.batch.terminal -and $selection.batch.succeeded -and
                            $item.state -in @('Consumed','NoChange')) "$case-$label-terminal-effect"
                        $current = Invoke-VerificationSession parameters $session
                        Write-VerificationJson "$directory/$label-publication.json" $current
                        $profile = $current.lightingProfiles[$index]
                        if ($mixed) {
                            Check ($selection.batch.source -eq 'mixed' -and $selection.batch.items.Count -eq 3 -and
                                $selection.batch.items[1].declaredClass -eq 'Immediate' -and
                                $selection.batch.items[1].state -eq 'Consumed' -and
                                $selection.batch.items[2].state -eq 'NoChange' -and
                                @($current.parameters | Where-Object id -eq 'atmosphere_range.aerial_perspective')[0].current -eq ($index -eq 0)) "$case-$label-mixed-source-commit"
                        }
                        Check ($current.lighting.requested.profileId -eq $profile.id -and
                            $current.lighting.active.profileId -eq $profile.id -and
                            $current.lighting.active.profileHash -eq $profile.hash -and
                            $current.lighting.active.generation -eq $current.lighting.requested.generation) "$case-$label-active-identity"
                        if ($item.state -eq 'Consumed') {
                            Check ($item.identity.domain -eq 'lighting' -and
                                $item.identity.generation -eq $current.lighting.active.generation -and
                                $item.requested -eq $item.applied -and $item.published -ge $item.applied -and
                                $item.firstConsumed -ge $item.published) "$case-$label-real-generation-receipt"
                        }
                        $capture = Invoke-VerificationSession capture $session @{ SettleFrames=8;OutputDirectory=$directory;Label=$label;TimeoutSeconds=$TimeoutSeconds }
                        Write-VerificationJson "$directory/$label-capture.json" $capture
                        $metadata = Get-Content (Get-VerificationCaptureFile $capture '.json') -Raw | ConvertFrom-Json
                        Check ($capture.status -eq 'completed' -and $capture.publication -eq 'Published' -and
                            $metadata.sceneInputs.environment.activeIBLGeneration -eq $current.lighting.active.generation -and
                            $metadata.sceneInputs.exposure.manualEV100 -eq 11 -and
                            ($item.state -eq 'NoChange' -or $capture.frameSerial -ge $item.firstConsumed)) "$case-$label-captured-generation-and-independent-exposure"
                        $taken = Invoke-VerificationSession parameter-result $session @{BatchId=$selection.batchId;Consume=$true}
                        Check $taken.consumed "$case-$label-result-ownership"
                        $same = Set-Values @(@{id='environment.profile';type='enum';value=$index}) @{WaitConsumed=$true}
                        $unchanged = Invoke-VerificationSession parameters $session
                        Check ($same.batch.succeeded -and $same.batch.items[0].state -eq 'NoChange' -and
                            $same.batch.revision -eq $current.revision -and
                            $unchanged.lighting.requested.generation -eq $current.lighting.requested.generation) "$case-$label-nochange-does-not-bake"
                        $arms += @{profile=$profile.id;generation=$current.lighting.active.generation;state=$item.state}
                    }
                    if ($content -eq 'fixture') {
                        Check ($arms[1].state -eq 'Consumed' -and $arms[2].state -eq 'Consumed' -and
                            $arms[2].generation -gt $arms[1].generation -and
                            $arms[1].generation -gt $arms[0].generation) "$case-authored-roundtrip-advances-actual-generations"
                    }
                    Write-VerificationJson "$directory/arms.json" $arms
                } catch { $caseFailure=$_;throw }
                finally {
                    if ($start) {
                        try { $null=Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                        catch {
                            if ($null -eq $caseFailure) { throw }
                            [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                        }
                    }
                }
                $log = Get-Content -LiteralPath $start.outputLog -Raw
                if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native validation errors: $($start.outputLog)" }
                foreach ($arm in @($arms | Where-Object state -eq 'Consumed')) {
                    $pattern = 'IBL bake generation '+$arm.generation+' published atomically \(cacheStages=0, gpuStages=[1-9]\d*,.*source=Physical Sky'
                    Check ([regex]::IsMatch($log, $pattern)) "$case-native-publication-matches-receipt-$($arm.generation)"
                }
                $cases += @{configuration=$config;backend=$backend;content=$content;passed=$true;directory=$directory}
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
