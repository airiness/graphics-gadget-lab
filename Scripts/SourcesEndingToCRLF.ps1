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
    ".natvis", ".bat", ".cmd", ".ps1", ".json",
    ".md", ".txt", ".xml", ".yml", ".yaml", ".toml", ".ini", ".config"
)

$TargetFileNames = @(
    ".editorconfig",
    ".gitattributes",
    ".gitignore"
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

function Test-IsBytePinnedFixture {
    param(
        [string]$Path,
        [string]$FixtureDirectory,
        [string]$NativeGICaptureDirectory
    )

    # Imported ShaderGraphCore emissions are byte-pinned fixtures. Their
    # generatedSourceIdentity is the SHA-256 of the exact LF bytes, so the
    # repository-wide CRLF normalizer must preserve them verbatim.
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $directory = [System.IO.Path]::GetDirectoryName($fullPath)

    $isGeneratedShader = [System.IO.Path]::GetExtension($fullPath).Equals(
        ".hlsli",
        [System.StringComparison]::OrdinalIgnoreCase
    ) -and [string]::Equals(
        $directory,
        $FixtureDirectory,
        [System.StringComparison]::OrdinalIgnoreCase
    )

    # Native capture logs and bundle manifests retain their recorded mixed/LF bytes.
    # Sidecars, indexes and authored documentation keep the normal text policy.
    $captureParent = [System.IO.Path]::GetDirectoryName($directory)
    $name = [System.IO.Path]::GetFileName($fullPath)
    $isNativeCapture = [string]::Equals(
        $captureParent, $NativeGICaptureDirectory,
        [System.StringComparison]::OrdinalIgnoreCase
    ) -and ($name.Equals('output.txt', [System.StringComparison]::OrdinalIgnoreCase) -or
        $name.EndsWith('.capture-bundle.json', [System.StringComparison]::OrdinalIgnoreCase))

    return $isGeneratedShader -or $isNativeCapture
}

function Convert-BytesToUtf8WithoutBomCRLF {
    param(
        [byte[]]$Bytes,
        [string]$Path
    )

    if ($Bytes.Length -eq 0) {
        $text = ""
    }
    else {
        try {
            # Accept a legacy UTF-8 BOM as input, but never retain it in the
            # canonical output.
            if ($Bytes.Length -ge 3 -and
                $Bytes[0] -eq 0xEF -and
                $Bytes[1] -eq 0xBB -and
                $Bytes[2] -eq 0xBF) {
                $encoding = New-Object System.Text.UTF8Encoding($false, $true)
                $text = $encoding.GetString($Bytes, 3, $Bytes.Length - 3)
            }
            # UTF-16 LE/BE can be identified safely by its BOM. Convert it to
            # canonical UTF-8 without BOM.
            elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) {
                $encoding = New-Object System.Text.UnicodeEncoding($false, $true, $true)
                $text = $encoding.GetString($Bytes, 2, $Bytes.Length - 2)
            }
            elseif ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) {
                $encoding = New-Object System.Text.UnicodeEncoding($true, $true, $true)
                $text = $encoding.GetString($Bytes, 2, $Bytes.Length - 2)
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
    }

    $text = $text -replace "`r`n|`n|`r", "`r`n"
    $utf8 = New-Object System.Text.UTF8Encoding($false, $true)
    return $utf8.GetBytes($text)
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
$bytePinnedGeneratedFixtureDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $Root "Shaders\Tests\Generated")
)
$bytePinnedNativeGICaptureDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $Root "Assets\Media\GGLabCoastalRetreat\GI\Daylight")
)

Write-Host "Root: $Root"
Write-Host "Mode: " -NoNewline
if ($DryRun) {
    Write-Host "DryRun"
}
else {
    Write-Host "Normalize UTF-8 without BOM/CRLF"
}

# Prune before recursion: ignored build/cache trees can contain hundreds of
# thousands of entries even when no source file needs normalization.
$directories = New-Object 'System.Collections.Generic.Stack[string]'
$directories.Push($Root)
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
while ($directories.Count -gt 0) {
    $directory = $directories.Pop()
    foreach ($entry in Get-ChildItem -LiteralPath $directory) {
        if ($entry.PSIsContainer) {
            # Keep traversal within ordinary directories; do not follow directory links.
            if ($ExcludeDirs -notcontains $entry.Name -and
                ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                $directories.Push($entry.FullName)
            }
            continue
        }

        if (($TargetExtensions -contains $entry.Extension.ToLowerInvariant() -or
                $TargetFileNames -contains $entry.Name.ToLowerInvariant()) -and
            -not (Test-IsBytePinnedFixture $entry.FullName $bytePinnedGeneratedFixtureDirectory `
                $bytePinnedNativeGICaptureDirectory)) {
            $files.Add($entry)
        }
    }
}

$total = 0
$changed = 0

foreach ($file in $files) {
    $total++

    $path = $file.FullName
    $oldBytes = [System.IO.File]::ReadAllBytes($path)
    $newBytes = Convert-BytesToUtf8WithoutBomCRLF $oldBytes $path

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
