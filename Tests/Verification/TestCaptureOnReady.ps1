<#
.SYNOPSIS
Checks hidden startup capture admission, bundle outcomes and timeout exit codes.
Busy/rejection and publication arbitration are also covered by app-frame-capture
headless contracts; this suite exercises native success, missing view and timeout.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/CaptureOnReady' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$checks = @()
$cases = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Quote-Argument([string]$Value) {
    return '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            foreach ($mode in @('scene', 'composited', 'missing-view', 'timeout')) {
                $directory = Join-Path $output "$config-$backend-$mode"
                $null = New-Item -ItemType Directory -Path $directory
                $logPath = Join-Path $directory 'app.log'
                $captureDirectory = Join-Path $directory 'output'
                $arguments = @('--hidden', '--no-devtools', '--rhi', $backend,
                    '--lab', 'gglab.lab.temporal_aa', '--window-size', '320x180',
                    '--state-root', "$output/$config-$backend-state", '--capture-on-ready', $captureDirectory,
                    '--capture-settle-frames', $(if ($mode -eq 'timeout') { '10000' } else { '2' }),
                    '--capture-label', 'startup-job',
                    '--output-log', $logPath)
                if ($mode -eq 'scene' -or $mode -eq 'composited') {
                    $arguments += @('--capture-source', $mode)
                } elseif ($mode -eq 'missing-view') {
                    $arguments += @('--capture-view', 'Missing.CaptureOnReady.View')
                } else {
                    $arguments += @('--capture-timeout', '0.05')
                }
                $executable = Join-Path $root "Build/Output/x64/$config/GraphicsGadgetLab.exe"
                $process = Start-Process -FilePath $executable -ArgumentList ($arguments | ForEach-Object { Quote-Argument $_ }) `
                    -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Hidden -PassThru
                try {
                    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                        $process.Kill()
                        $process.WaitForExit()
                        throw "Startup watchdog expired: $directory"
                    }
                    $exitCode = $process.ExitCode
                } finally { $process.Dispose() }
                $log = Get-Content -LiteralPath $logPath -Raw -Encoding UTF8
                $fields = @{}
                foreach ($line in $log -split '\r?\n') {
                    if ($line -match '^(capture-[a-z-]+): (.*)$') { $fields[$Matches[1]] = $Matches[2] }
                }
                Check ($fields['capture-admission'] -eq 'accepted' -and [uint64]$fields['capture-job'] -gt 0) "$config-$backend-$mode-accepted"
                if ($mode -eq 'scene' -or $mode -eq 'composited') {
                    Check ($exitCode -eq 0 -and $fields['capture-status'] -eq 'completed' -and
                        $fields['capture-publication'] -eq 'Published' -and
                        (Test-Path -LiteralPath $fields['capture-manifest']) -and
                        (Test-Path -LiteralPath $fields['capture-image']) -and
                        (Test-Path -LiteralPath $fields['capture-metadata'])) "$config-$backend-$mode-published"
                    $manifest = Get-Content -LiteralPath $fields['capture-manifest'] -Raw -Encoding UTF8 | ConvertFrom-Json
                    $sidecar = Get-Content -LiteralPath $fields['capture-metadata'] -Raw -Encoding UTF8 | ConvertFrom-Json
                    Check ($manifest.schema -eq 'gglab.capture-bundle' -and $manifest.schemaVersion -eq 2 -and
                        $manifest.jobId -eq [uint64]$fields['capture-job'] -and $manifest.products.Count -eq 1 -and
                        $sidecar.source -eq $mode -and $sidecar.timing.mode -eq 'after-ready' -and
                        $sidecar.timing.settleFrames -eq 2) "$config-$backend-$mode-typed-product"
                    $environment = $manifest.environment
                    Check ($environment.process.runIdentity -eq $manifest.runIdentity -and $environment.process.host -eq 'hidden' -and
                        $environment.device.backend -eq $backend -and $environment.device.adapterName -and
                        $environment.device.compatibilityIdentity -eq $manifest.submission.adapterDriverIdentity -and
                        $environment.build.configuration -eq $config -and $environment.validation.requested -and
                        $environment.validation.effective) "$config-$backend-$mode-environment"
                    Check ($log -notmatch '\[(error|critical)\]') "$config-$backend-$mode-clean-log"
                } elseif ($mode -eq 'missing-view') {
                    Check ($exitCode -eq 2 -and $fields['capture-status'] -eq 'failed' -and
                        $fields['capture-publication'] -eq 'NotPublished' -and $fields['capture-failure']) "$config-$backend-view-failure"
                } else {
                    Check ($exitCode -eq 3 -and $fields['capture-status'] -eq 'timeout' -and
                        $fields['capture-cancellation'] -eq 'Cancelled') "$config-$backend-timeout-cancellation"
                }
                if ($mode -eq 'missing-view' -or $mode -eq 'timeout') {
                    Check (@(Get-ChildItem -LiteralPath $directory -Recurse -File -Filter '*.capture-bundle.json').Count -eq 0) "$config-$backend-$mode-no-manifest"
                }
                Check ($log -notmatch 'VUID-|GPU_BASED_VALIDATION_|assertion failed|\[critical\]') "$config-$backend-$mode-no-native-fault"
                $cases += @{configuration=$config;backend=$backend;mode=$mode;exitCode=$exitCode;directory=$directory}
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
