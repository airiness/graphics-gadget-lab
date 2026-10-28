<#
.SYNOPSIS
Checks that text normalization preserves byte-pinned shader and capture fixtures.
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
    @{ path='Sources/example.cpp'; before="int value;`n"; after="int value;`r`n" }
)
try {
    foreach ($case in $cases) {
        $path = Join-Path $fixtureRoot $case.path
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllBytes($path, $utf8.GetBytes($case.before))
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'Scripts/SourcesEndingToCRLF.ps1') -Root $fixtureRoot | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Source normalization failed' }
    foreach ($case in $cases) {
        $actual = [IO.File]::ReadAllBytes((Join-Path $fixtureRoot $case.path))
        $expected = $utf8.GetBytes($case.after)
        if ([Convert]::ToBase64String($actual) -ne [Convert]::ToBase64String($expected)) {
            throw "Unexpected normalized bytes: $($case.path)"
        }
    }
    $result = @{ ok=$true; byteComparisons=$cases.Count }
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
