param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$TargetExtensions = @(
    ".c", ".cc", ".cpp", ".cxx",
    ".h", ".hh", ".hpp", ".hxx",
    ".inl", ".ixx",
    ".hlsl", ".hlsli",
    ".sln", ".vcxproj", ".filters", ".props", ".targets",
    ".natvis", ".bat", ".cmd", ".ps1"
)

$ExcludeDirs = @(
    ".git",
    ".vs",
    ".vscode",
    "Externals",
    "packages",
    "Build",
    "Binaries",
    "Intermediate",
    "Saved",
    "DerivedDataCache",
    "x64",
    "Debug",
    "Release",
    "out"
)

function Test-IsExcludedPath {
    param([string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)

    foreach ($dir in $ExcludeDirs) {
        $pattern = "[\\/]" + [regex]::Escape($dir) + "[\\/]"
        if ($fullPath -match $pattern) {
            return $true
        }
    }

    return $false
}

function Convert-BytesToCRLF {
    param([byte[]]$Bytes)

    if ($Bytes.Length -eq 0) {
        return $Bytes
    }

    # UTF-16 LE BOM
    if ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) {
        $encoding = [System.Text.Encoding]::Unicode
        $text = $encoding.GetString($Bytes)
        $text = $text -replace "`r`n|`n|`r", "`r`n"
        return $encoding.GetBytes($text)
    }

    # UTF-16 BE BOM
    if ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) {
        $encoding = [System.Text.Encoding]::BigEndianUnicode
        $text = $encoding.GetString($Bytes)
        $text = $text -replace "`r`n|`n|`r", "`r`n"
        return $encoding.GetBytes($text)
    }

    $out = New-Object System.Collections.Generic.List[byte]

    for ($i = 0; $i -lt $Bytes.Length; $i++) {
        $b = $Bytes[$i]

        if ($b -eq 0x0D) {
            $out.Add(0x0D)
            $out.Add(0x0A)

            if ($i + 1 -lt $Bytes.Length -and $Bytes[$i + 1] -eq 0x0A) {
                $i++
            }
        }
        elseif ($b -eq 0x0A) {
            # LF -> CRLF
            $out.Add(0x0D)
            $out.Add(0x0A)
        }
        else {
            $out.Add($b)
        }
    }

    return $out.ToArray()
}

function Test-BytesEqual {
    param(
        [byte[]]$A,
        [byte[]]$B
    )

    if ($A.Length -ne $B.Length) {
        return $false
    }

    for ($i = 0; $i -lt $A.Length; $i++) {
        if ($A[$i] -ne $B[$i]) {
            return $false
        }
    }

    return $true
}

$Root = [System.IO.Path]::GetFullPath($Root)

Write-Host "Root: $Root"
Write-Host "Mode: " -NoNewline
if ($DryRun) {
    Write-Host "DryRun"
}
else {
    Write-Host "Convert"
}

$files = Get-ChildItem -Path $Root -Recurse -File |
    Where-Object {
        $TargetExtensions -contains $_.Extension.ToLowerInvariant() -and
        -not (Test-IsExcludedPath $_.FullName)
    }

$total = 0
$changed = 0

foreach ($file in $files) {
    $total++

    $path = $file.FullName
    $oldBytes = [System.IO.File]::ReadAllBytes($path)
    $newBytes = Convert-BytesToCRLF $oldBytes

    if (-not (Test-BytesEqual $oldBytes $newBytes)) {
        $changed++

        if ($DryRun) {
            Write-Host "[Would Convert] $path"
        }
        else {
            [System.IO.File]::WriteAllBytes($path, $newBytes)
            Write-Host "[Converted] $path"
        }
    }
}

Write-Host ""
Write-Host "Scanned : $total"
Write-Host "Changed : $changed"

if ($DryRun) {
    Write-Host "DryRun only. No file was modified."
}
