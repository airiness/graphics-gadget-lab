<#
.SYNOPSIS
Checks source normalization, excluded trees, byte-pinned fixtures and no-op runs.
#>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$fixtureRoot = Join-Path $tempRoot ('gglab-normalization-' + [Guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($false)
$cases = @(
    @{ path='Shaders/Tests/Generated/fixture.hlsli'; before="float value;`n"; after="float value;`n" },
    @{ path='Shaders/Tests/Other/fixture.hlsli'; before="float value;`n"; after="float value;`r`n" },
    @{ path='Assets/Media/GGLabCoastalRetreat/GI/Daylight/dx12/output.txt'; before="native`r`nready`n"; after="native`r`nready`n" },
    @{ path='Assets/Media/GGLabCoastalRetreat/GI/Daylight/vulkan/view.capture-bundle.json'; before="{}`n"; after="{}`n" },
    @{ path='Assets/Media/GGLabCoastalRetreat/GI/Daylight/vulkan/view.p0.json'; before="{}`n"; after="{}`r`n" },
    @{ path='Assets/Media/GGLabCoastalRetreat/GI/Daylight/README.md'; before="# Baseline`n"; after="# Baseline`r`n" },
    @{ path='Assets/Media/Other/output.txt'; before="other`n"; after="other`r`n" },
    @{ path='Sources/example.cpp'; before="int value;`n"; after="int value;`r`n" },
    @{ path='Build/Validation/deep/cache.cpp'; before="build cache`n"; after="build cache`n" },
    @{ path='Sources/bUiLd/deep/cache.json'; before="{}`n"; after="{}`n" },
    @{ path='Externals/Vender/source.cpp'; before="vendor source`n"; after="vendor source`n" },
    @{ path='packages/deep/source.h'; before="package source`n"; after="package source`n" }
)
try {
    foreach ($case in $cases) {
        $path = Join-Path $fixtureRoot $case.path
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllBytes($path, $utf8.GetBytes($case.before))
    }
    $normalizer = Join-Path $root 'Scripts/SourcesEndingToCRLF.ps1'
    & powershell -NoProfile -ExecutionPolicy Bypass -File $normalizer -Root $fixtureRoot -DryRun | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Source normalization DryRun failed' }
    foreach ($case in $cases) {
        $actual = [IO.File]::ReadAllBytes((Join-Path $fixtureRoot $case.path))
        if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($utf8.GetBytes($case.before))) {
            throw "DryRun modified bytes: $($case.path)"
        }
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $normalizer -Root $fixtureRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Source normalization failed' }
    foreach ($case in $cases) {
        $actual = [IO.File]::ReadAllBytes((Join-Path $fixtureRoot $case.path))
        $expected = $utf8.GetBytes($case.after)
        if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($expected)) {
            throw "Unexpected normalized bytes: $($case.path)"
        }
    }
    $normalizedTimes = @{}
    foreach ($case in $cases) {
        $normalizedTimes[$case.path] = [IO.File]::GetLastWriteTimeUtc((Join-Path $fixtureRoot $case.path))
    }
    $unchanged = @(& powershell -NoProfile -ExecutionPolicy Bypass -File $normalizer -Root $fixtureRoot)
    if ($LASTEXITCODE -ne 0 -or ($unchanged -join "`n") -notmatch 'Changed\s*:\s*0') {
        throw 'A second normalization pass was not a no-op'
    }
    foreach ($case in $cases) {
        if ([IO.File]::GetLastWriteTimeUtc((Join-Path $fixtureRoot $case.path)) -ne $normalizedTimes[$case.path]) {
            throw "Unchanged file was rewritten: $($case.path)"
        }
    }
    $result = @{ ok=$true; byteComparisons=$cases.Count; dryRunByteComparisons=$cases.Count; idempotent=$true }
}
catch {
    $result = @{ ok=$false; error=$_.Exception.Message }
}
finally {
    $cleanupPath = [IO.Path]::GetFullPath($fixtureRoot)
    if (-not $cleanupPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($cleanupPath).StartsWith('gglab-normalization-')) {
        throw "Refusing cleanup outside the test temporary directory: $cleanupPath"
    }
    if (Test-Path -LiteralPath $cleanupPath) {
        Remove-Item -LiteralPath $cleanupPath -Recurse -Force
    }
}
$json = $result | ConvertTo-Json -Compress
if ($OutputDirectory) {
    $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'result.json'), $json, $utf8)
}
[Console]::Out.WriteLine($json)
if (-not $result.ok) { exit 1 }
