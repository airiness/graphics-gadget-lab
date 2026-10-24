<#
.SYNOPSIS
Checks hidden graph exports for frozen allocator facts and logical sizes.
.DESCRIPTION
Runs the Temporal AA Lab on the selected backends and publishes a full graph, a
summary graph and a resource export with its paired full graph. Reads graph
products through completed bundle manifests. Every known allocation must be at
least the logical size of its resource, unique physical bytes must fit in the
allocator bytes sampled at the same point, and external facts must belong to
imported resources. Logical totals must agree across backends. This checks the
reported facts, not residency or performance. Every started session is stopped.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')]
    [string[]]$Configuration = @('Release'),
    [ValidateSet('dx12', 'vulkan')]
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
if (-not $OutputDirectory -and -not $RunDirectory) {
    $OutputDirectory = Join-Path $root 'Build/Validation/GraphAllocationFacts'
}
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId

function Read-GraphProduct {
    param($Result, [int]$ProductIndex)
    $file = Get-VerificationCaptureFile $Result '.json' $ProductIndex
    $path = if ([IO.Path]::IsPathRooted($file)) { $file } else { Join-Path (Split-Path -Parent $Result.manifest) $file }
    return @{ path = $path; graph = (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json) }
}

function Test-GraphMemoryFacts {
    param($Graph, [string[]]$NodeKinds)
    if ($Graph.schemaVersion -ne 6) { throw "Expected graph schemaVersion 6, found $($Graph.schemaVersion)." }
    if ((@($Graph.observation.nodeKinds) -join ',') -cne ($NodeKinds -join ',')) {
        throw "Graph node kinds '$(@($Graph.observation.nodeKinds) -join ',')' differ from '$($NodeKinds -join ',')'."
    }
    if ($Graph.mode -eq 'summary') {
        if ($Graph.PSObject.Properties['execution'] -or $Graph.compile.PSObject.Properties['resources']) {
            throw 'A summary graph carries full-only execution or resource fields.'
        }
        return @{ mode = 'summary'; passes = @($Graph.compile.passes).Count }
    }
    $resources = @($Graph.compile.resources)
    $known = 0
    $classes = @{}
    $reasons = @{}
    $external = @()
    foreach ($record in @($Graph.execution.allocations)) {
        $resource = $resources[$record.resourceIndex]
        if ($record.allocationFacts -eq 'known') {
            ++$known
            if ($record.alignmentBytes -le 0) { throw "Known allocation of $($resource.name) has no alignment." }
            if ($null -ne $resource.logicalBytes -and $record.allocatedBytes -lt $resource.logicalBytes) {
                throw "Allocation of $($resource.name) ($($record.allocatedBytes)) is smaller than its logical size ($($resource.logicalBytes))."
            }
            $class = [string]$record.memoryClass
            $classes[$class] = 1 + $(if ($classes.ContainsKey($class)) { $classes[$class] } else { 0 })
        } else {
            $reason = [string]$record.unavailableReason
            $reasons[$reason] = 1 + $(if ($reasons.ContainsKey($reason)) { $reasons[$reason] } else { 0 })
            if ($reason -eq 'external') {
                if (-not $record.imported) { throw "Transient resource $($resource.name) reported external facts." }
                $external += $resource.name
            }
        }
    }
    $totals = $Graph.execution.totals
    if ($known -eq 0) { throw 'A GPU graph export reported no known allocator facts.' }
    if ($null -eq $totals.allocatorAllocationBytes -or $null -eq $totals.allocatorBlockBytes) {
        throw 'A GPU graph export reported no allocator totals.'
    }
    if ($totals.uniquePhysicalAllocationBytes -gt $totals.allocationBytes -or
        $totals.uniquePhysicalAllocationBytes -gt $totals.allocatorAllocationBytes -or
        $totals.allocatorAllocationBytes -gt $totals.allocatorBlockBytes) {
        throw 'Unique physical bytes must fit in allocation bytes and allocator bytes sampled at the same point.'
    }
    return @{
        mode = 'full'; resources = $resources.Count; allocations = @($Graph.execution.allocations).Count
        known = $known; memoryClasses = $classes; unavailable = $reasons; external = $external
        logicalBytes = $totals.logicalBytes; unavailableLogicalResources = $totals.unavailableLogicalResources
        allocationBytes = $totals.allocationBytes; uniquePhysicalAllocationBytes = $totals.uniquePhysicalAllocationBytes
        allocatorAllocationBytes = $totals.allocatorAllocationBytes; allocatorBlockBytes = $totals.allocatorBlockBytes
    }
}

$cases = @()
try {
    foreach ($config in $Configuration) {
        $reference = $null
        foreach ($backend in $Rhi) {
            $directory = Join-Path $output "$config-$backend"
            $null = New-Item -ItemType Directory -Path $directory
            $sessionId = "allocation-$runId-$config-$backend"
            $start = $null
            try {
                $start = Invoke-VerificationSession start $sessionId @{
                    Configuration = $config; Rhi = $backend; Lab = 'gglab.lab.temporal_aa'
                    NoDevTools = $true; WindowSize = '320x180'
                    StateRoot = (Join-Path $directory 'State'); StartTimeoutSeconds = $TimeoutSeconds
                }
                Write-VerificationJson "$directory/start.json" $start
                $common = @{ SettleFrames = 8; OutputDirectory = $directory; TimeoutSeconds = $TimeoutSeconds }
                $full = Invoke-VerificationSession graph $sessionId ($common + @{ Label = 'full' })
                $summary = Invoke-VerificationSession graph $sessionId ($common + @{ Label = 'summary'; Summary = $true })
                $paired = Invoke-VerificationSession export $sessionId ($common + @{
                    Label = 'paired'; Targets = '[{"resource":"TAA.ResolvedSceneColor"}]'; IncludeGraph = $true
                })
                $fullGraph = Read-GraphProduct $full 0
                $summaryGraph = Read-GraphProduct $summary 0
                $pairedGraph = Read-GraphProduct $paired 1
                $facts = @{
                    full = Test-GraphMemoryFacts $fullGraph.graph @()
                    summary = Test-GraphMemoryFacts $summaryGraph.graph @()
                    paired = Test-GraphMemoryFacts $pairedGraph.graph @('ResourceExport')
                }
                if ($null -ne $reference -and ($facts.full.logicalBytes -ne $reference.logicalBytes -or
                    $facts.full.resources -ne $reference.resources)) {
                    throw 'The same graph reported different logical sizes across backends.'
                }
                $reference = $facts.full
                Write-VerificationJson "$directory/facts.json" $facts
                $cases += @{ configuration = $config; backend = $backend; passed = $true; facts = $facts
                    graphs = @($fullGraph.path, $summaryGraph.path, $pairedGraph.path); directory = $directory }
            } finally {
                if ($start) {
                    $null = Stop-VerificationSession $sessionId -StartedSession $start `
                        -ReceiptPath "$directory/stop.json" -RequireCleanExit
                }
            }
            $log = Get-Content -LiteralPath $start.outputLog -Raw
            if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') {
                throw "Native validation errors: $($start.outputLog)"
            }
        }
    }
    $result = @{ ok = $true; directory = $output; cases = $cases }
} catch { $result = @{ ok = $false; directory = $output; cases = $cases; error = $_.Exception.Message } }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
