<#
.SYNOPSIS
Checks screenshot discovery, metadata rejection and exact pixel comparison without a GPU.
#>
[CmdletBinding()]
param([string]$OutputDirectory)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$comparisonScript = Join-Path $root 'Scripts/Verification/CompareCaptures.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/CaptureComparisons' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$checks = @()
Add-Type -AssemblyName System.Drawing

function Write-Capture([string]$Directory, [string]$Name = 'capture', [int]$Red = 0,
    [string]$CapturedAt = '2026-01-01T00:00:01Z') {
    $image = Join-Path $Directory "$Name.png"
    $bitmap = New-Object Drawing.Bitmap(2, 2)
    try {
        for ($y = 0; $y -lt 2; ++$y) {
            for ($x = 0; $x -lt 2; ++$x) {
                $bitmap.SetPixel($x, $y, [Drawing.Color]::FromArgb($Red, 0, 0))
            }
        }
        $bitmap.Save($image, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }
    Write-VerificationJson -Depth 12 ([IO.Path]::ChangeExtension($image, '.json')) @{
        schemaVersion = 1; image = @{ file = "$Name.png" }; content = @{ labId = 'gglab.lab.fixture' }
        source = 'scene'; label = 'fixture'; capturedAtUtc = $CapturedAt
    }
}

function New-Case([string]$Name) {
    $directory = Join-Path $output $Name
    foreach ($arm in @('off', 'on')) {
        $path = Join-Path $directory $arm
        New-Item -ItemType Directory -Path $path | Out-Null
        Write-Capture $path
    }
    return $directory
}

function Check-Comparison([string]$Name, [string]$Reference, [string]$Candidate,
    [int]$ExitCode, [int]$Compared, [int]$Rejected, [int]$Failed = 0, [int]$Unmatched = 0,
    [switch]$RequireMatchingInputs, [string]$ExpectedInputDifference) {
    $extra = @()
    if ($RequireMatchingInputs) { $extra += '-RequireMatchingInputs' }
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -File $comparisonScript `
        -Reference $Reference -Candidate $Candidate -Threshold 0 -MaxMeanError 0 -MaxDifferingPercent 0 @extra
    $actualExit = $LASTEXITCODE
    $receipt = ($text -join "`n") | ConvertFrom-Json
    Write-VerificationJson -Depth 12 (Join-Path $output "$Name-result.json") $receipt
    $actualCompared = if ($receipt.PSObject.Properties['compared']) { $receipt.compared } else { 0 }
    $actualFailed = if ($receipt.PSObject.Properties['failed']) { $receipt.failed } else { 0 }
    if ($actualExit -ne $ExitCode -or $receipt.ok -ne ($ExitCode -eq 0) -or
        $actualCompared -ne $Compared -or @($receipt.rejected).Count -ne $Rejected -or
        $actualFailed -ne $Failed -or @($receipt.unmatched).Count -ne $Unmatched) {
        throw "Unexpected comparison result for ${Name}: $($text -join '')"
    }
    if ($ExpectedInputDifference -and ($receipt.pairs[0].inputDifferences.Count -ne 1 -or
        $receipt.pairs[0].inputDifferences[0] -cne $ExpectedInputDifference -or
        $receipt.pairs[0].PSObject.Properties['meanAbsoluteError'])) {
        throw "Expected only input difference '$ExpectedInputDifference' without pixel metrics for ${Name}."
    }
    $script:checks += $Name
}

try {
    $case = New-Case 'mixed-directory'
    foreach ($arm in @('off', 'on')) {
        $directory = Join-Path $case $arm
        Write-VerificationJson -Depth 12 (Join-Path $directory 'session.json') @{ ok = $true }
        Write-VerificationJson -Depth 12 (Join-Path $directory 'resource-export.json') @{ schemaVersion = 2 }
        Write-VerificationJson -Depth 12 (Join-Path $directory 'graph.json') @{ schemaVersion = 4 }
        [IO.File]::WriteAllText((Join-Path $directory 'unrelated.json'), '{invalid', $utf8)
    }
    Write-Capture (Join-Path $case 'off') 'older' 255 '2026-01-01T00:00:00Z'
    Check-Comparison 'mixed-directory' "$case/off" "$case/on" 0 1 0
    Check-Comparison 'single-image' "$case/off/capture.png" "$case/on/capture.png" 0 1 0

    $case = New-Case 'unsupported-sidecar'
    $sidecar = "$case/on/capture.json"
    $metadata = Get-Content -Raw $sidecar | ConvertFrom-Json
    $metadata.schemaVersion = 99
    Write-VerificationJson -Depth 12 $sidecar $metadata
    Check-Comparison 'unsupported-sidecar' "$case/off" "$case/on" 1 0 1 0 1
    Check-Comparison 'unsupported-single-image' "$case/off/capture.png" "$case/on/capture.png" 1 0 1

    $case = New-Case 'malformed-sidecar'
    [IO.File]::WriteAllText("$case/on/capture.json", '{invalid', $utf8)
    Check-Comparison 'malformed-sidecar' "$case/off" "$case/on" 1 0 1 0 1

    $case = New-Case 'missing-sidecar'
    Remove-Item -LiteralPath "$case/on/capture.json"
    Check-Comparison 'missing-sidecar' "$case/off" "$case/on" 1 0 1 0 1
    Check-Comparison 'single-image-optional-sidecar' "$case/off/capture.png" "$case/on/capture.png" 0 1 0

    $case = New-Case 'mismatched-image'
    $sidecar = "$case/on/capture.json"
    $metadata = Get-Content -Raw $sidecar | ConvertFrom-Json
    $metadata.image.file = 'other.png'
    Write-VerificationJson -Depth 12 $sidecar $metadata
    Check-Comparison 'mismatched-image' "$case/off" "$case/on" 1 0 1 0 1

    $case = New-Case 'pixel-difference'
    Write-Capture "$case/on" 'capture' 1
    Check-Comparison 'pixel-difference' "$case/off" "$case/on" 1 1 0 1

    $case = New-Case 'unmatched-key'
    $sidecar = "$case/on/capture.json"
    $metadata = Get-Content -Raw $sidecar | ConvertFrom-Json
    $metadata.label = 'unmatched'
    Write-VerificationJson -Depth 12 $sidecar $metadata
    Check-Comparison 'unmatched-key' "$case/off" "$case/on" 1 0 0 0 2

    $case = New-Case 'input-identity'
    Check-Comparison 'missing-recorded-inputs' "$case/off" "$case/on" 1 1 0 1 -RequireMatchingInputs
    foreach ($arm in @('off', 'on')) {
        $path = "$case/$arm/capture.json"
        $metadata = Get-Content -Raw $path | ConvertFrom-Json
        $metadata.content | Add-Member -NotePropertyName assetBundle -NotePropertyValue ([pscustomobject]@{ id = 'gglab.fixture'; revision = 'same' })
        $metadata | Add-Member -NotePropertyName sceneInputs -NotePropertyValue ([pscustomobject]@{
            schemaVersion = 1; environment = [pscustomobject]@{ activeIBLGeneration = 1 }
            exposure = [pscustomobject]@{ manualEV100 = 15 }
        })
        $metadata | Add-Member -NotePropertyName temporal -NotePropertyValue ([pscustomobject]@{ requested = $true; status = 'active' })
        Write-VerificationJson -Depth 12 $path $metadata
    }
    Check-Comparison 'matching-recorded-inputs' "$case/off" "$case/on" 0 1 0 -RequireMatchingInputs
    $path = "$case/on/capture.json"
    $metadata = Get-Content -Raw $path | ConvertFrom-Json
    $metadata.content.assetBundle.revision = 'different'
    Write-VerificationJson -Depth 12 $path $metadata
    Check-Comparison 'different-asset-inputs' "$case/off" "$case/on" 1 1 0 1 -RequireMatchingInputs
    $metadata.content.assetBundle.revision = 'same'
    $metadata.sceneInputs.exposure.manualEV100 = 14.99999
    Write-VerificationJson -Depth 12 $path $metadata
    Check-Comparison 'different-lighting-inputs' "$case/off" "$case/on" 1 1 0 1 -RequireMatchingInputs
    $metadata.sceneInputs.exposure.manualEV100 = 15
    $metadata.temporal.status = 'disabled'
    Write-VerificationJson -Depth 12 $path $metadata
    Check-Comparison 'different-effective-temporal-inputs' "$case/off" "$case/on" 1 1 0 1 -RequireMatchingInputs

    foreach ($arm in @('off', 'on')) {
        $path = "$case/$arm/capture.json"
        $metadata = Get-Content -Raw $path | ConvertFrom-Json
        $metadata.temporal.status = 'active'
        $metadata.source = 'diagnostic'
        $metadata | Add-Member -NotePropertyName diagnosticTap -NotePropertyValue 'temporal-motion-direction'
        Write-VerificationJson -Depth 12 $path $metadata
    }
    Check-Comparison 'matching-diagnostic-inputs' "$case/off" "$case/on" 0 1 0 -RequireMatchingInputs
    $path = "$case/on/capture.json"
    $metadata = Get-Content -Raw $path | ConvertFrom-Json
    $metadata.diagnosticTap = 'temporal-rejection'
    Write-VerificationJson -Depth 12 $path $metadata
    Check-Comparison 'different-diagnostic-inputs' "$case/off" "$case/on" 1 1 0 1 `
        -RequireMatchingInputs -ExpectedInputDifference 'diagnosticTap'
    Check-Comparison 'different-diagnostic-inputs-without-strict-matching' "$case/off" "$case/on" 0 1 0
    $metadata.PSObject.Properties.Remove('diagnosticTap')
    Write-VerificationJson -Depth 12 $path $metadata
    Check-Comparison 'missing-diagnostic-input' "$case/off" "$case/on" 1 1 0 1 `
        -RequireMatchingInputs -ExpectedInputDifference 'diagnosticTap'

    $result = @{ ok = $true; checks = $checks; directory = $output }
    Write-VerificationJson -Depth 12 (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress))
} catch {
    $result = @{ ok = $false; checks = $checks; directory = $output; error = $_.Exception.Message }
    Write-VerificationJson -Depth 12 (Join-Path $output 'result.json') $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress))
    exit 1
}
