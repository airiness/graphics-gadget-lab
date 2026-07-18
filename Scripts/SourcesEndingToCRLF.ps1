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
    ".natvis", ".bat", ".cmd", ".ps1", ".json"
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

function Join-ByteArrays {
    param(
        [byte[]]$First,
        [byte[]]$Second
    )

    $result = New-Object byte[] ($First.Length + $Second.Length)
    [System.Buffer]::BlockCopy($First, 0, $result, 0, $First.Length)
    [System.Buffer]::BlockCopy($Second, 0, $result, $First.Length, $Second.Length)
    return $result
}

function Convert-BytesToUtf8CRLF {
    param(
        [byte[]]$Bytes,
        [string]$Path
    )

    if ($Bytes.Length -eq 0) {
        return $Bytes
    }

    $emitUtf8Bom = $false

    try {
        # Preserve an existing UTF-8 BOM.
        if ($Bytes.Length -ge 3 -and
            $Bytes[0] -eq 0xEF -and
            $Bytes[1] -eq 0xBB -and
            $Bytes[2] -eq 0xBF) {
            $encoding = New-Object System.Text.UTF8Encoding($false, $true)
            $text = $encoding.GetString($Bytes, 3, $Bytes.Length - 3)
            $emitUtf8Bom = $true
        }
        # UTF-16 LE/BE can be identified safely by its BOM. Convert it to
        # UTF-8 with BOM so the encoding remains explicit to Windows tools.
        elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) {
            $encoding = New-Object System.Text.UnicodeEncoding($false, $true, $true)
            $text = $encoding.GetString($Bytes, 2, $Bytes.Length - 2)
            $emitUtf8Bom = $true
        }
        elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) {
            $encoding = New-Object System.Text.UnicodeEncoding($true, $true, $true)
            $text = $encoding.GetString($Bytes, 2, $Bytes.Length - 2)
            $emitUtf8Bom = $true
        }
        else {
            # Strict decoding prevents legacy ANSI files from being silently
            # interpreted with the wrong code page and corrupted on rewrite.
            $encoding = New-Object System.Text.UTF8Encoding($false, $true)
            $text = $encoding.GetString($Bytes)
        }
    }
    catch [System.Text.DecoderFallbackException] {
        throw "File is not valid UTF-8 or BOM-marked UTF-16 and cannot be converted safely: $Path"
    }

    $text = $text -replace "`r`n|`n|`r", "`r`n"
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    $utf8Bytes = $utf8.GetBytes($text)

    if ($emitUtf8Bom) {
        $utf8WithBom = New-Object System.Text.UTF8Encoding($true, $true)
        return Join-ByteArrays ($utf8WithBom.GetPreamble()) $utf8Bytes
    }

    return $utf8Bytes
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
    Write-Host "Normalize UTF-8/CRLF"
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
    $newBytes = Convert-BytesToUtf8CRLF $oldBytes $path

    if (-not (Test-BytesEqual $oldBytes $newBytes)) {
        $changed++

        if ($DryRun) {
            Write-Host "[Would Normalize] $path"
        }
        else {
            [System.IO.File]::WriteAllBytes($path, $newBytes)
            Write-Host "[Normalized] $path"
        }
    }
}

Write-Host ""
Write-Host "Scanned : $total"
Write-Host "Changed : $changed"

if ($DryRun) {
    Write-Host "DryRun only. No file was modified."
}
