param(
    [string]$RootDir = "",
    [string]$ManifestPath = "Assets/Media/GGLabCoastalAtrium/Baseline1/baseline.json",
    [string]$ContentRoot = "",
    [string]$ShaderArtifactRoot = "",
    [ValidateSet("dx12", "vulkan")][string]$Rhi = "dx12",
    [switch]$CheckRenderer
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($RootDir)) {
    $RootDir = Join-Path $PSScriptRoot ".."
}
$RootDir = (Resolve-Path -LiteralPath $RootDir).Path
if (-not [System.IO.Path]::IsPathRooted($ManifestPath)) {
    $ManifestPath = Join-Path $RootDir $ManifestPath
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1) {
    throw "Unsupported rendering baseline manifest version."
}

function Test-RecordedFiles {
    param([string]$BaseDirectory, [object[]]$Files, [string]$Label)

    if ($null -eq $Files -or $Files.Count -eq 0) {
        throw "No files recorded for $Label."
    }
    $root = (Resolve-Path -LiteralPath $BaseDirectory).Path.TrimEnd('\', '/')
    foreach ($file in $Files) {
        if ([string]::IsNullOrWhiteSpace($file.path) -or
            [System.IO.Path]::IsPathRooted($file.path) -or $file.sha256 -notmatch '^[0-9a-f]{64}$') {
            throw "Invalid file record: $($file.path)"
        }
        $path = [System.IO.Path]::GetFullPath((Join-Path $root $file.path))
        if (-not $path.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "File record escapes its repository: $($file.path)"
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing $Label file: $($file.path)"
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "SHA-256 mismatch in $Label file: $($file.path)"
        }
    }
    Write-Host "$Label : $($Files.Count) file hashes match."
}

Test-RecordedFiles $RootDir $manifest.content.runtimeFiles "Runtime content"
Test-RecordedFiles (Split-Path -Parent $ManifestPath) $manifest.captures "Reference captures"

if (-not [string]::IsNullOrWhiteSpace($ContentRoot)) {
    Test-RecordedFiles $ContentRoot $manifest.content.authoringFiles "Authoring content"
} else {
    Write-Host "Authoring content: skipped (supply -ContentRoot to check)."
}

if ($CheckRenderer) {
    # Documentation commits may follow the capture without changing the renderer.
    $inputs = @("Sources", "Shaders", "Projects", "PropertySheets", "Externals",
        "GraphicsGadgetLab.sln", "Directory.Build.props", "Directory.Build.targets")
    & git -C $RootDir diff --quiet $manifest.renderer.gitCommit -- $inputs
    if ($LASTEXITCODE -ne 0) {
        throw "Renderer/build inputs differ from recorded commit $($manifest.renderer.gitCommit)."
    }
    $untracked = & git -C $RootDir ls-files --others --exclude-standard -- $inputs
    if ($LASTEXITCODE -ne 0 -or $untracked) {
        throw "Renderer/build inputs contain untracked files or could not be checked."
    }
    Write-Host "Renderer/build inputs match the recorded Git revision."
} else {
    Write-Host "Renderer revision: not constrained (supply -CheckRenderer for baseline reproduction)."
}

if (-not [string]::IsNullOrWhiteSpace($ShaderArtifactRoot)) {
    $registry = @($manifest.renderer.shaderRegistries | Where-Object { $_.rhi -eq $Rhi })
    if ($registry.Count -ne 1) {
        throw "Expected one recorded shader registry for $Rhi."
    }
    Test-RecordedFiles $ShaderArtifactRoot $registry[0].files "Shader registry ($Rhi)"
}

Write-Host "PASS: $($manifest.id) recorded files are intact."
Write-Host "This checks files only; camera, live render settings, image quality and GPU timing require capture verification."
