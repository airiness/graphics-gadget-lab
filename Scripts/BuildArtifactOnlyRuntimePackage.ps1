[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("dx12", "vulkan")]
    [string]$Backend = "vulkan",

    [ValidateRange(5, 120)]
    [int]$SmokeTimeoutSeconds = 30,

    [switch]$RunSmoke
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-Succeeded {
    param([string]$Operation)

    if ($LASTEXITCODE -ne 0) {
        throw "$Operation failed with exit code $LASTEXITCODE."
    }
}

function Remove-ContainedDirectory {
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$AllowedRoot
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    $fullAllowedRoot = [System.IO.Path]::GetFullPath($AllowedRoot).TrimEnd('\')
    if (!$fullPath.StartsWith($fullAllowedRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside '$fullAllowedRoot': '$fullPath'."
    }
    if (Test-Path -LiteralPath $fullPath) {
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}

function Get-MSBuildPath {
    $knownPath =
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    if (Test-Path -LiteralPath $knownPath -PathType Leaf) {
        return $knownPath
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
            -property installationPath
        if ($installationPath) {
            $candidate = Join-Path $installationPath "MSBuild\Current\Bin\MSBuild.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }
    throw "Visual Studio MSBuild was not found."
}

function Assert-ArtifactOnlyPackage {
    param(
        [Parameter(Mandatory)] [string]$PackageRoot,
        [Parameter(Mandatory)] [string]$IntermediateRoot
    )

    $requiredPaths = @(
        (Join-Path $PackageRoot "GraphicsGadgetLab.exe"),
        (Join-Path $PackageRoot "Assets"),
        (Join-Path $PackageRoot "ShaderArtifacts\active\program-registry.ggsh.active")
    )
    foreach ($requiredPath in $requiredPaths) {
        if (!(Test-Path -LiteralPath $requiredPath)) {
            throw "Artifact-only package is missing '$requiredPath'."
        }
    }

    $forbiddenRootDirectories = @("Shaders", "ShaderCache", "ShaderToolchain")
    foreach ($directoryName in $forbiddenRootDirectories) {
        $candidate = Join-Path $PackageRoot $directoryName
        if (Test-Path -LiteralPath $candidate) {
            throw "Artifact-only package contains forbidden directory '$candidate'."
        }
    }

    $forbiddenFileNames = @(
        "dxcompiler.dll",
        "dxil.dll",
        "gglab-shaderc.exe",
        "ShaderToolchainCore.lib"
    )
    $forbiddenFiles = Get-ChildItem -LiteralPath $PackageRoot -Recurse -File | Where-Object {
        $_.Extension -in @(".hlsl", ".hlsli") -or $_.Name -in $forbiddenFileNames
    }
    if ($forbiddenFiles) {
        throw "Artifact-only package contains forbidden payload: $($forbiddenFiles.FullName -join ', ')."
    }

    $tlogRoot = Join-Path $IntermediateRoot "WinApp.tlog"
    $compileCommand = Get-Content -LiteralPath (Join-Path $tlogRoot "CL.command.1.tlog") -Raw
    if ($compileCommand -match "Sources\\ShaderToolchain" -or
        $compileCommand -match "Microsoft\.Direct3D\.DXC") {
        throw "Artifact-only compile command still exposes ShaderToolchain or DXC include paths."
    }
    $linkCommand = Get-Content -LiteralPath (Join-Path $tlogRoot "link.command.1.tlog") -Raw
    if ($linkCommand -match "ShaderToolchainCore\.lib" -or
        $linkCommand -match "dxcompiler\.lib") {
        throw "Artifact-only link command still consumes ShaderToolchainCore or DXC."
    }
}

function Invoke-PackageSmoke {
    param(
        [Parameter(Mandatory)] [string]$PackageRoot,
        [Parameter(Mandatory)] [string]$SelectedBackend,
        [Parameter(Mandatory)] [int]$TimeoutSeconds
    )

    $executable = Join-Path $PackageRoot "GraphicsGadgetLab.exe"
    $stdoutPath = Join-Path $PackageRoot "artifact-only-smoke.stdout.log"
    $stderrPath = Join-Path $PackageRoot "artifact-only-smoke.stderr.log"
    $arguments = @(
        "--rhi", $SelectedBackend,
        "--lab", "gglab.lab.culling",
        "--absolute-mouse",
        "--no-devtools"
    )
    $process = Start-Process -FilePath $executable -WorkingDirectory $PackageRoot `
        -ArgumentList $arguments -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath -WindowStyle Hidden -PassThru
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        $succeeded = $false
        while ([DateTime]::UtcNow -lt $deadline) {
            if ($process.HasExited) {
                throw "Packaged runtime exited before the smoke contract completed (exit=$($process.ExitCode))."
            }
            $output = if (Test-Path -LiteralPath $stdoutPath) {
                Get-Content -LiteralPath $stdoutPath -Raw
            } else {
                ""
            }
            $hasArtifactStartup =
                $output -match "Artifact-only shader startup selected the packaged active registry"
            $hasPreload = $output -match "Async artifact preload published 30 shaders"
            $hasBackendFrame = if ($SelectedBackend -eq "vulkan") {
                $output -match "Vulkan completed its first production submit/present frame transaction"
            } else {
                $output -match "Activated lab 'gglab\.lab\.culling'"
            }
            if ($hasArtifactStartup -and $hasPreload -and $hasBackendFrame) {
                $succeeded = $true
                break
            }
            Start-Sleep -Milliseconds 250
            $process.Refresh()
        }
        if (!$succeeded) {
            throw "Packaged $SelectedBackend runtime did not satisfy the startup contract within $TimeoutSeconds seconds."
        }
    }
    finally {
        $process.Refresh()
        if (!$process.HasExited) {
            Stop-Process -Id $process.Id
            Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
        }
    }

    $combinedDiagnostics =
        (Get-Content -LiteralPath $stdoutPath -Raw) + "`n" +
        (Get-Content -LiteralPath $stderrPath -Raw)
    if ($combinedDiagnostics -match "(?im)\[error\]|assertion failed|failed to prepare development shader artifacts") {
        throw "Packaged runtime smoke reported an error. Inspect '$stdoutPath' and '$stderrPath'."
    }
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$artifactOnlyRoot = Join-Path $repositoryRoot "Build\ArtifactOnly"
$packageBaseRoot = Join-Path $repositoryRoot "Build\Packages\ArtifactOnly"
$packageRoot = Join-Path $packageBaseRoot "$Configuration\$Backend"
$artifactOutputRoot = Join-Path $artifactOnlyRoot "Output\x64\$Configuration"
$artifactIntermediateRoot =
    Join-Path $artifactOnlyRoot "Intermediate\WinApp\x64\$Configuration"
$shaderCacheRoot = Join-Path $artifactOnlyRoot "ShaderCache\$Configuration\$Backend"
$shaderArtifactRoot = Join-Path $packageRoot "ShaderArtifacts"
$msbuild = Get-MSBuildPath
$shaderTarget = if ($Backend -eq "dx12") { "gglab-dx12" } else { "gglab-vulkan13" }

Remove-ContainedDirectory -Path $packageRoot -AllowedRoot $packageBaseRoot
Remove-ContainedDirectory -Path $artifactOutputRoot -AllowedRoot $artifactOnlyRoot
Remove-ContainedDirectory -Path $artifactIntermediateRoot -AllowedRoot $artifactOnlyRoot
New-Item -ItemType Directory -Path $packageRoot, $shaderArtifactRoot, $shaderCacheRoot -Force |
    Out-Null

& $msbuild (Join-Path $repositoryRoot "Projects\ShaderCompiler\ShaderCompiler.vcxproj") /m `
    /p:Configuration=$Configuration /p:Platform=x64
Assert-Succeeded "Shader compiler build"

$developmentOutputRoot = Join-Path $repositoryRoot "Build\Output\x64\$Configuration"
$shaderCompiler = Join-Path $developmentOutputRoot "gglab-shaderc.exe"
& $shaderCompiler build-runtime --source-root (Join-Path $repositoryRoot "Shaders") `
    --target $shaderTarget --cache-root $shaderCacheRoot --artifact-root $shaderArtifactRoot `
    --result-format json
Assert-Succeeded "$shaderTarget artifact publication"

& $msbuild (Join-Path $repositoryRoot "Projects\WinApp\WinApp.vcxproj") /m `
    /p:Configuration=$Configuration /p:Platform=x64 /p:GGLAB_ARTIFACT_ONLY_RUNTIME=1
Assert-Succeeded "Artifact-only WinApp build"

Copy-Item -LiteralPath (Join-Path $artifactOutputRoot "GraphicsGadgetLab.exe") `
    -Destination $packageRoot
Get-ChildItem -LiteralPath $artifactOutputRoot -Filter *.dll -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $packageRoot
}
$d3d12RuntimeRoot = Join-Path $artifactOutputRoot "D3D12"
if (Test-Path -LiteralPath $d3d12RuntimeRoot) {
    Copy-Item -LiteralPath $d3d12RuntimeRoot -Destination $packageRoot -Recurse
}
Copy-Item -LiteralPath (Join-Path $repositoryRoot "Assets") -Destination $packageRoot -Recurse
New-Item -ItemType Directory -Path (Join-Path $packageRoot "DerivedDataCache\IBL"), `
    (Join-Path $packageRoot "DerivedDataCache\Texture") -Force | Out-Null

$manifest = [ordered]@{
    schemaVersion = 1
    packageKind = "gglab.artifact-only-runtime"
    configuration = $Configuration
    backend = $Backend
    shaderTarget = $shaderTarget
    executable = "GraphicsGadgetLab.exe"
    shaderArtifactRoot = "ShaderArtifacts"
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath `
    (Join-Path $packageRoot "artifact-only-package.json") -Encoding utf8

Assert-ArtifactOnlyPackage -PackageRoot $packageRoot `
    -IntermediateRoot $artifactIntermediateRoot
if ($RunSmoke) {
    Invoke-PackageSmoke -PackageRoot $packageRoot -SelectedBackend $Backend `
        -TimeoutSeconds $SmokeTimeoutSeconds
}

Write-Host "Artifact-only $Backend package validated: $packageRoot"
