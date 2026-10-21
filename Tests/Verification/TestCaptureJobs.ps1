<#
.SYNOPSIS
Exercises protocol v4 admission, bounded receipts, bundle publication and cancellation
through a real hidden session. This is not GPU/device-loss fault qualification.
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
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/CaptureJobs' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @()
$cases = @()
$script:controlId = 0
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Send-Control([hashtable]$Request, [int]$Protocol = 4) {
    $Request.protocol = $Protocol
    $Request.id = ++$script:controlId
    $client = New-Object IO.Pipes.NamedPipeClientStream('.', "gglab-session-$session", [IO.Pipes.PipeDirection]::InOut)
    try {
        $client.Connect(5000)
        $encoding = New-Object Text.UTF8Encoding($false)
        $writer = New-Object IO.StreamWriter($client, $encoding, 4096, $true)
        $writer.NewLine = "`n"
        $writer.WriteLine(($Request | ConvertTo-Json -Compress -Depth 16))
        $writer.Flush()
        $reader = New-Object IO.StreamReader($client, $encoding, $false, 4096, $true)
        $task = $reader.ReadLineAsync()
        if (-not $task.Wait([TimeSpan]::FromSeconds(30))) { throw 'Control response timeout' }
        $response = $task.Result | ConvertFrom-Json
        if ($response.protocol -ne 4 -or $response.id -ne $Request.id) { throw 'Unversioned or uncorrelated control response' }
        return $response
    } finally { $client.Dispose() }
}
function Submit-Waiting {
    return Send-Control @{ command='capture'; requiredContentId='Missing.CaptureJobs.Content'; settleFrames=0; outputDirectory=$directory }
}
function Await-Result([uint64]$Id, [bool]$Consume = $false) {
    return Wait-VerificationCondition -Poll { Send-Control @{command='result'; jobId=$Id; consume=$Consume} } `
        -Until { param($r) -not $r.ok -or $r.status -ne 'pending' } -TimeoutSeconds $TimeoutSeconds
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $directory = Join-Path $output "$config-$backend"
            $null = New-Item -ItemType Directory -Path $directory
            $session = "jobs-$runId-$config-$backend"
            $start = $null
            $caseFailure = $null
            try {
                $start = Invoke-VerificationSession start $session @{ Configuration=$config; Rhi=$backend;
                    Lab='gglab.lab.temporal_aa'; WindowSize='320x180'; NoDevTools=$true;
                    StateRoot="$output/$backend-state"; StartTimeoutSeconds=$TimeoutSeconds }
                Write-VerificationJson "$directory/start.json" $start
                $status = Send-Control @{command='status'}
                Check ($status.capabilities.captureJobs -and $status.capabilities.sequenceCaptureJobs -and
                    $status.capture.limits.activeJobs -eq 64 -and $status.capture.limits.receipts -eq 128 -and
                    $status.capture.limits.productsPerJob -eq 8 -and $status.capture.limits.resourceTargetsPerJob -eq 64) "$backend-capabilities"
                foreach ($old in @(1, 2, 3, 5)) {
                    $bad = Send-Control @{command='capture'} $old
                    Check (-not $bad.ok -and $bad.error -match 'protocol') "$backend-reject-version-$old"
                }
                $invalid = Send-Control @{command='capture'; products=@(@{kind='image';source='scene';optional=$true})}
                Check (-not $invalid.ok) "$backend-invalid-product"
                $invalid = Send-Control @{command='capture'; products=@(@{kind='graph'},@{kind='graph'})}
                Check (-not $invalid.ok -and $invalid.admission -eq 'Rejected' -and $invalid.reason -eq 'InvalidPlan' -and $null -eq $invalid.jobId) "$backend-rejected-plan"
                $image = Invoke-VerificationSession capture $session @{ SettleFrames=2; OutputDirectory=$directory; Label='image' }
                Write-VerificationJson "$directory/image-receipt.json" $image
                Check ($image.protocol -eq 4 -and $image.status -eq 'completed' -and $image.publication -eq 'Published' -and $image.consumed -and
                    (Test-Path (Get-VerificationCaptureFile $image '.png')) -and (Test-Path $image.manifest)) "$backend-script-consumes-completed-bundle"
                $missing = Send-Control @{command='result';jobId=$image.jobId}
                Check (-not $missing.ok -and $missing.status -eq 'not-found') "$backend-consumed-job-not-found"
                $retained = Invoke-VerificationSession graph $session @{ Summary=$true; KeepResult=$true; OutputDirectory=$directory; Label='summary' }
                $first = Send-Control @{command='result';jobId=$retained.jobId}
                $second = Send-Control @{command='result';jobId=$retained.jobId}
                $before = (Send-Control @{command='status'}).capture.reservedReceipts
                Check ($first.status -eq 'completed' -and -not $first.consumed -and
                    ($first.products | ConvertTo-Json -Compress -Depth 32) -eq ($second.products | ConvertTo-Json -Compress -Depth 32) -and $before -eq 1) "$backend-query-borrows-one-receipt"
                $finished = Send-Control @{command='capture-cancel';jobId=$retained.jobId}
                Check ($finished.cancellation -eq 'Finished') "$backend-cancel-finished-does-not-rewrite-outcome"
                $taken = Send-Control @{command='result';jobId=$retained.jobId;consume=$true}
                Check ($taken.consumed -and $taken.publication -eq 'Published' -and
                    (Send-Control @{command='status'}).capture.reservedReceipts -eq 0) "$backend-explicit-consumption-releases-credit"
                $products = @(@{kind='image';source='scene'}, @{kind='image';source='composited'}, @{kind='diagnostic';diagnosticTap='temporal-history-weight'},
                    @{kind='resource';targets=@(@{resource='TAA.ResolvedSceneColor'})}, @{kind='graph'})
                $bundle = Invoke-VerificationSession capture $session @{ Products=($products | ConvertTo-Json -Compress -Depth 8);
                    OutputDirectory=$directory; Label='multi' }
                Write-VerificationJson "$directory/multi-receipt.json" $bundle
                Check ($bundle.status -eq 'completed' -and $bundle.publication -eq 'Published' -and $bundle.products.Count -eq 5 -and ($bundle.products.kind -join ',') -eq 'image,image,diagnostic,resource,graph' -and
                    @($bundle.products | Where-Object { $_.status -ne 'completed' -or $_.publication -ne 'Published' }).Count -eq 0 -and
                    (Test-Path $bundle.manifest)) "$backend-multi-product-publication"
                $manifest = Get-Content -LiteralPath $bundle.manifest -Raw | ConvertFrom-Json
                Check ($manifest.products.Count -eq 5 -and $manifest.jobId -eq $bundle.jobId -and
                    @($bundle.products.metadata.frame.serial | Select-Object -Unique).Count -eq 1) "$backend-same-frame-products"
                $badExport = Invoke-VerificationSession export $session @{ Targets='[{"resource":"Missing.CaptureJobs.Resource"}]';
                    IncludeGraph=$true; OutputDirectory=$directory; Label='failed' }
                Write-VerificationJson "$directory/failed-receipt.json" $badExport
                Check ($badExport.status -eq 'failed' -and $badExport.publication -ne 'Published' -and $null -eq $badExport.manifest -and
                    $badExport.products.Count -eq 2 -and $badExport.consumed) "$backend-required-product-failure-has-no-complete-manifest"

                $queued = Invoke-VerificationSession capture $session @{NoWait=$true;RequiredContentId='Missing.CaptureJobs.Client';OutputDirectory=$directory}
                $query = Invoke-VerificationSession result $session @{JobId=$queued.jobId}
                $cancelled = Invoke-VerificationSession capture-cancel $session @{JobId=$queued.jobId}
                $null = Await-Result $queued.jobId
                $consumed = Invoke-VerificationSession result $session @{JobId=$queued.jobId;Consume=$true}
                Check ($queued.admission -eq 'Accepted' -and $query.status -eq 'pending' -and -not $query.consumed -and
                    $cancelled.cancellation -eq 'Cancelled' -and $consumed.status -eq 'cancelled' -and $consumed.consumed) "$backend-script-job-query-cancel-consume"
                $timeout = Invoke-VerificationSession capture $session @{RequiredContentId='Missing.CaptureJobs.Timeout';TimeoutSeconds=0;OutputDirectory=$directory} -AllowFailure
                Check (-not $timeout.ok -and $timeout.status -eq 'pending' -and -not $timeout.consumed -and $timeout.jobId -gt 0) "$backend-client-timeout-retains-recoverable-id"
                $null = Send-Control @{command='capture-cancel';jobId=$timeout.jobId}
                $null = Await-Result $timeout.jobId $true

                # Missing content guarantees waiting, so pressure tests do not depend on GPU timing.
                $ids = @()
                for ($index=0; $index -lt 64; ++$index) {
                    $a = Submit-Waiting
                    if (-not $a.ok -or $a.admission -ne 'Accepted') { throw 'Could not fill active job capacity' }
                    $ids += $a.jobId
                }
                $busy = Submit-Waiting
                Check (-not $busy.ok -and $busy.admission -eq 'Busy' -and $busy.reason -eq 'ActiveJobLimit' -and $null -eq $busy.jobId) "$backend-active-pressure"
                $pending = Send-Control @{command='result';jobId=$ids[0];consume=$true}
                Check ($pending.status -eq 'pending' -and -not $pending.consumed -and
                    (Send-Control @{command='status'}).capture.reservedReceipts -eq 64) "$backend-pending-consumption-does-not-release-credit"
                foreach ($id in $ids) {
                    $c = Send-Control @{command='capture-cancel';jobId=$id}
                    if ($c.cancellation -ne 'Cancelled') { throw 'Waiting cancellation did not cancel delivery' }
                }
                $r = Await-Result $ids[0]
                Check ($r.status -eq 'cancelled' -and $r.publication -eq 'NotPublished' -and $r.products[0].kind -eq 'image' -and -not $r.consumed) "$backend-cancelled-terminal-receipt"
                for ($index=0; $index -lt 64; ++$index) {
                    $a = Submit-Waiting
                    if (-not $a.ok) { throw 'Terminal receipts incorrectly hold active job credit' }
                    $ids += $a.jobId
                    $c = Send-Control @{command='capture-cancel';jobId=$a.jobId}
                    if ($c.cancellation -ne 'Cancelled') { throw 'Waiting cancellation failed' }
                }
                $busy = Submit-Waiting
                $full = Send-Control @{command='status'}
                Check (-not $busy.ok -and $busy.admission -eq 'Busy' -and $busy.reason -eq 'ReceiptLimit' -and $null -eq $busy.jobId -and
                    $full.capture.reservedReceipts -eq 128 -and $full.capture.activeJobs -eq 0) "$backend-unconsumed-receipts-backpressure"
                # Sequence jobs share those slots and consume only their own receipts.
                $sequenceDirectory = Join-Path $directory 'Sequence'
                $sequenceStart = Send-Control @{command='sequence';path='SEQ_TemporalAALab_MovingObject';
                    captureFrames=@(0..179);outputDirectory=$sequenceDirectory;label='bounded';
                    exports=@(@{frame=179;targets=@(@{resource='TAA.ResolvedSceneColor'});graph=$true})}
                Check ($sequenceStart.ok) "$backend-sequence-starts-under-receipt-pressure"
                $blocked = Send-Control @{command='status'}
                Write-VerificationJson "$directory/sequence-blocked.json" $blocked
                Check ($blocked.sequence.submittedFrames -eq 0 -and $blocked.sequence.acceptedCaptureJobs -eq 0 -and
                    $blocked.capture.reservedReceipts -eq 128) "$backend-sequence-defers-before-admission"
                $last = Await-Result $ids[-1] $true
                $sequenceFinal = Wait-VerificationCondition -Poll { Send-Control @{command='status'} } `
                    -Until { param($r) $r.sequence.state -in @('completed','failed','cancelled') } -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/sequence-completed.json" $sequenceFinal
                Check ($sequenceFinal.sequence.state -eq 'completed' -and $sequenceFinal.sequence.submittedFrames -eq 180 -and
                    $sequenceFinal.sequence.acceptedCaptureJobs -eq 180 -and $sequenceFinal.sequence.finishedCaptureJobs -eq 180 -and
                    $sequenceFinal.sequence.pendingCaptureJobs -eq 0 -and $sequenceFinal.sequence.completedCaptures -eq 181 -and
                    $sequenceFinal.capture.reservedReceipts -eq 127 -and $sequenceFinal.capture.activeJobs -eq 0 -and
                    $null -eq $sequenceFinal.capture.PSObject.Properties['legacyRetainedResults']) "$backend-long-sequence-reuses-one-receipt-without-consuming-foreign-results"
                $manifests = @(Get-ChildItem -LiteralPath $sequenceDirectory -Filter '*.capture-bundle.json')
                $paired = @($manifests | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } |
                    Where-Object { $_.products.Count -eq 3 })
                Check ($manifests.Count -eq 180 -and $paired.Count -eq 1 -and
                    $paired[0].provenance.sequence.frame -eq 179) "$backend-sequence-publishes-manifest-per-frame-and-required-resource-graph"
                $replacement = Submit-Waiting
                Check ($last.consumed -and $replacement.ok -and
                    $replacement.jobId -eq ($ids[-1]+$sequenceFinal.sequence.acceptedCaptureJobs+1)) "$backend-out-of-order-consumption-slot-reuse-without-busy-identity-loss"
                $null = Send-Control @{command='capture-cancel';jobId=$replacement.jobId}
                $null = Await-Result $replacement.jobId $true
                foreach ($id in $ids[0..126]) {
                    $taken = Await-Result $id $true
                    if (-not $taken.consumed) { throw 'Receipt was evicted or consumed twice' }
                }
                $missing = Send-Control @{command='result';jobId=$ids[0]}
                $cancelMissing = Send-Control @{command='capture-cancel';jobId=$ids[0]}
                Check (-not $missing.ok -and $missing.status -eq 'not-found' -and $cancelMissing.cancellation -eq 'NotFound') "$backend-no-consumed-id-tombstone"
                $empty = Send-Control @{command='status'}
                Check ($empty.capture.reservedReceipts -eq 0 -and $empty.capture.activeJobs -eq 0 -and
                    $null -eq $empty.capture.PSObject.Properties['legacyRetainedResults'] -and
                    $null -eq $empty.capture.PSObject.Properties['legacyUnfinishedOutputs']) "$backend-no-host-result-cache"
                $cancelOutcomes = @()
                for ($index=0; $index -lt 8; ++$index) {
                    $a = Send-Control @{command='capture';timing='next-frame';settleFrames=0;outputDirectory=$directory;label='racing-cancel'}
                    if (-not $a.ok) { throw 'Cancellation race admission failed' }
                    $snapshot = Send-Control @{command='result';jobId=$a.jobId}
                    $c = Send-Control @{command='capture-cancel';jobId=$a.jobId}
                    $final = Await-Result $a.jobId $true
                    $cancelOutcomes += @{before=$snapshot.status;cancellation=$c.cancellation;status=$final.status;publication=$final.publication}
                    if ($c.cancellation -eq 'Cancelled') {
                        Check ($final.status -eq 'cancelled' -and $final.publication -eq 'NotPublished') "$backend-racing-cancel-prevents-publication-$index"
                    } else {
                        Check ($c.cancellation -in @('TooLate','Finished') -and $final.status -eq 'completed' -and $final.publication -eq 'Published') "$backend-late-cancel-preserves-publication-$index"
                    }
                }
                Write-VerificationJson "$directory/cancellation-outcomes.json" $cancelOutcomes
                # Session termination owns final cancellation/draining; receipts are not durable across processes.
                $abandoned = Submit-Waiting
                $beforeShutdown = Send-Control @{command='status'}
                Check ($abandoned.ok -and $beforeShutdown.capture.activeJobs -eq 1 -and
                    $beforeShutdown.capture.reservedReceipts -eq 1) "$backend-pending-job-before-shutdown"
                Write-VerificationJson "$directory/final-status.json" $beforeShutdown
            } catch {
                $caseFailure = $_
                throw
            } finally {
                if ($start) {
                    try { $null = Stop-VerificationSession $session -StartedSession $start -ReceiptPath "$directory/stop.json" -RequireCleanExit }
                    catch {
                        if ($null -eq $caseFailure) { throw }
                        [Console]::Error.WriteLine("Cleanup also failed: $($_.Exception.Message)")
                    }
                }
            }
            $log = Get-Content -LiteralPath $start.outputLog -Raw
            if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native validation errors: $($start.outputLog)" }
            $cases += @{configuration=$config;backend=$backend;passed=$true;directory=$directory}
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
