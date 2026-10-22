<#
.SYNOPSIS
Lists and runs verification contracts or hidden GPU regressions.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -List
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Group gpu -Configuration Develop -Rhi dx12,vulkan
#>
[CmdletBinding()]
param(
    [ValidateSet('contracts', 'gpu', 'all')][string]$Group = 'contracts',
    [string[]]$Suite,
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [switch]$List,
    [string]$OutputDirectory,
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$catalog = @(
    @{ name = 'coastal-assets'; group = 'contracts'; steps = @(
        @{ path = 'Scripts/Verification/ValidateAssetBundle.py'; arguments = @('--self-test') },
        @{ path = 'Scripts/Verification/ValidateAssetBundle.py'; arguments = @() }) },
    @{ name = 'verification-support'; group = 'contracts'; steps = @(@{ path = 'Tests/Verification/TestVerificationSupport.ps1' }) },
    @{ name = 'nonfinite-sites'; group = 'contracts'; steps = @(
        @{ path = 'Scripts/GenerateNonFiniteSites.py'; arguments = @('--check') },
        @{ path = 'Scripts/ValidateNonFiniteSites.py'; arguments = @('--self-test') },
        @{ path = 'Scripts/ValidateNonFiniteSites.py'; arguments = @() }) },
    @{ name = 'render-graph-schema'; group = 'contracts'; steps = @(@{ path = 'Scripts/Verification/ValidateRenderGraphExports.py'; arguments = @('--self-test') }) },
    @{ name = 'resource-export-schema'; group = 'contracts'; steps = @(@{ path = 'Scripts/Verification/ValidateResourceExports.py'; arguments = @('--self-test') }) },
    @{ name = 'nonfinite-coverage-schema'; group = 'contracts'; steps = @(@{ path = 'Scripts/Verification/ValidateNonFiniteCoverage.py'; arguments = @('--self-test') }) },
    @{ name = 'capture-comparison'; group = 'contracts'; steps = @(@{ path = 'Tests/Verification/TestCompareCaptures.ps1' }) },
    @{ name = 'capture-bundle-schema'; group = 'contracts'; steps = @(@{ path = 'Scripts/Verification/ValidateCaptureBundles.py'; arguments = @('--self-test') }) },
    @{ name = 'gpu-timing-schema'; group = 'contracts'; steps = @(@{ path = 'Scripts/Verification/ValidateGpuTiming.py'; arguments = @('--self-test') }) },
    @{ name = 'session-host-parameters'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestHostParameters.ps1' }) },
    @{ name = 'session-parameters'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestSessionParameters.ps1' }) },
    @{ name = 'capture-jobs'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestCaptureJobs.ps1' }) },
    @{ name = 'capture-on-ready'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestCaptureOnReady.ps1' }) },
    @{ name = 'pass-profile-identity'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestPassProfileIdentity.ps1' }) },
    @{ name = 'graph-allocation-facts'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestGraphAllocationFacts.ps1' }) },
    @{ name = 'nonfinite-coverage'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestNonFiniteCoverage.ps1' }) },
    @{ name = 'nonfinite-generations'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestNonFiniteGenerations.ps1' }) },
    @{ name = 'nonfinite-injection'; group = 'gpu'; configurations = @('Debug', 'Develop'); steps = @(@{ path = 'Tests/Verification/TestNonFiniteInjection.ps1' }) },
    @{ name = 'temporal-aa-counters'; group = 'gpu'; configurations = @('Debug', 'Develop', 'Release'); steps = @(@{ path = 'Tests/Verification/TestTemporalAACounters.ps1' }) }
)
$output = $null
$results = @()
try {
    if ($List) {
        $items = @($catalog | ForEach-Object {
            $entry = @{ name = $_.name; group = $_.group; requiresGpu = $_.group -eq 'gpu'; configurations = @() }
            if ($_.group -eq 'gpu') { $entry.configurations = $_.configurations }
            $entry
        })
        [Console]::Out.WriteLine((@{ ok = $true; suites = $items } | ConvertTo-Json -Depth 5))
        exit 0
    }
    $Suite = @($Suite | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | Select-Object -Unique)
    $Configuration = @($Configuration | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
    $Rhi = @($Rhi | ForEach-Object { $_ -split ',' } | Select-Object -Unique)
    if (-not $Configuration.Count -or @($Configuration | Where-Object { $_ -notin @('Debug', 'Develop', 'Release') }).Count) { throw 'Configuration must be Debug, Develop or Release.' }
    if (-not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12', 'vulkan') }).Count) { throw 'Rhi must be dx12 or vulkan.' }
    if ($Suite) {
        $unknown = @($Suite | Where-Object { $_ -notin $catalog.name })
        if ($unknown.Count) { throw "Unknown suites: $($unknown -join ', '). Use -List." }
        $selected = @($catalog | Where-Object { $_.name -in $Suite })
        if ($PSBoundParameters.ContainsKey('Group') -and $Group -ne 'all' -and @($selected | Where-Object { $_.group -ne $Group }).Count) { throw 'Selected suites do not belong to the requested group.' }
    } else { $selected = @($catalog | Where-Object { $Group -eq 'all' -or $_.group -eq $Group }) }
    foreach ($item in $selected) {
        foreach ($step in $item.steps) {
            if (-not (Test-Path -LiteralPath (Join-Path $root $step.path) -PathType Leaf)) { throw "Missing verification script: $($step.path)" }
        }
        if ($item.group -eq 'gpu') {
            foreach ($config in $Configuration) {
                if ($config -notin $item.configurations) { throw "Suite '$($item.name)' does not support $config. Supported: $($item.configurations -join ', ')." }
                if (-not (Test-Path -LiteralPath (Join-Path $root "Build/Output/x64/$config/GraphicsGadgetLab.exe"))) { throw "Build $config GraphicsGadgetLab.exe before running GPU suites." }
            }
        }
    }
    if (@($selected.steps | Where-Object { $_.path.EndsWith('.py') }).Count) { $Python = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source }
    if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/Verification' }
    $output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6))
    $null = New-Item -ItemType Directory -Path $output
    foreach ($item in $selected) {
        $configs = if ($item.group -eq 'gpu') { $Configuration } else { @('contracts') }
        foreach ($config in $configs) {
            $name = if ($item.group -eq 'gpu') { "$($item.name)-$config" } else { $item.name }
            [Console]::Error.WriteLine("Running $name")
            # Keep GPU state under one run id; nested ids can exceed native publication path limits.
            $directory = Join-Path $output ('{0:D2}' -f ($results.Count + 1))
            $null = New-Item -ItemType Directory -Path $directory
            $steps = @()
            foreach ($step in $item.steps) {
                $path = Join-Path $root $step.path
                $log = Join-Path $directory (([IO.Path]::GetFileNameWithoutExtension($path)) + "-$($steps.Count).log")
                if ($path.EndsWith('.ps1')) {
                    $parameters = @{ OutputDirectory = $directory }
                    if ($item.group -eq 'gpu') {
                        $parameters = @{ RunDirectory = (Join-Path $directory 'run'); Configuration = $config; Rhi = $Rhi }
                    }
                    $execution = Invoke-VerificationScript $path $parameters
                    $text = $execution.output + $execution.error
                    $code = $execution.exitCode
                    try { $receipt = $execution.output | ConvertFrom-Json; $passed = $code -eq 0 -and $receipt.ok }
                    catch { $receipt = $null; $passed = $false }
                } else {
                    $arguments = @($path) + $step.arguments
                    $ErrorActionPreference = 'Continue'
                    try { $text = (& $Python @arguments 2>&1 | Out-String); $code = $LASTEXITCODE }
                    finally { $ErrorActionPreference = 'Stop' }
                    $receipt = $null
                    $passed = $code -eq 0
                }
                [IO.File]::WriteAllText($log, $text, (New-Object Text.UTF8Encoding($false)))
                $steps += @{ script = $step.path; exitCode = $code; passed = [bool]$passed; log = $log; receipt = $receipt }
            }
            $results += @{ name = $item.name; configuration = $config; directory = $directory; passed = @($steps | Where-Object { -not $_.passed }).Count -eq 0; steps = $steps }
        }
    }
    $result = @{ ok = @($results | Where-Object { -not $_.passed }).Count -eq 0; directory = $output; suites = $results }
} catch { $result = @{ ok = $false; directory = $output; suites = $results; error = $_.Exception.Message } }
if ($output) { Write-VerificationJson (Join-Path $output 'result.json') $result }
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 16))
if (-not $result.ok) { exit 1 }
