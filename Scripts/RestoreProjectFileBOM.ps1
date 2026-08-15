param(
    [string]$RootDir = ""
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    param([string]$InputRoot)

    if (-not [string]::IsNullOrWhiteSpace($InputRoot)) {
        $cleanRoot = $InputRoot.Trim().Trim('"')
        return (Resolve-Path -LiteralPath $cleanRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

$root = Get-RepoRoot $RootDir
$projectsDir = Join-Path $root "Projects"
if (-not (Test-Path -LiteralPath $projectsDir -PathType Container)) {
    throw "Projects directory not found: $projectsDir"
}

$files = @(Get-ChildItem -LiteralPath $projectsDir -Recurse -File |
    Where-Object { $_.Extension -in @(".vcxproj", ".filters") })

$restored = 0
foreach ($file in $files) {
    $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
    $hasBom = ($bytes.Length -ge 3 -and
        $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    if ($hasBom) {
        continue
    }

    $restoredBytes = New-Object byte[] ($bytes.Length + 3)
    $restoredBytes[0] = 0xEF
    $restoredBytes[1] = 0xBB
    $restoredBytes[2] = 0xBF
    [Array]::Copy($bytes, 0, $restoredBytes, 3, $bytes.Length)
    [System.IO.File]::WriteAllBytes($file.FullName, $restoredBytes)

    Write-Host "Restored UTF-8 BOM: $($file.FullName.Substring($root.Length + 1))"
    $restored++
}

if ($restored -eq 0) {
    Write-Host ("Project file encoding is clean: {0} vcxproj/.filters files already have a UTF-8 BOM." -f $files.Count)
}
