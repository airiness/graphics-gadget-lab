param(
    [string]$RootDir = "",
    [switch]$ShowAll
)

# Project ownership and first-party boundary validation.
# Enforces the first-party source ownership and dependency direction contracts:
#   Project graph - WinApp must reference GGLabRuntime and NapaVoxelCore;
#                   Foundation must remain Tier-0; its tests may reference only
#                   Foundation; NapaVoxelCore remains an independent sibling;
#                   Vulkan qualification owns the explicit privileged Runtime /
#                   Shader Toolchain dependency closure.
#   Source ownership - every first-party source item must live below its owning
#                      project's source root, and every physical source file must
#                      belong to exactly one owning project.
#   Include visibility - Foundation and NapaVoxelCore see only their owned
#                        Public/Private roots, while ordinary consumers receive
#                        only the corresponding Public root.
#   Include identity - public logical include paths must be unique across owners.
#   Foundation boundary - Foundation must not include any upper first-party domain.
#   Foundation private access - every private header is compiler-gated to the
#                               Foundation project even when a broad consumer
#                               include root can physically resolve its path.
#   Public header closure - Foundation and Runtime Public headers must resolve
#                           without implementation or legacy Runtime headers.
#   Foundation consumers - ShaderCompiler foundational dependencies must come
#                          from Foundation rather than Runtime Core infrastructure.
#   Ownership boundary - runtime candidates must not include Application/*,
#                        DevTools/*, or WinApp-owned platform input.
#   Platform leakage - portable runtime files must not depend on unapproved
#                      Win32 / GameInput / COM semantics.
# Current known violations are enumerated as an explicit ledger; any
# violation outside the ledger fails the validation.
#
# Physical/project ownership scanning covers all first-party source roots.
# Runtime dependency and platform scanning derives from actual GGLabRuntime
# project items and remains classification-driven.

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
$repositorySourcesDir = Join-Path $root "Sources"
$repositoryTestsDir = Join-Path $root "Tests"
$winAppSourcesDir = Join-Path $root "Sources/WinApp"
$vulkanQualificationSourcesDir = Join-Path $root "Sources/GGLabVulkanQualification"
$appRuntimeSourcesDir = Join-Path $root "Sources/GGLabAppRuntime"
$appRuntimeTestsDir = Join-Path $root "Tests/GGLabAppRuntime"
$foundationSourcesDir = Join-Path $root "Sources/GGLabFoundation"
$foundationPublicDir = Join-Path $foundationSourcesDir "Public"
$foundationPrivateDir = Join-Path $foundationSourcesDir "Private"
$testCoreSourcesDir = Join-Path $root "Sources/GGLabTestCore"
$foundationTestsDir = Join-Path $root "Tests/GGLabFoundation"
$runtimeTestsDir = Join-Path $root "Tests/GGLabRuntime"
$shaderToolchainTestsDir = Join-Path $root "Tests/ShaderToolchain"
$shaderRuntimeIntegrationTestsDir = Join-Path $root "Tests/ShaderRuntimeIntegration"
$napaTestsDir = Join-Path $root "Tests/NapaVoxelCore"
$runtimeSourcesDir = Join-Path $root "Sources/GGLabRuntime"
$runtimePublicDir = Join-Path $runtimeSourcesDir "Public"
$runtimePrivateDir = Join-Path $runtimeSourcesDir "Private"
$shaderArtifactRuntimeSourcesDir = Join-Path $root "Sources/ShaderArtifactRuntime"
$shaderToolchainSourcesDir = Join-Path $root "Sources/ShaderToolchain"
$shaderCompilerSourcesDir = Join-Path $root "Sources/Tools/ShaderCompiler"
$napaSourcesDir = Join-Path $root "Sources/NapaVoxelCore"
$napaPublicDir = Join-Path $napaSourcesDir "Public"

function Test-IsPathUnderRoot {
    param(
        [string]$Path,
        [string]$Directory
    )

    return $Path.StartsWith(
        $Directory.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)
}

function ConvertTo-RepoRelativePath {
    param([string]$Path)

    return $Path.Substring($root.Length + 1).Replace('\', '/')
}

# Legacy Runtime candidate directories (portable candidates; backend leaves included).
# Migrated Core and Scene files are validated through the Public/Private ownership rules below.
$candidateDirs = @("Graphics", "Diagnostics")

# Platform / backend leaf allowlists.
# Permanent leaves are reviewed and need no removal condition.
$platformLeafPrefixes = @(
    "Core/Platform/Win", # Windows implementation leaves
    "Graphics/Asset/DerivedData/Platform/Win", # Local DDC Windows platform leaf
    "Graphics/RHI/DX12"  # DX12 backend leaf (Windows-native by design)
)
$platformLeafFiles = @(
    "Graphics/RHI/Vulkan/VulkanWin32Surface.h", # Win32 WSI leaf
    "Graphics/RHI/Vulkan/VulkanWin32Surface.cpp", # Win32 WSI leaf
    "Graphics/Asset/Loading/TextureLoader.cpp"  # DirectXTex (Windows third-party) consumer
)

# Transitional debt: allowed only with an explicit removal condition.
$transitionalDebt = @()

# Known violations ledger. Each entry: File, Kind (ownership/platform), Reason
# (planned owner / removal condition). New entries may only be added with an
# explicit plan; entries whose violation disappears are reported as stale and
# should be removed from the ledger.
$knownViolations = @()

$ownershipIncludeRegex = '#include\s*"(Application|DevTools|Core/Input)/'
$ownershipSymbolRegex = '\bDevelopGuiSystem\b'
$platformLeakRegex = 'Windows\.h|GameInput|IGameInput|\bHWND\b|\bHMODULE\b|\bHRESULT\b|CoInitializeEx|SetThreadDescription|MultiByteToWideChar|WideCharToMultiByte|GetModuleFileName|CreateSymbolicLink|GetCurrentProcessId|GetTickCount64|GetExeOutDir|bcrypt\.h'
$platformLeafIncludeRegex = '#include\s*"(Core/Platform/Win/|GGLabFoundation/Platform/Win/|Graphics/RHI/Vulkan/VulkanWin32Surface\.h)'
$platformNamespaceRegex = '\bwin32::'
$platformTypeRegex = '\bVulkanWin32Surface(Factory)?\b'

function Test-IsExempt {
    param([string]$RelativePath)

    # Permanent platform leaves are exempt from scanning. Transitional debt is
    # NOT exempt: it must be scanned, matched when the violation is still
    # present, and reported stale when resolved.
    foreach ($prefix in $platformLeafPrefixes) {
        if ($RelativePath.StartsWith($prefix + "/")) {
            return $true
        }
    }
    foreach ($leaf in $platformLeafFiles) {
        if ($RelativePath -eq $leaf) {
            return $true
        }
    }
    return $false
}

function Test-IsTransitionalDebt {
    param([string]$File)

    foreach ($debt in $transitionalDebt) {
        if ($debt.File -eq $File) {
            return $true
        }
    }
    return $false
}

function Test-IsKnown {
    param(
        [string]$File,
        [string]$Kind
    )

    foreach ($entry in $knownViolations) {
        if ($entry.File -eq $File -and $entry.Kind -eq $Kind) {
            return $true
        }
    }
    return $false
}

# Collect portable-platform candidates with Runtime-owner-root-relative paths.
$candidateFiles = @()
foreach ($dir in $candidateDirs) {
    $dirPath = Join-Path $runtimeSourcesDir $dir
    if (-not (Test-Path $dirPath)) {
        throw "Candidate directory not found: $dirPath"
    }
    $candidateFiles += Get-ChildItem -Path $dirPath -Recurse -File |
        Where-Object { $_.Extension -in @(".h", ".cpp") } |
        ForEach-Object {
            [pscustomobject]@{
                Path     = $_.FullName.Substring($runtimeSourcesDir.Length + 1).Replace('\', '/')
                FullPath = $_.FullName
            }
        }
}

# Collect the authoritative runtime ownership set from the project. Project-local
# build anchors and vendored sources use repository-relative paths; owner-root
# items use owner-root-relative paths so they share the boundary ledger namespace.
$runtimeProjectPath = Join-Path $root "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
if (-not (Test-Path $runtimeProjectPath)) {
    throw "Runtime project not found: $runtimeProjectPath"
}
$runtimeProject = [xml](Get-Content -LiteralPath $runtimeProjectPath -Raw -ErrorAction Stop)
$namespace = New-Object System.Xml.XmlNamespaceManager($runtimeProject.NameTable)
$namespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$runtimeProjectDir = Split-Path -Parent $runtimeProjectPath
$runtimeOwnedFiles = @(
    $runtimeProject.SelectNodes("//msb:ClCompile | //msb:ClInclude", $namespace) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_.Include) } |
        ForEach-Object {
            $fullPath = [System.IO.Path]::GetFullPath((Join-Path $runtimeProjectDir $_.Include))
            if (-not (Test-Path $fullPath)) {
                throw "Runtime project item not found: $($_.Include)"
            }
            $path = if ($fullPath.StartsWith($runtimeSourcesDir + [System.IO.Path]::DirectorySeparatorChar)) {
                $fullPath.Substring($runtimeSourcesDir.Length + 1)
            }
            else {
                $fullPath.Substring($root.Length + 1)
            }
            [pscustomobject]@{
                Path     = $path.Replace('\', '/')
                FullPath = $fullPath
            }
        } |
        Sort-Object FullPath -Unique
)

$winAppProjectPath = Join-Path $root "Projects/WinApp/WinApp.vcxproj"
if (-not (Test-Path $winAppProjectPath)) {
    throw "WinApp project not found: $winAppProjectPath"
}
$winAppProject = [xml](Get-Content -LiteralPath $winAppProjectPath -Raw -ErrorAction Stop)
$winAppNamespace = New-Object System.Xml.XmlNamespaceManager($winAppProject.NameTable)
$winAppNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$winAppProjectDir = Split-Path -Parent $winAppProjectPath

$vulkanQualificationProjectPath = Join-Path $root `
    "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj"
if (-not (Test-Path $vulkanQualificationProjectPath)) {
    throw "GGLabVulkanQualification project not found: $vulkanQualificationProjectPath"
}
$vulkanQualificationProject = [xml](
    Get-Content -LiteralPath $vulkanQualificationProjectPath -Raw -ErrorAction Stop)
$vulkanQualificationNamespace = New-Object `
    System.Xml.XmlNamespaceManager($vulkanQualificationProject.NameTable)
$vulkanQualificationNamespace.AddNamespace(
    "msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$vulkanQualificationProjectDir = Split-Path -Parent $vulkanQualificationProjectPath

$appRuntimeProjectPath = Join-Path $root "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
if (-not (Test-Path $appRuntimeProjectPath)) {
    throw "GGLabAppRuntime project not found: $appRuntimeProjectPath"
}
$appRuntimeProject = [xml](Get-Content -LiteralPath $appRuntimeProjectPath -Raw -ErrorAction Stop)
$appRuntimeNamespace = New-Object System.Xml.XmlNamespaceManager($appRuntimeProject.NameTable)
$appRuntimeNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$appRuntimeProjectDir = Split-Path -Parent $appRuntimeProjectPath

$foundationProjectPath = Join-Path $root "Projects/GGLabFoundation/GGLabFoundation.vcxproj"
if (-not (Test-Path $foundationProjectPath)) {
    throw "GGLabFoundation project not found: $foundationProjectPath"
}
$foundationProject = [xml](Get-Content -LiteralPath $foundationProjectPath -Raw -ErrorAction Stop)
$foundationNamespace = New-Object System.Xml.XmlNamespaceManager($foundationProject.NameTable)
$foundationNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$foundationProjectDir = Split-Path -Parent $foundationProjectPath

$foundationTestsProjectPath = Join-Path $root `
    "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj"
if (-not (Test-Path $foundationTestsProjectPath)) {
    throw "GGLabFoundationTests project not found: $foundationTestsProjectPath"
}
$foundationTestsProject = [xml](
    Get-Content -LiteralPath $foundationTestsProjectPath -Raw -ErrorAction Stop)
$foundationTestsNamespace = New-Object `
    System.Xml.XmlNamespaceManager($foundationTestsProject.NameTable)
$foundationTestsNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$foundationTestsProjectDir = Split-Path -Parent $foundationTestsProjectPath

$runtimeTestsProjectPath = Join-Path $root "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj"
if (-not (Test-Path $runtimeTestsProjectPath)) {
    throw "GGLabRuntimeTests project not found: $runtimeTestsProjectPath"
}
$runtimeTestsProject = [xml](Get-Content -LiteralPath $runtimeTestsProjectPath -Raw -ErrorAction Stop)
$runtimeTestsNamespace = New-Object System.Xml.XmlNamespaceManager($runtimeTestsProject.NameTable)
$runtimeTestsNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$runtimeTestsProjectDir = Split-Path -Parent $runtimeTestsProjectPath

$shaderToolchainTestsProjectPath = Join-Path $root `
    "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj"
if (-not (Test-Path $shaderToolchainTestsProjectPath)) {
    throw "ShaderToolchainTests project not found: $shaderToolchainTestsProjectPath"
}
$shaderToolchainTestsProject = [xml](
    Get-Content -LiteralPath $shaderToolchainTestsProjectPath -Raw -ErrorAction Stop)
$shaderToolchainTestsNamespace = New-Object `
    System.Xml.XmlNamespaceManager($shaderToolchainTestsProject.NameTable)
$shaderToolchainTestsNamespace.AddNamespace(
    "msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$shaderToolchainTestsProjectDir = Split-Path -Parent $shaderToolchainTestsProjectPath

$shaderRuntimeIntegrationTestsProjectPath = Join-Path $root `
    "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj"
if (-not (Test-Path $shaderRuntimeIntegrationTestsProjectPath)) {
    throw "ShaderRuntimeIntegrationTests project not found: " +
        $shaderRuntimeIntegrationTestsProjectPath
}
$shaderRuntimeIntegrationTestsProject = [xml](
    Get-Content -LiteralPath $shaderRuntimeIntegrationTestsProjectPath -Raw -ErrorAction Stop)
$shaderRuntimeIntegrationTestsNamespace = New-Object `
    System.Xml.XmlNamespaceManager($shaderRuntimeIntegrationTestsProject.NameTable)
$shaderRuntimeIntegrationTestsNamespace.AddNamespace(
    "msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$shaderRuntimeIntegrationTestsProjectDir =
    Split-Path -Parent $shaderRuntimeIntegrationTestsProjectPath

$appRuntimeTestsProjectPath = Join-Path $root `
    "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj"
if (-not (Test-Path $appRuntimeTestsProjectPath)) {
    throw "GGLabAppRuntimeTests project not found: $appRuntimeTestsProjectPath"
}
$appRuntimeTestsProject = [xml](
    Get-Content -LiteralPath $appRuntimeTestsProjectPath -Raw -ErrorAction Stop)
$appRuntimeTestsNamespace = New-Object `
    System.Xml.XmlNamespaceManager($appRuntimeTestsProject.NameTable)
$appRuntimeTestsNamespace.AddNamespace(
    "msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$appRuntimeTestsProjectDir = Split-Path -Parent $appRuntimeTestsProjectPath

$napaTestsProjectPath = Join-Path $root "Projects/NapaVoxelCoreTests/NapaVoxelCoreTests.vcxproj"
if (-not (Test-Path $napaTestsProjectPath)) {
    throw "NapaVoxelCoreTests project not found: $napaTestsProjectPath"
}
$napaTestsProject = [xml](Get-Content -LiteralPath $napaTestsProjectPath -Raw -ErrorAction Stop)
$napaTestsNamespace = New-Object System.Xml.XmlNamespaceManager($napaTestsProject.NameTable)
$napaTestsNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$napaTestsProjectDir = Split-Path -Parent $napaTestsProjectPath

$testCoreProjectPath = Join-Path $root "Projects/GGLabTestCore/GGLabTestCore.vcxproj"
if (-not (Test-Path $testCoreProjectPath)) {
    throw "GGLabTestCore project not found: $testCoreProjectPath"
}
$testCoreProject = [xml](Get-Content -LiteralPath $testCoreProjectPath -Raw -ErrorAction Stop)
$testCoreNamespace = New-Object System.Xml.XmlNamespaceManager($testCoreProject.NameTable)
$testCoreNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$testCoreProjectDir = Split-Path -Parent $testCoreProjectPath

$napaProjectPath = Join-Path $root "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj"
if (-not (Test-Path $napaProjectPath)) {
    throw "NapaVoxelCore project not found: $napaProjectPath"
}
$napaProject = [xml](Get-Content -LiteralPath $napaProjectPath -Raw -ErrorAction Stop)
$napaNamespace = New-Object System.Xml.XmlNamespaceManager($napaProject.NameTable)
$napaNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$napaProjectDir = Split-Path -Parent $napaProjectPath

$shaderToolchainProjectPath = Join-Path $root `
    "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
if (-not (Test-Path $shaderToolchainProjectPath)) {
    throw "ShaderToolchainCore project not found: $shaderToolchainProjectPath"
}
$shaderToolchainProject = [xml](
    Get-Content -LiteralPath $shaderToolchainProjectPath -Raw -ErrorAction Stop)
$shaderToolchainNamespace = New-Object `
    System.Xml.XmlNamespaceManager($shaderToolchainProject.NameTable)
$shaderToolchainNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$shaderToolchainProjectDir = Split-Path -Parent $shaderToolchainProjectPath

$shaderArtifactRuntimeProjectPath = Join-Path $root `
    "Projects/ShaderArtifactRuntime/ShaderArtifactRuntime.vcxproj"
if (-not (Test-Path $shaderArtifactRuntimeProjectPath)) {
    throw "ShaderArtifactRuntime project not found: $shaderArtifactRuntimeProjectPath"
}
$shaderArtifactRuntimeProject = [xml](
    Get-Content -LiteralPath $shaderArtifactRuntimeProjectPath -Raw -ErrorAction Stop)
$shaderArtifactRuntimeNamespace = New-Object `
    System.Xml.XmlNamespaceManager($shaderArtifactRuntimeProject.NameTable)
$shaderArtifactRuntimeNamespace.AddNamespace(
    "msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$shaderArtifactRuntimeProjectDir = Split-Path -Parent $shaderArtifactRuntimeProjectPath

$shaderCompilerProjectPath = Join-Path $root "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
if (-not (Test-Path $shaderCompilerProjectPath)) {
    throw "ShaderCompiler project not found: $shaderCompilerProjectPath"
}
$shaderCompilerProject = [xml](
    Get-Content -LiteralPath $shaderCompilerProjectPath -Raw -ErrorAction Stop)
$shaderCompilerNamespace = New-Object `
    System.Xml.XmlNamespaceManager($shaderCompilerProject.NameTable)
$shaderCompilerNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$shaderCompilerProjectDir = Split-Path -Parent $shaderCompilerProjectPath

function Get-ProjectItemPaths {
    param(
        [xml]$Project,
        [System.Xml.XmlNamespaceManager]$Namespace,
        [string]$ProjectDir,
        [string]$XPath,
        [string]$ItemKind
    )

    return @(
        $Project.SelectNodes($XPath, $Namespace) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_.Include) } |
            ForEach-Object {
                $fullPath = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir $_.Include))
                if (-not (Test-Path $fullPath)) {
                    throw "$ItemKind not found: $($_.Include)"
                }
                $fullPath
            } |
            Sort-Object -Unique
    )
}

$runtimeCompileFiles = Get-ProjectItemPaths $runtimeProject $namespace $runtimeProjectDir `
    "//msb:ClCompile" "Runtime compile item"
$winAppCompileFiles = Get-ProjectItemPaths $winAppProject $winAppNamespace `
    $winAppProjectDir "//msb:ClCompile" "WinApp compile item"
$vulkanQualificationCompileFiles = Get-ProjectItemPaths `
    $vulkanQualificationProject $vulkanQualificationNamespace `
    $vulkanQualificationProjectDir "//msb:ClCompile" `
    "GGLabVulkanQualification compile item"
$appRuntimeCompileFiles = Get-ProjectItemPaths $appRuntimeProject $appRuntimeNamespace `
    $appRuntimeProjectDir "//msb:ClCompile" "GGLabAppRuntime compile item"
$foundationCompileFiles = Get-ProjectItemPaths $foundationProject $foundationNamespace `
    $foundationProjectDir "//msb:ClCompile" "GGLabFoundation compile item"
$foundationTestsCompileFiles = Get-ProjectItemPaths `
    $foundationTestsProject $foundationTestsNamespace $foundationTestsProjectDir `
    "//msb:ClCompile" "GGLabFoundationTests compile item"
$napaCompileFiles = Get-ProjectItemPaths $napaProject $napaNamespace $napaProjectDir `
    "//msb:ClCompile" "NapaVoxelCore compile item"
$runtimeSourceItems = Get-ProjectItemPaths $runtimeProject $namespace $runtimeProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "Runtime source item"
$winAppSourceItems = Get-ProjectItemPaths $winAppProject $winAppNamespace `
    $winAppProjectDir "//msb:ClCompile | //msb:ClInclude" "WinApp source item"
$vulkanQualificationSourceItems = Get-ProjectItemPaths `
    $vulkanQualificationProject $vulkanQualificationNamespace `
    $vulkanQualificationProjectDir "//msb:ClCompile | //msb:ClInclude" `
    "GGLabVulkanQualification source item"
$appRuntimeSourceItems = Get-ProjectItemPaths $appRuntimeProject $appRuntimeNamespace `
    $appRuntimeProjectDir "//msb:ClCompile | //msb:ClInclude" "GGLabAppRuntime source item"
$foundationSourceItems = Get-ProjectItemPaths $foundationProject $foundationNamespace `
    $foundationProjectDir "//msb:ClCompile | //msb:ClInclude" "GGLabFoundation source item"
$foundationTestsSourceItems = Get-ProjectItemPaths `
    $foundationTestsProject $foundationTestsNamespace $foundationTestsProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "GGLabFoundationTests source item"
$napaSourceItems = Get-ProjectItemPaths $napaProject $napaNamespace $napaProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "NapaVoxelCore source item"
$testCoreCompileFiles = Get-ProjectItemPaths $testCoreProject $testCoreNamespace $testCoreProjectDir `
    "//msb:ClCompile" "GGLabTestCore compile item"
$testCoreSourceItems = Get-ProjectItemPaths $testCoreProject $testCoreNamespace $testCoreProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "GGLabTestCore source item"
$runtimeTestsCompileFiles = Get-ProjectItemPaths $runtimeTestsProject $runtimeTestsNamespace `
    $runtimeTestsProjectDir "//msb:ClCompile" "GGLabRuntimeTests compile item"
$runtimeTestsSourceItems = Get-ProjectItemPaths $runtimeTestsProject $runtimeTestsNamespace `
    $runtimeTestsProjectDir "//msb:ClCompile | //msb:ClInclude" "GGLabRuntimeTests source item"
$runtimeTestsProjectReferences = Get-ProjectItemPaths $runtimeTestsProject `
    $runtimeTestsNamespace $runtimeTestsProjectDir "//msb:ProjectReference" "GGLabRuntimeTests project reference"
$shaderToolchainTestsCompileFiles = Get-ProjectItemPaths `
    $shaderToolchainTestsProject $shaderToolchainTestsNamespace `
    $shaderToolchainTestsProjectDir "//msb:ClCompile" "ShaderToolchainTests compile item"
$shaderToolchainTestsSourceItems = Get-ProjectItemPaths `
    $shaderToolchainTestsProject $shaderToolchainTestsNamespace `
    $shaderToolchainTestsProjectDir "//msb:ClCompile | //msb:ClInclude" `
    "ShaderToolchainTests source item"
$shaderToolchainTestsProjectReferences = Get-ProjectItemPaths `
    $shaderToolchainTestsProject $shaderToolchainTestsNamespace `
    $shaderToolchainTestsProjectDir "//msb:ProjectReference" `
    "ShaderToolchainTests project reference"
$shaderRuntimeIntegrationTestsCompileFiles = Get-ProjectItemPaths `
    $shaderRuntimeIntegrationTestsProject $shaderRuntimeIntegrationTestsNamespace `
    $shaderRuntimeIntegrationTestsProjectDir "//msb:ClCompile" `
    "ShaderRuntimeIntegrationTests compile item"
$shaderRuntimeIntegrationTestsSourceItems = Get-ProjectItemPaths `
    $shaderRuntimeIntegrationTestsProject $shaderRuntimeIntegrationTestsNamespace `
    $shaderRuntimeIntegrationTestsProjectDir "//msb:ClCompile | //msb:ClInclude" `
    "ShaderRuntimeIntegrationTests source item"
$shaderRuntimeIntegrationTestsProjectReferences = Get-ProjectItemPaths `
    $shaderRuntimeIntegrationTestsProject $shaderRuntimeIntegrationTestsNamespace `
    $shaderRuntimeIntegrationTestsProjectDir "//msb:ProjectReference" `
    "ShaderRuntimeIntegrationTests project reference"
$appRuntimeTestsCompileFiles = Get-ProjectItemPaths `
    $appRuntimeTestsProject $appRuntimeTestsNamespace $appRuntimeTestsProjectDir `
    "//msb:ClCompile" "GGLabAppRuntimeTests compile item"
$appRuntimeTestsSourceItems = Get-ProjectItemPaths `
    $appRuntimeTestsProject $appRuntimeTestsNamespace $appRuntimeTestsProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "GGLabAppRuntimeTests source item"
$appRuntimeTestsProjectReferences = Get-ProjectItemPaths `
    $appRuntimeTestsProject $appRuntimeTestsNamespace $appRuntimeTestsProjectDir `
    "//msb:ProjectReference" "GGLabAppRuntimeTests project reference"
$napaTestsCompileFiles = Get-ProjectItemPaths $napaTestsProject $napaTestsNamespace `
    $napaTestsProjectDir "//msb:ClCompile" "NapaVoxelCoreTests compile item"
$napaTestsSourceItems = Get-ProjectItemPaths $napaTestsProject $napaTestsNamespace `
    $napaTestsProjectDir "//msb:ClCompile | //msb:ClInclude" "NapaVoxelCoreTests source item"
$napaTestsProjectReferences = Get-ProjectItemPaths $napaTestsProject $napaTestsNamespace `
    $napaTestsProjectDir "//msb:ProjectReference" "NapaVoxelCoreTests project reference"
$testCoreProjectReferences = Get-ProjectItemPaths $testCoreProject $testCoreNamespace `
    $testCoreProjectDir "//msb:ProjectReference" "GGLabTestCore project reference"
$runtimeProjectReferences = Get-ProjectItemPaths $runtimeProject $namespace $runtimeProjectDir `
    "//msb:ProjectReference" "Runtime project reference"
$winAppProjectReferences = Get-ProjectItemPaths $winAppProject $winAppNamespace `
    $winAppProjectDir "//msb:ProjectReference" "WinApp project reference"
$vulkanQualificationProjectReferences = Get-ProjectItemPaths `
    $vulkanQualificationProject $vulkanQualificationNamespace `
    $vulkanQualificationProjectDir "//msb:ProjectReference" `
    "GGLabVulkanQualification project reference"
$appRuntimeProjectReferences = Get-ProjectItemPaths $appRuntimeProject $appRuntimeNamespace `
    $appRuntimeProjectDir "//msb:ProjectReference" "GGLabAppRuntime project reference"
$foundationProjectReferences = Get-ProjectItemPaths $foundationProject $foundationNamespace `
    $foundationProjectDir "//msb:ProjectReference" "GGLabFoundation project reference"
$foundationTestsProjectReferences = Get-ProjectItemPaths `
    $foundationTestsProject $foundationTestsNamespace $foundationTestsProjectDir `
    "//msb:ProjectReference" "GGLabFoundationTests project reference"
$napaProjectReferences = Get-ProjectItemPaths $napaProject $napaNamespace $napaProjectDir `
    "//msb:ProjectReference" "NapaVoxelCore project reference"
$shaderToolchainCompileFiles = Get-ProjectItemPaths $shaderToolchainProject `
    $shaderToolchainNamespace $shaderToolchainProjectDir `
    "//msb:ClCompile" "ShaderToolchainCore compile item"
$shaderToolchainSourceItems = Get-ProjectItemPaths $shaderToolchainProject `
    $shaderToolchainNamespace $shaderToolchainProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "ShaderToolchainCore source item"
$shaderToolchainProjectReferences = Get-ProjectItemPaths $shaderToolchainProject `
    $shaderToolchainNamespace $shaderToolchainProjectDir `
    "//msb:ProjectReference" "ShaderToolchainCore project reference"
$shaderArtifactRuntimeCompileFiles = Get-ProjectItemPaths $shaderArtifactRuntimeProject `
    $shaderArtifactRuntimeNamespace $shaderArtifactRuntimeProjectDir `
    "//msb:ClCompile" "ShaderArtifactRuntime compile item"
$shaderArtifactRuntimeSourceItems = Get-ProjectItemPaths $shaderArtifactRuntimeProject `
    $shaderArtifactRuntimeNamespace $shaderArtifactRuntimeProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "ShaderArtifactRuntime source item"
$shaderArtifactRuntimeProjectReferences = Get-ProjectItemPaths `
    $shaderArtifactRuntimeProject $shaderArtifactRuntimeNamespace `
    $shaderArtifactRuntimeProjectDir "//msb:ProjectReference" `
    "ShaderArtifactRuntime project reference"
$shaderCompilerCompileFiles = Get-ProjectItemPaths $shaderCompilerProject `
    $shaderCompilerNamespace $shaderCompilerProjectDir `
    "//msb:ClCompile" "ShaderCompiler compile item"
$shaderCompilerSourceItems = Get-ProjectItemPaths $shaderCompilerProject `
    $shaderCompilerNamespace $shaderCompilerProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "ShaderCompiler source item"
$shaderCompilerProjectReferences = Get-ProjectItemPaths $shaderCompilerProject `
    $shaderCompilerNamespace $shaderCompilerProjectDir `
    "//msb:ProjectReference" "ShaderCompiler project reference"

$runtimeProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$winAppProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$vulkanQualificationProjectReferenceSet = New-Object `
    'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$appRuntimeProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$foundationProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$foundationTestsProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$napaProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$testCoreProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$runtimeTestsProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderToolchainTestsProjectReferenceSet = New-Object `
    'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderRuntimeIntegrationTestsProjectReferenceSet = New-Object `
    'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$appRuntimeTestsProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$napaTestsProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderToolchainProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderArtifactRuntimeProjectReferenceSet = New-Object `
    'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderCompilerProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in $runtimeProjectReferences) {
    [void]$runtimeProjectReferenceSet.Add($path)
}
foreach ($path in $winAppProjectReferences) {
    [void]$winAppProjectReferenceSet.Add($path)
}
foreach ($path in $vulkanQualificationProjectReferences) {
    [void]$vulkanQualificationProjectReferenceSet.Add($path)
}
foreach ($path in $appRuntimeProjectReferences) {
    [void]$appRuntimeProjectReferenceSet.Add($path)
}
foreach ($path in $foundationProjectReferences) {
    [void]$foundationProjectReferenceSet.Add($path)
}
foreach ($path in $foundationTestsProjectReferences) {
    [void]$foundationTestsProjectReferenceSet.Add($path)
}
foreach ($path in $napaProjectReferences) {
    [void]$napaProjectReferenceSet.Add($path)
}
foreach ($path in $testCoreProjectReferences) {
    [void]$testCoreProjectReferenceSet.Add($path)
}
foreach ($path in $runtimeTestsProjectReferences) {
    [void]$runtimeTestsProjectReferenceSet.Add($path)
}
foreach ($path in $shaderToolchainTestsProjectReferences) {
    [void]$shaderToolchainTestsProjectReferenceSet.Add($path)
}
foreach ($path in $shaderRuntimeIntegrationTestsProjectReferences) {
    [void]$shaderRuntimeIntegrationTestsProjectReferenceSet.Add($path)
}
foreach ($path in $appRuntimeTestsProjectReferences) {
    [void]$appRuntimeTestsProjectReferenceSet.Add($path)
}
foreach ($path in $napaTestsProjectReferences) {
    [void]$napaTestsProjectReferenceSet.Add($path)
}
foreach ($path in $shaderToolchainProjectReferences) {
    [void]$shaderToolchainProjectReferenceSet.Add($path)
}
foreach ($path in $shaderArtifactRuntimeProjectReferences) {
    [void]$shaderArtifactRuntimeProjectReferenceSet.Add($path)
}
foreach ($path in $shaderCompilerProjectReferences) {
    [void]$shaderCompilerProjectReferenceSet.Add($path)
}

$projectContractFindings = New-Object System.Collections.Generic.List[object]

function Test-ProjectIncludeVisibility {
    param(
        [xml]$Project,
        [System.Xml.XmlNamespaceManager]$Namespace,
        [string]$ProjectPath,
        [array]$RequiredFirstPartyRoots,
        [array]$AllowedFirstPartyRoots
    )

    $includeDirectoryNodes = @(
        $Project.SelectNodes(
            "//msb:ItemDefinitionGroup/msb:ClCompile/msb:AdditionalIncludeDirectories",
            $Namespace)
    )
    if ($includeDirectoryNodes.Count -eq 0) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "include-visibility"
            Target = $ProjectPath
            Reason = "missing include-directory definitions"
        })
    }

    foreach ($includeNode in $includeDirectoryNodes) {
        $includeRoots = @(
            ([string]$includeNode.InnerText).Split(';') |
                ForEach-Object { $_.Trim().TrimEnd('\') } |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )
        $condition = [string]$includeNode.ParentNode.ParentNode.Condition
        $target = if ([string]::IsNullOrWhiteSpace($condition)) {
            $ProjectPath
        }
        else {
            "$ProjectPath ($condition)"
        }

        foreach ($requiredRoot in $RequiredFirstPartyRoots) {
            if ($includeRoots -notcontains $requiredRoot) {
                $projectContractFindings.Add([pscustomobject]@{
                    Rule   = "include-visibility"
                    Target = $target
                    Reason = "missing required first-party include root: $requiredRoot"
                })
            }
        }

        foreach ($includeRoot in $includeRoots) {
            $isFirstPartySourceRoot =
                $includeRoot.StartsWith('$(GGLabRepositoryRoot)Sources',
                    [System.StringComparison]::OrdinalIgnoreCase) -or
                $includeRoot.StartsWith('$(NapaVoxelRepositoryRoot)Sources',
                    [System.StringComparison]::OrdinalIgnoreCase)
            if ($isFirstPartySourceRoot -and
                $AllowedFirstPartyRoots -notcontains $includeRoot) {
                $projectContractFindings.Add([pscustomobject]@{
                    Rule   = "include-visibility"
                    Target = $target
                    Reason = "unexpected first-party include root: $includeRoot"
                })
            }
        }
    }
}

$runtimeIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabRuntime'
$runtimePublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabRuntime\Public'
$runtimePrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabRuntime\Private'
$winAppIncludeRoot = '$(GGLabRepositoryRoot)Sources\WinApp'
$vulkanQualificationIncludeRoot = `
    '$(GGLabRepositoryRoot)Sources\GGLabVulkanQualification'
$appRuntimeIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabAppRuntime'
$shaderArtifactRuntimePublicIncludeRoot = `
    '$(GGLabRepositoryRoot)Sources\ShaderArtifactRuntime\Public'
$shaderToolchainIncludeRoot = '$(GGLabRepositoryRoot)Sources\ShaderToolchain'
$shaderCompilerIncludeRoot = '$(GGLabRepositoryRoot)Sources\Tools\ShaderCompiler'
$foundationPublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Public'
$foundationPrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Private'
$testCorePublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabTestCore\Public'
$testCorePrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabTestCore\Private'
$runtimeTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\GGLabRuntime'
$shaderToolchainTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\ShaderToolchain'
$shaderRuntimeIntegrationTestsIncludeRoot = `
    '$(GGLabRepositoryRoot)Tests\ShaderRuntimeIntegration'
$appRuntimeTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\GGLabAppRuntime'
$napaTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\NapaVoxelCore'
$napaConsumerPublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\NapaVoxelCore\Public'
$napaProjectPublicIncludeRoot = '$(NapaVoxelRepositoryRoot)Sources\NapaVoxelCore\Public'
$napaProjectPrivateIncludeRoot = '$(NapaVoxelRepositoryRoot)Sources\NapaVoxelCore\Private'
Test-ProjectIncludeVisibility $runtimeProject $namespace `
    "Projects/GGLabRuntime/GGLabRuntime.vcxproj" `
    @($runtimePrivateIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot) `
    @($runtimePrivateIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot)
# Phase 3A keeps Runtime Private visibility on WinApp because that project still
# owns the Win32 RHI factory and DX12/Vulkan DevelopGui backend adapters. Phase 4
# must isolate those sources before Phase 3B removes this target-wide root.
Test-ProjectIncludeVisibility $winAppProject $winAppNamespace `
    "Projects/WinApp/WinApp.vcxproj" `
    @($winAppIncludeRoot, $appRuntimeIncludeRoot, $runtimePrivateIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot, $napaConsumerPublicIncludeRoot) `
    @($winAppIncludeRoot, $appRuntimeIncludeRoot, $runtimePrivateIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot, $napaConsumerPublicIncludeRoot)
Test-ProjectIncludeVisibility $vulkanQualificationProject `
    $vulkanQualificationNamespace `
    "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj" `
    @($vulkanQualificationIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot) `
    @($vulkanQualificationIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot)
# AppRuntime consumes Runtime Public contracts plus the remaining legacy
# Graphics surface. DynamicBufferAllocator keeps its RingSpanAllocator storage
# opaque, so neither AppRuntime target receives Runtime Private visibility.
Test-ProjectIncludeVisibility $appRuntimeProject $appRuntimeNamespace `
    "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj" `
    @($appRuntimeIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot) `
    @($appRuntimeIncludeRoot, $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $foundationProject $foundationNamespace `
    "Projects/GGLabFoundation/GGLabFoundation.vcxproj" `
    @($foundationPublicIncludeRoot, $foundationPrivateIncludeRoot) `
    @($foundationPublicIncludeRoot, $foundationPrivateIncludeRoot)
Test-ProjectIncludeVisibility $foundationTestsProject $foundationTestsNamespace `
    "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj" `
    @($foundationPublicIncludeRoot) @($foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $testCoreProject $testCoreNamespace `
    "Projects/GGLabTestCore/GGLabTestCore.vcxproj" `
    @($testCorePublicIncludeRoot, $testCorePrivateIncludeRoot) `
    @($testCorePublicIncludeRoot, $testCorePrivateIncludeRoot)
Test-ProjectIncludeVisibility $runtimeTestsProject $runtimeTestsNamespace `
    "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj" `
    @($runtimeTestsIncludeRoot, $runtimePrivateIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot) `
    @($runtimeTestsIncludeRoot, $runtimePrivateIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderToolchainTestsProject `
    $shaderToolchainTestsNamespace `
    "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj" `
    @($shaderToolchainTestsIncludeRoot, $shaderToolchainIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot) `
    @($shaderToolchainTestsIncludeRoot, $shaderToolchainIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderRuntimeIntegrationTestsProject `
    $shaderRuntimeIntegrationTestsNamespace `
    "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj" `
    @($shaderRuntimeIntegrationTestsIncludeRoot, $runtimePublicIncludeRoot,
        $runtimeIncludeRoot,
        $shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot) `
    @($shaderRuntimeIntegrationTestsIncludeRoot, $runtimePublicIncludeRoot,
        $runtimeIncludeRoot,
        $shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot, $testCorePublicIncludeRoot)
Test-ProjectIncludeVisibility $appRuntimeTestsProject $appRuntimeTestsNamespace `
    "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj" `
    @($appRuntimeTestsIncludeRoot, $appRuntimeIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot) `
    @($appRuntimeTestsIncludeRoot, $appRuntimeIncludeRoot,
        $runtimePublicIncludeRoot, $runtimeIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderToolchainProject $shaderToolchainNamespace `
    "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj" `
    @($shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot) `
    @($shaderToolchainIncludeRoot, $shaderArtifactRuntimePublicIncludeRoot,
        $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderArtifactRuntimeProject `
    $shaderArtifactRuntimeNamespace `
    "Projects/ShaderArtifactRuntime/ShaderArtifactRuntime.vcxproj" `
    @($shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot) `
    @($shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderCompilerProject $shaderCompilerNamespace `
    "Projects/ShaderCompiler/ShaderCompiler.vcxproj" `
    @($shaderCompilerIncludeRoot, $shaderToolchainIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot) `
    @($shaderCompilerIncludeRoot, $shaderToolchainIncludeRoot,
        $shaderArtifactRuntimePublicIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $napaProject $napaNamespace `
    "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj" `
    @($napaProjectPublicIncludeRoot, $napaProjectPrivateIncludeRoot) `
    @($napaProjectPublicIncludeRoot, $napaProjectPrivateIncludeRoot)
Test-ProjectIncludeVisibility $napaTestsProject $napaTestsNamespace `
    "Projects/NapaVoxelCoreTests/NapaVoxelCoreTests.vcxproj" `
    @($napaTestsIncludeRoot, $napaConsumerPublicIncludeRoot) `
    @($napaTestsIncludeRoot, $napaConsumerPublicIncludeRoot)

$legacyWinAppInputDir = Join-Path $winAppSourcesDir "Core/Input"
if (Test-Path -LiteralPath $legacyWinAppInputDir) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "winapp-input-ownership"
        Target = ConvertTo-RepoRelativePath $legacyWinAppInputDir
        Reason = "Windows input implementation must live under Application/Platform/Windows/Input"
    })
}
$legacyWinAppInputIncludeRegex = '#include\s*[<"]Core[\\/]Input[\\/]'
foreach ($winAppSourceFile in Get-ChildItem -LiteralPath $winAppSourcesDir -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") }) {
    $content = Get-Content -LiteralPath $winAppSourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match $legacyWinAppInputIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "winapp-input-ownership"
            Target = ConvertTo-RepoRelativePath $winAppSourceFile.FullName
            Reason = "WinApp source still includes the legacy Core/Input path"
        })
    }
}

$legacyRuntimeCoreDir = Join-Path $runtimeSourcesDir "Core"
$legacyRuntimeCoreFiles = if (Test-Path -LiteralPath $legacyRuntimeCoreDir -PathType Container) {
    @(Get-ChildItem -LiteralPath $legacyRuntimeCoreDir -Recurse -File)
}
else {
    @()
}
foreach ($legacyFile in $legacyRuntimeCoreFiles) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "runtime-public-private-layout"
        Target = ConvertTo-RepoRelativePath $legacyFile.FullName
        Reason = "migrated Runtime Core files must live under Public/GGLabRuntime or Private"
    })
}

$legacyRuntimePublicCoreIncludeRegex =
    '#include\s*[<"]Core[\\/](?:Hash[\\/]KeyHash\.h|Log[\\/]|' +
    'Math[\\/](?!Backends[\\/])|Profiling[\\/]|StringId(?:Formatting)?\.h|' +
    'Time\.h|World\.h)'
foreach ($sourceFile in Get-ChildItem -LiteralPath @($repositorySourcesDir, $repositoryTestsDir) `
        -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") }) {
    $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match $legacyRuntimePublicCoreIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-include-prefix"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "migrated Runtime Core contracts must use the GGLabRuntime/Core include prefix"
        })
    }
}

$legacyRuntimeGraphicsContractPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/GraphicsTypes.h"),
    (Join-Path $runtimeSourcesDir "Graphics/ShadowSettings.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Asset/ArtifactContentDigest.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Asset/ArtifactContentDigest.cpp")
)
foreach ($legacyPath in $legacyRuntimeGraphicsContractPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated foundational Graphics contracts must live under Public/GGLabRuntime or Private"
        })
    }
}

$legacyRuntimeCameraPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/Camera.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Camera.cpp"),
    (Join-Path $runtimeSourcesDir "Graphics/CameraController.h"),
    (Join-Path $runtimeSourcesDir "Graphics/CameraController.cpp"),
    (Join-Path $runtimeSourcesDir "Graphics/CameraRig.h"),
    (Join-Path $runtimeSourcesDir "Graphics/CameraRig.cpp")
)
foreach ($legacyPath in $legacyRuntimeCameraPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated Camera contracts and implementations must live under Public/GGLabRuntime or Private"
        })
    }
}

$legacyRuntimeViewContractPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/ScreenSpace/ScreenSpaceTypes.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Pipeline/TemporalAA.h"),
    (Join-Path $runtimeSourcesDir "Graphics/PostProcess/ViewRenderSettings.h"),
    (Join-Path $runtimeSourcesDir "Graphics/PostProcess/ViewRenderSettings.cpp"),
    (Join-Path $runtimeSourcesDir "Graphics/RenderView.h"),
    (Join-Path $runtimeSourcesDir "Graphics/RenderView.cpp")
)
foreach ($legacyPath in $legacyRuntimeViewContractPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated view and temporal contracts must live under Public/GGLabRuntime or Private"
        })
    }
}

$legacyRuntimeRenderQueueContractPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/RenderParameters.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Pipeline/DepthCoverage.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Pipeline/DepthCoverage.cpp"),
    (Join-Path $runtimeSourcesDir "Graphics/RenderQueue.h"),
    (Join-Path $runtimeSourcesDir "Graphics/RenderQueue.cpp")
)
foreach ($legacyPath in $legacyRuntimeRenderQueueContractPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated render-queue contracts must live under Public/GGLabRuntime or Private"
        })
    }
}

$legacyRuntimeDebugDrawContractPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/DebugDraw/DebugDraw.h"),
    (Join-Path $runtimeSourcesDir "Graphics/DebugDraw/DebugDrawShapes.cpp"),
    (Join-Path $runtimeSourcesDir "Graphics/DebugDraw/DebugDrawSystem.cpp")
)
foreach ($legacyPath in $legacyRuntimeDebugDrawContractPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated DebugDraw contracts and implementations must live under Public/GGLabRuntime or Private"
        })
    }
}

$legacyRuntimeDiagnosticsContractPaths = @(
    (Join-Path $runtimeSourcesDir "Diagnostics/SnapshotCommon.h"),
    (Join-Path $runtimeSourcesDir "Diagnostics/Snapshots/DX12BackendSnapshot.h"),
    (Join-Path $runtimeSourcesDir "Diagnostics/Snapshots/VulkanBackendSnapshot.h"),
    (Join-Path $runtimeSourcesDir "Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"),
    (Join-Path $runtimeSourcesDir "Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"),
    (Join-Path $runtimeSourcesDir "Diagnostics/Snapshots/TaskSystemSnapshot.h")
)
foreach ($legacyPath in $legacyRuntimeDiagnosticsContractPaths) {
    if (Test-Path -LiteralPath $legacyPath -PathType Leaf) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-private-layout"
            Target = ConvertTo-RepoRelativePath $legacyPath
            Reason = "migrated Diagnostics contracts must live under Public/GGLabRuntime"
        })
    }
}

$developGuiDiagnosticsConsumerFiles = @()
$developGuiPanelsDir = Join-Path $winAppSourcesDir "DevTools/DevelopGui/Panels"
if (Test-Path -LiteralPath $developGuiPanelsDir -PathType Container) {
    $developGuiDiagnosticsConsumerFiles += @(
        Get-ChildItem -LiteralPath $developGuiPanelsDir -Recurse -File |
            Where-Object { $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") }
    )
}
$developGuiContextPath = Join-Path $winAppSourcesDir "DevTools/DevelopGui/DevelopGuiContext.h"
if (Test-Path -LiteralPath $developGuiContextPath -PathType Leaf) {
    $developGuiDiagnosticsConsumerFiles += Get-Item -LiteralPath $developGuiContextPath
}
foreach ($sourceFile in $developGuiDiagnosticsConsumerFiles) {
    $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match '\bDiagnosticsRuntime\b') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "diagnostics-read-view"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "DevTools panels must use DiagnosticsView or DiagnosticsControl instead of the capture engine"
        })
    }
    if ($content -match '\bGpuProfiler\b|\bGetGpuProfiler\b') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "gpu-profiling-tooling-boundary"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "DevTools panels must consume the separate Public GPU profiling query and control capabilities"
        })
    }
}

$diagnosticsViewPath = Join-Path $runtimePublicDir "GGLabRuntime/Diagnostics/DiagnosticsView.h"
if (Test-Path -LiteralPath $diagnosticsViewPath -PathType Leaf) {
    $diagnosticsViewContent = Get-Content -LiteralPath $diagnosticsViewPath -Raw -ErrorAction Stop
    if ($diagnosticsViewContent -match '\bRequestRefresh\b') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "diagnostics-query-control"
            Target = ConvertTo-RepoRelativePath $diagnosticsViewPath
            Reason = "DiagnosticsView must remain read-only; refresh requests belong to DiagnosticsControl"
        })
    }
}

$builtinSnapshotProvidersPath =
    Join-Path $runtimeSourcesDir "Diagnostics/Builders/BuiltinSnapshotProviders.cpp"
if (Test-Path -LiteralPath $builtinSnapshotProvidersPath -PathType Leaf) {
    $builtinSnapshotProvidersContent =
        Get-Content -LiteralPath $builtinSnapshotProvidersPath -Raw -ErrorAction Stop
    $builtinBackendProviderRegex =
        '\b(?:DX12Context|VulkanContext|DX12PipelineSystem|VulkanPipelineSystem|' +
        'DX12BackendSnapshot|DX12ResourceManagerSnapshot|VulkanBackendSnapshot|' +
        'RHIPipelineSystemSnapshotProvider|dynamic_cast)\b'
    if ($builtinSnapshotProvidersContent -match $builtinBackendProviderRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "diagnostics-backend-provider-ownership"
            Target = ConvertTo-RepoRelativePath $builtinSnapshotProvidersPath
            Reason = "Backend-neutral diagnostics registration must not rediscover or own DX12/Vulkan snapshot providers"
        })
    }
}

$diagnosticsBuildersDir = Join-Path $runtimeSourcesDir "Diagnostics/Builders"
$backendSnapshotDispatcherPath =
    Join-Path $diagnosticsBuildersDir "BackendSnapshotProviders.cpp"
$backendSpecificDiagnosticsRegex = '\b(?:DX12|Vulkan)'
foreach ($sourceFile in @(
        Get-ChildItem -LiteralPath $diagnosticsBuildersDir -File |
            Where-Object {
                $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") -and
                $_.FullName -ne $backendSnapshotDispatcherPath
            })) {
    $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match $backendSpecificDiagnosticsRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "diagnostics-backend-builder-ownership"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "Backend-specific diagnostics builders must live with their DX12 or Vulkan backend owner"
        })
    }
}

$winAppDiagnosticsCaptureOwnershipRegex =
    '\b(?:DiagnosticsRuntime|SnapshotContext|SnapshotProviderBase|SnapshotStore|' +
    'RegisterBuiltinSnapshotProviders|LabSnapshotProvider)\b'
foreach ($itemPath in $winAppSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $winAppDiagnosticsCaptureOwnershipRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "diagnostics-capture-ownership"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "WinApp must consume DiagnosticsView and narrow DiagnosticsControl without owning capture context, providers, or storage"
        })
    }
}

$legacyRuntimeSceneDir = Join-Path $runtimeSourcesDir "Scene"
$legacyRuntimeSceneFiles = if (Test-Path -LiteralPath $legacyRuntimeSceneDir -PathType Container) {
    @(Get-ChildItem -LiteralPath $legacyRuntimeSceneDir -Recurse -File)
}
else {
    @()
}
foreach ($legacyFile in $legacyRuntimeSceneFiles) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "runtime-public-private-layout"
        Target = ConvertTo-RepoRelativePath $legacyFile.FullName
        Reason = "migrated Runtime Scene contracts must live under Public/GGLabRuntime or Private"
    })
}

$legacyRuntimeGraphicsContractIncludeRegex =
    '#include\s*[<"]Graphics[\\/](?:GraphicsTypes\.h|ShadowSettings\.h|' +
    'Asset[\\/]ArtifactContentDigest\.h)[>"]'
$legacyRuntimeCameraIncludeRegex =
    '#include\s*[<"]Graphics[\\/]Camera(?:Controller|Rig)?\.h[>"]'
$legacyRuntimeViewContractIncludeRegex =
    '#include\s*[<"]Graphics[\\/](?:RenderView\.h|' +
    'Pipeline[\\/]TemporalAA\.h|PostProcess[\\/]ViewRenderSettings\.h|' +
    'ScreenSpace[\\/]ScreenSpaceTypes\.h)[>"]'
$legacyRuntimeRenderQueueContractIncludeRegex =
    '#include\s*[<"]Graphics[\\/](?:RenderParameters\.h|RenderQueue\.h|' +
    'Pipeline[\\/]DepthCoverage\.h)[>"]'
$legacyRuntimeDebugDrawContractIncludeRegex =
    '#include\s*[<"]Graphics[\\/]DebugDraw[\\/]DebugDraw\.h[>"]'
$legacyRuntimeDiagnosticsContractIncludeRegex =
    '#include\s*[<"]Diagnostics[\\/](?:SnapshotCommon\.h|' +
    'Snapshots[\\/](?:DX12BackendSnapshot|VulkanBackendSnapshot|' +
    'DX12ResourceManagerSnapshot|PersistentSceneBufferSnapshot|' +
    'TaskSystemSnapshot)\.h)[>"]'
$legacyRuntimeSceneIncludeRegex =
    '#include\s*[<"]Scene[\\/]Components\.h[>"]'
foreach ($sourceFile in Get-ChildItem -LiteralPath @($repositorySourcesDir, $repositoryTestsDir) `
        -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") }) {
    $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match $legacyRuntimeGraphicsContractIncludeRegex -or
        $content -match $legacyRuntimeCameraIncludeRegex -or
        $content -match $legacyRuntimeViewContractIncludeRegex -or
        $content -match $legacyRuntimeRenderQueueContractIncludeRegex -or
        $content -match $legacyRuntimeDebugDrawContractIncludeRegex -or
        $content -match $legacyRuntimeDiagnosticsContractIncludeRegex -or
        $content -match $legacyRuntimeSceneIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-include-prefix"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "migrated Graphics, Scene and Diagnostics contracts must use the GGLabRuntime include prefix"
        })
    }
}

$legacyRuntimeRhiDir = Join-Path $runtimeSourcesDir "Graphics/RHI"
$legacyRuntimeRhiFiles = if (Test-Path -LiteralPath $legacyRuntimeRhiDir -PathType Container) {
    @(Get-ChildItem -LiteralPath $legacyRuntimeRhiDir -File |
        Where-Object { $_.Name -ne "RHIHandleTable.h" })
}
else {
    @()
}
foreach ($legacyFile in $legacyRuntimeRhiFiles) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "runtime-public-private-layout"
        Target = ConvertTo-RepoRelativePath $legacyFile.FullName
        Reason = "migrated backend-neutral RHI files must live under Public/GGLabRuntime or Private"
    })
}

$legacyRuntimeShaderTypesPath = Join-Path $runtimeSourcesDir "Graphics/Shader/ShaderTypes.h"
if (Test-Path -LiteralPath $legacyRuntimeShaderTypesPath -PathType Leaf) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "runtime-public-private-layout"
        Target = ConvertTo-RepoRelativePath $legacyRuntimeShaderTypesPath
        Reason = "the RHI ShaderTypes contract must live under Public/GGLabRuntime"
    })
}

$legacyRuntimePublicRhiIncludeRegex =
    '#include\s*[<"]Graphics[\\/]RHI[\\/](?!RHIHandleTable\.h[>"]|' +
    'RHISubresourceUtils\.h[>"]|DX12[\\/]|Vulkan[\\/])RHI[^>"\\/]+'
$legacyRuntimeShaderTypesIncludeRegex =
    '#include\s*[<"]Graphics[\\/]Shader[\\/]ShaderTypes\.h[>"]'
foreach ($sourceFile in Get-ChildItem -LiteralPath @($repositorySourcesDir, $repositoryTestsDir) `
        -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @(".cpp", ".h", ".hpp", ".inl") }) {
    $content = Get-Content -LiteralPath $sourceFile.FullName -Raw -ErrorAction Stop
    if ($content -match $legacyRuntimePublicRhiIncludeRegex -or
        $content -match $legacyRuntimeShaderTypesIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-include-prefix"
            Target = ConvertTo-RepoRelativePath $sourceFile.FullName
            Reason = "migrated Runtime RHI contracts must use the GGLabRuntime/Graphics include prefix"
        })
    }
}

$appRuntimeConfigurationTypes = @($appRuntimeProject.SelectNodes(
    "//msb:PropertyGroup[@Label='Configuration']/msb:ConfigurationType",
    $appRuntimeNamespace))
if ($appRuntimeConfigurationTypes.Count -ne 2 -or
    @($appRuntimeConfigurationTypes | Where-Object { $_.InnerText -ne "StaticLibrary" }).Count -gt 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "build-policy"
        Target = "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
        Reason = "every configuration must build a StaticLibrary"
    })
}
$appRuntimePchNodes = @($appRuntimeProject.SelectNodes(
    "//msb:ItemDefinitionGroup/msb:ClCompile/msb:PrecompiledHeader",
    $appRuntimeNamespace))
if ($appRuntimePchNodes.Count -ne 2 -or
    @($appRuntimePchNodes | Where-Object { $_.InnerText -ne "NotUsing" }).Count -gt 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "build-policy"
        Target = "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
        Reason = "the initial shared target must use no precompiled header"
    })
}
$appRuntimeImports = @($appRuntimeProject.SelectNodes("//msb:Import[@Project]",
        $appRuntimeNamespace) | ForEach-Object { [string]$_.Project })
if (@($appRuntimeImports | Where-Object {
            $_.EndsWith("PropertySheets\GGLabBuildBaseline.props",
                [System.StringComparison]::OrdinalIgnoreCase)
        }).Count -ne 2 -or
    @($appRuntimeImports | Where-Object {
            $_.EndsWith("PropertySheets\Common.props",
                [System.StringComparison]::OrdinalIgnoreCase)
        }).Count -gt 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "build-policy"
        Target = "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
        Reason = "shared target must import only the workspace baseline, not Common.props"
    })
}
$appRuntimeTestsPchNodes = @($appRuntimeTestsProject.SelectNodes(
    "//msb:ItemDefinitionGroup/msb:ClCompile/msb:PrecompiledHeader",
    $appRuntimeTestsNamespace))
if ($appRuntimeTestsPchNodes.Count -ne 2 -or
    @($appRuntimeTestsPchNodes | Where-Object { $_.InnerText -ne "NotUsing" }).Count -gt 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "build-policy"
        Target = "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj"
        Reason = "shared lifecycle tests must compile without a precompiled header"
    })
}

$foundationPrivateAccessMacro = "GGLAB_FOUNDATION_PRIVATE_ACCESS"
function Test-ProjectPrivateAccessDefinition {
    param(
        [xml]$Project,
        [System.Xml.XmlNamespaceManager]$Namespace,
        [string]$ProjectPath,
        [bool]$Required
    )

    $definitionNodes = @($Project.SelectNodes(
        "//msb:ItemDefinitionGroup/msb:ClCompile/msb:PreprocessorDefinitions", $Namespace))
    if ($Required -and $definitionNodes.Count -eq 0) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-private-access"
            Target = $ProjectPath
            Reason = "Foundation project has no compile definitions for private access"
        })
    }
    foreach ($definitionNode in $definitionNodes) {
        $definitions = @(([string]$definitionNode.InnerText).Split(';') |
            ForEach-Object { $_.Trim() })
        $hasPrivateAccess = $definitions -contains $foundationPrivateAccessMacro
        if ($Required -and -not $hasPrivateAccess) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "foundation-private-access"
                Target = $ProjectPath
                Reason = "Foundation compile definitions must grant private-header access"
            })
        }
        elseif (-not $Required -and $hasPrivateAccess) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "foundation-private-access"
                Target = $ProjectPath
                Reason = "only GGLabFoundation may define $foundationPrivateAccessMacro"
            })
        }
    }
}

Test-ProjectPrivateAccessDefinition $foundationProject $foundationNamespace `
    "Projects/GGLabFoundation/GGLabFoundation.vcxproj" $true
Test-ProjectPrivateAccessDefinition $foundationTestsProject $foundationTestsNamespace `
    "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj" $false
Test-ProjectPrivateAccessDefinition $runtimeProject $namespace `
    "Projects/GGLabRuntime/GGLabRuntime.vcxproj" $false
Test-ProjectPrivateAccessDefinition $winAppProject $winAppNamespace `
    "Projects/WinApp/WinApp.vcxproj" $false
Test-ProjectPrivateAccessDefinition $vulkanQualificationProject `
    $vulkanQualificationNamespace `
    "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj" $false
Test-ProjectPrivateAccessDefinition $appRuntimeProject $appRuntimeNamespace `
    "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj" $false
Test-ProjectPrivateAccessDefinition $shaderArtifactRuntimeProject `
    $shaderArtifactRuntimeNamespace `
    "Projects/ShaderArtifactRuntime/ShaderArtifactRuntime.vcxproj" $false
Test-ProjectPrivateAccessDefinition $napaProject $napaNamespace `
    "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj" $false
Test-ProjectPrivateAccessDefinition $testCoreProject $testCoreNamespace `
    "Projects/GGLabTestCore/GGLabTestCore.vcxproj" $false
Test-ProjectPrivateAccessDefinition $runtimeTestsProject $runtimeTestsNamespace `
    "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj" $false
Test-ProjectPrivateAccessDefinition $shaderToolchainTestsProject `
    $shaderToolchainTestsNamespace `
    "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj" $false
Test-ProjectPrivateAccessDefinition $shaderRuntimeIntegrationTestsProject `
    $shaderRuntimeIntegrationTestsNamespace `
    "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj" $false
Test-ProjectPrivateAccessDefinition $appRuntimeTestsProject $appRuntimeTestsNamespace `
    "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj" $false

# Project-file encoding contract: every vcxproj and .filters file must be
# UTF-8 without BOM so generated and editor-authored files use one encoding.
foreach ($projectEncodingFile in @(
        Get-ChildItem -LiteralPath (Join-Path $root "Projects") -Recurse -File |
        Where-Object { $_.Extension -in @(".vcxproj", ".filters") })) {
    $projectEncodingBytes = [System.IO.File]::ReadAllBytes($projectEncodingFile.FullName)
    $projectEncodingHasBom = ($projectEncodingBytes.Length -ge 3 -and
        $projectEncodingBytes[0] -eq 0xEF -and $projectEncodingBytes[1] -eq 0xBB -and
        $projectEncodingBytes[2] -eq 0xBF)
    if ($projectEncodingHasBom) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-encoding"
            Target = ConvertTo-RepoRelativePath $projectEncodingFile.FullName
            Reason = "vcxproj/.filters file must be UTF-8 without BOM"
        })
    }
}

$firstPartySourceExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
$publicHeaderExtensions = @(".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
$ownershipSpecifications = @(
    [pscustomobject]@{
        Name       = "WinApp"
        SourceRoot = $winAppSourcesDir
        ItemPaths  = $winAppSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabVulkanQualification"
        SourceRoot = $vulkanQualificationSourcesDir
        ItemPaths  = $vulkanQualificationSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabAppRuntime"
        SourceRoot = $appRuntimeSourcesDir
        ItemPaths  = $appRuntimeSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabAppRuntimeTests"
        SourceRoot = $appRuntimeTestsDir
        ItemPaths  = $appRuntimeTestsSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabFoundation"
        SourceRoot = $foundationSourcesDir
        ItemPaths  = $foundationSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabFoundationTests"
        SourceRoot = $foundationTestsDir
        ItemPaths  = $foundationTestsSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabRuntime"
        SourceRoot = $runtimeSourcesDir
        ItemPaths  = $runtimeSourceItems
    }
    [pscustomobject]@{
        Name       = "ShaderArtifactRuntime"
        SourceRoot = $shaderArtifactRuntimeSourcesDir
        ItemPaths  = $shaderArtifactRuntimeSourceItems
    }
    [pscustomobject]@{
        Name       = "ShaderToolchainCore"
        SourceRoot = $shaderToolchainSourcesDir
        ItemPaths  = $shaderToolchainSourceItems
    }
    [pscustomobject]@{
        Name       = "ShaderCompiler"
        SourceRoot = $shaderCompilerSourcesDir
        ItemPaths  = $shaderCompilerSourceItems
    }
    [pscustomobject]@{
        Name       = "NapaVoxelCore"
        SourceRoot = $napaSourcesDir
        ItemPaths  = $napaSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabTestCore"
        SourceRoot = $testCoreSourcesDir
        ItemPaths  = $testCoreSourceItems
    }
    [pscustomobject]@{
        Name       = "GGLabRuntimeTests"
        SourceRoot = $runtimeTestsDir
        ItemPaths  = $runtimeTestsSourceItems
    }
    [pscustomobject]@{
        Name       = "ShaderToolchainTests"
        SourceRoot = $shaderToolchainTestsDir
        ItemPaths  = $shaderToolchainTestsSourceItems
    }
    [pscustomobject]@{
        Name       = "ShaderRuntimeIntegrationTests"
        SourceRoot = $shaderRuntimeIntegrationTestsDir
        ItemPaths  = $shaderRuntimeIntegrationTestsSourceItems
    }
    [pscustomobject]@{
        Name       = "NapaVoxelCoreTests"
        SourceRoot = $napaTestsDir
        ItemPaths  = $napaTestsSourceItems
    }
)

$firstPartyMemberships = @{}
foreach ($specification in $ownershipSpecifications) {
    if (-not (Test-Path -LiteralPath $specification.SourceRoot -PathType Container)) {
        throw "$($specification.Name) source root not found: $($specification.SourceRoot)"
    }

    $projectItemPathSet = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($itemPath in $specification.ItemPaths) {
        [void]$projectItemPathSet.Add($itemPath)

        $isFirstPartySource = (Test-IsPathUnderRoot $itemPath $repositorySourcesDir) -or
            (Test-IsPathUnderRoot $itemPath $repositoryTestsDir)
        if (-not $isFirstPartySource) {
            continue
        }

        $itemUnderOwnerRoot = Test-IsPathUnderRoot $itemPath $specification.SourceRoot
        if (-not $itemUnderOwnerRoot) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "source-ownership"
                Target = ConvertTo-RepoRelativePath $itemPath
                Reason = "$($specification.Name) project item is outside " +
                    (ConvertTo-RepoRelativePath $specification.SourceRoot)
            })
        }

        if (-not $firstPartyMemberships.ContainsKey($itemPath)) {
            $firstPartyMemberships[$itemPath] = New-Object 'System.Collections.Generic.List[string]'
        }
        [void]$firstPartyMemberships[$itemPath].Add($specification.Name)
    }

    $physicalOwnerFiles = @(
        Get-ChildItem -LiteralPath $specification.SourceRoot -Recurse -File |
            Where-Object { $_.Extension.ToLowerInvariant() -in $firstPartySourceExtensions }
    )
    foreach ($file in $physicalOwnerFiles) {
        if (-not $projectItemPathSet.Contains($file.FullName)) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "source-membership"
                Target = ConvertTo-RepoRelativePath $file.FullName
                Reason = "$($specification.Name)-owned source is missing from its project"
            })
        }
    }
}

$firstPartySourceFiles = @(
    Get-ChildItem -LiteralPath $repositorySourcesDir -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $firstPartySourceExtensions }
    Get-ChildItem -LiteralPath $repositoryTestsDir -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $firstPartySourceExtensions }
)
foreach ($file in $firstPartySourceFiles) {
    $matchingOwners = @(
        $ownershipSpecifications |
            Where-Object { Test-IsPathUnderRoot $file.FullName $_.SourceRoot }
    )
    if ($matchingOwners.Count -eq 0) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "source-ownership"
            Target = ConvertTo-RepoRelativePath $file.FullName
            Reason = "source is outside every recognized project owner root"
        })
    }
    elseif ($matchingOwners.Count -gt 1) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "source-ownership"
            Target = ConvertTo-RepoRelativePath $file.FullName
            Reason = "source matches multiple project owner roots"
        })
    }

    if ($firstPartyMemberships.ContainsKey($file.FullName) -and
        $firstPartyMemberships[$file.FullName].Count -gt 1) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "source-membership"
            Target = ConvertTo-RepoRelativePath $file.FullName
            Reason = "source is listed by multiple projects: " +
                ($firstPartyMemberships[$file.FullName] -join ", ")
        })
    }
}

if (-not $winAppProjectReferenceSet.Contains($runtimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing ProjectReference to GGLabRuntime"
    })
}
if (-not $winAppProjectReferenceSet.Contains($appRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing ProjectReference to GGLabAppRuntime"
    })
}
if (-not $winAppProjectReferenceSet.Contains($shaderArtifactRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing direct ProjectReference to ShaderArtifactRuntime"
    })
}
if ($winAppProjectReferenceSet.Contains($shaderToolchainProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "normal WinApp must not reference ShaderToolchainCore"
    })
}
$winAppProjectContent = Get-Content -LiteralPath $winAppProjectPath -Raw -ErrorAction Stop
if ($winAppProjectContent -match 'DxcImport\.(?:props|targets)' -or
    $winAppProjectContent -match 'Sources\\ShaderToolchain') {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "qualification-dependency-isolation"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "normal WinApp must not import DXC or expose Shader Toolchain include visibility"
    })
}
$winAppQualificationDispatchPaths = @(
    (Join-Path $winAppSourcesDir "Application/Main.cpp"),
    (Join-Path $winAppSourcesDir "Application/ApplicationLaunchOptions.h"),
    (Join-Path $winAppSourcesDir "Application/ApplicationLaunchOptions.cpp"),
    (Join-Path $winAppSourcesDir "Application/RenderingStartup.h"),
    (Join-Path $winAppSourcesDir "Application/RenderingStartup.cpp")
)
$winAppQualificationDispatchRegex = `
    '\bm_RunVulkanQualification\b|\bRunVulkanQualification\b|' +
    '#include\s*[<"][^>"]*VulkanQualification\.h[>"]'
foreach ($dispatchPath in $winAppQualificationDispatchPaths) {
    $dispatchContent = Get-Content -LiteralPath $dispatchPath -Raw -ErrorAction Stop
    if ($dispatchContent -match $winAppQualificationDispatchRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "qualification-dependency-isolation"
            Target = ConvertTo-RepoRelativePath $dispatchPath
            Reason = "WinApp must not retain standalone Vulkan qualification dispatch or implementation access"
        })
    }
}

$vulkanQualificationRequiredReferences = @(
    $runtimeProjectPath, $shaderToolchainProjectPath, $shaderArtifactRuntimeProjectPath,
    $foundationProjectPath, $testCoreProjectPath)
foreach ($requiredReference in $vulkanQualificationRequiredReferences) {
    if (-not $vulkanQualificationProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj"
            Reason = "missing privileged ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
foreach ($reference in $vulkanQualificationProjectReferenceSet) {
    if (-not $vulkanQualificationRequiredReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj"
            Reason = "qualification may reference only Runtime, Shader Toolchain, Artifact Runtime, Foundation and TestCore"
        })
    }
}
$vulkanQualificationProjectContent = Get-Content `
    -LiteralPath $vulkanQualificationProjectPath -Raw -ErrorAction Stop
if ($vulkanQualificationProjectContent -notmatch 'DxcImport\.props' -or
    $vulkanQualificationProjectContent -notmatch 'DxcImport\.targets' -or
    $vulkanQualificationProjectContent -notmatch 'VulkanImport\.props') {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "qualification-dependency-isolation"
        Target = "Projects/GGLabVulkanQualification/GGLabVulkanQualification.vcxproj"
        Reason = "qualification target must explicitly own its Vulkan and DXC build dependencies"
    })
}
$appRuntimeRequiredReferences = @(
    $runtimeProjectPath, $shaderArtifactRuntimeProjectPath, $foundationProjectPath)
foreach ($requiredReference in $appRuntimeRequiredReferences) {
    if (-not $appRuntimeProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
            Reason = "missing ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
foreach ($reference in $appRuntimeProjectReferenceSet) {
    if (-not $appRuntimeRequiredReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabAppRuntime/GGLabAppRuntime.vcxproj"
            Reason = "shared app runtime may reference only GGLabRuntime, ShaderArtifactRuntime and GGLabFoundation"
        })
    }
}
if ($runtimeProjectReferenceSet.Contains($shaderToolchainProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "artifact-only GGLabRuntime must not reference ShaderToolchainCore"
    })
}
if (-not $runtimeProjectReferenceSet.Contains($shaderArtifactRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "missing ProjectReference to ShaderArtifactRuntime"
    })
}
if (-not $shaderToolchainProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
        Reason = "missing ProjectReference to GGLabFoundation"
    })
}
if (-not $shaderToolchainProjectReferenceSet.Contains($shaderArtifactRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
        Reason = "missing ProjectReference to ShaderArtifactRuntime"
    })
}
if ($shaderToolchainProjectReferenceSet.Contains($runtimeProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($appRuntimeProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($winAppProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($napaProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($shaderCompilerProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
        Reason = "ShaderToolchainCore must not reference GGLabRuntime, GGLabAppRuntime, WinApp, NapaVoxelCore, or ShaderCompiler"
    })
}
if (-not $shaderArtifactRuntimeProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderArtifactRuntime/ShaderArtifactRuntime.vcxproj"
        Reason = "missing ProjectReference to GGLabFoundation"
    })
}
foreach ($reference in $shaderArtifactRuntimeProjectReferenceSet) {
    if ($reference -ne $foundationProjectPath) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderArtifactRuntime/ShaderArtifactRuntime.vcxproj"
            Reason = "ShaderArtifactRuntime may reference only GGLabFoundation"
        })
    }
}
if (-not $shaderCompilerProjectReferenceSet.Contains($shaderToolchainProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
        Reason = "missing ProjectReference to ShaderToolchainCore"
    })
}
if (-not $shaderCompilerProjectReferenceSet.Contains($shaderArtifactRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
        Reason = "missing ProjectReference to ShaderArtifactRuntime"
    })
}
foreach ($reference in $shaderCompilerProjectReferenceSet) {
    if ($reference -ne $shaderToolchainProjectPath -and
        $reference -ne $shaderArtifactRuntimeProjectPath) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
            Reason = "gglab-shaderc may reference only ShaderToolchainCore and ShaderArtifactRuntime"
        })
    }
}
if (-not $winAppProjectReferenceSet.Contains($napaProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing ProjectReference to NapaVoxelCore"
    })
}
if (-not $winAppProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing direct ProjectReference to GGLabFoundation"
    })
}
if (-not $winAppProjectReferenceSet.Contains($testCoreProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/WinApp/WinApp.vcxproj"
        Reason = "missing ProjectReference to GGLabTestCore"
    })
}
if ($testCoreProjectReferenceSet.Count -ne 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabTestCore/GGLabTestCore.vcxproj"
        Reason = "STL/CRT-only GGLabTestCore must not reference another first-party project"
    })
}
if (-not $runtimeProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "missing ProjectReference to GGLabFoundation"
    })
}
if ($runtimeProjectReferenceSet.Contains($winAppProjectPath) -or
    $runtimeProjectReferenceSet.Contains($appRuntimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "must not reference GGLabAppRuntime or WinApp"
    })
}
if ($foundationProjectReferenceSet.Count -ne 0) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabFoundation/GGLabFoundation.vcxproj"
        Reason = "Tier-0 Foundation must not reference another first-party project"
    })
}
if (-not $foundationTestsProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj"
        Reason = "missing ProjectReference to GGLabFoundation"
    })
}
foreach ($reference in $foundationTestsProjectReferenceSet) {
    if ($reference -ne $foundationProjectPath) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj"
            Reason = "link probe may reference only GGLabFoundation"
        })
    }
}
$directXTexProjectPath = Join-Path $root "Externals/Vender/DirectXTex/DirectXTex/DirectXTex_Desktop_2022_Win10.vcxproj"
$runtimeTestsRequiredReferences = @($runtimeProjectPath, $shaderArtifactRuntimeProjectPath,
    $foundationProjectPath, $testCoreProjectPath)
foreach ($requiredReference in $runtimeTestsRequiredReferences) {
    if (-not $runtimeTestsProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj"
            Reason = "missing ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
$runtimeTestsAllowedReferences = @($runtimeProjectPath, $foundationProjectPath,
    $testCoreProjectPath, $shaderArtifactRuntimeProjectPath, $directXTexProjectPath)
$shaderToolchainTestsRequiredReferences = @(
    $shaderToolchainProjectPath, $shaderArtifactRuntimeProjectPath,
    $shaderCompilerProjectPath, $foundationProjectPath, $testCoreProjectPath)
foreach ($requiredReference in $shaderToolchainTestsRequiredReferences) {
    if (-not $shaderToolchainTestsProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj"
            Reason = "missing ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
foreach ($reference in $shaderToolchainTestsProjectReferenceSet) {
    if (-not $shaderToolchainTestsRequiredReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj"
            Reason = "toolchain tests may reference only ShaderToolchainCore, ShaderArtifactRuntime, ShaderCompiler, GGLabFoundation and GGLabTestCore"
        })
    }
}
$shaderRuntimeIntegrationTestsRequiredReferences = @(
    $runtimeProjectPath, $shaderToolchainProjectPath, $shaderArtifactRuntimeProjectPath,
    $foundationProjectPath, $testCoreProjectPath)
foreach ($requiredReference in $shaderRuntimeIntegrationTestsRequiredReferences) {
    if (-not $shaderRuntimeIntegrationTestsProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj"
            Reason = "missing ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
foreach ($reference in $shaderRuntimeIntegrationTestsProjectReferenceSet) {
    if (-not $shaderRuntimeIntegrationTestsRequiredReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj"
            Reason = "shader/runtime integration tests may reference only their declared Runtime, Toolchain, artifact, Foundation and TestCore dependencies"
        })
    }
}
$shaderToolchainTestsProjectContent = Get-Content `
    -LiteralPath $shaderToolchainTestsProjectPath -Raw -ErrorAction Stop
if ($shaderToolchainTestsProjectContent -match 'GGLabRuntime' -or
    $shaderToolchainTestsProjectContent -match 'Sources\\GGLabRuntime' -or
    $shaderToolchainTestsProjectContent -match 'VulkanImport\.props' -or
    $shaderToolchainTestsProjectContent -match 'Microsoft\.Direct3D\.D3D12' -or
    $shaderToolchainTestsProjectContent -match 'WinPixEventRuntime' -or
    $shaderToolchainTestsProjectContent -match 'AssimpImport\.props' -or
    $shaderToolchainTestsProjectContent -match 'DirectXTex' -or
    $shaderToolchainTestsProjectContent -match 'D3D12MemoryAllocator' -or
    $shaderToolchainTestsProjectContent -match 'VulkanMemoryAllocator') {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "shader-test-dependency-isolation"
        Target = "Projects/ShaderToolchainTests/ShaderToolchainTests.vcxproj"
        Reason = "Toolchain-owned tests must not expose Runtime, RHI/backend, or Runtime-only vendored dependency closure"
    })
}
$shaderRuntimeIntegrationTestsProjectContent = Get-Content `
    -LiteralPath $shaderRuntimeIntegrationTestsProjectPath -Raw -ErrorAction Stop
if ($shaderRuntimeIntegrationTestsProjectContent -notmatch 'DxcImport\.props' -or
    $shaderRuntimeIntegrationTestsProjectContent -notmatch 'DxcImport\.targets' -or
    $shaderRuntimeIntegrationTestsProjectContent -notmatch 'VulkanImport\.props') {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "shader-test-dependency-isolation"
        Target = "Projects/ShaderRuntimeIntegrationTests/ShaderRuntimeIntegrationTests.vcxproj"
        Reason = "shader/runtime integration tests must explicitly own DXC and Vulkan SDK dependencies"
    })
}
$appRuntimeTestsRequiredReferences = @(
    $appRuntimeProjectPath, $runtimeProjectPath, $shaderArtifactRuntimeProjectPath,
    $foundationProjectPath, $testCoreProjectPath)
foreach ($requiredReference in $appRuntimeTestsRequiredReferences) {
    if (-not $appRuntimeTestsProjectReferenceSet.Contains($requiredReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj"
            Reason = "missing ProjectReference to " + (Split-Path -Leaf $requiredReference)
        })
    }
}
foreach ($reference in $appRuntimeTestsProjectReferenceSet) {
    if (-not $appRuntimeTestsRequiredReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabAppRuntimeTests/GGLabAppRuntimeTests.vcxproj"
            Reason = "lifecycle tests may reference only GGLabAppRuntime, GGLabRuntime, ShaderArtifactRuntime, GGLabFoundation and GGLabTestCore"
        })
    }
}
if (-not $napaTestsProjectReferenceSet.Contains($napaProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/NapaVoxelCoreTests/NapaVoxelCoreTests.vcxproj"
        Reason = "missing ProjectReference to NapaVoxelCore"
    })
}
foreach ($reference in $napaTestsProjectReferenceSet) {
    if ($reference -ne $napaProjectPath) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/NapaVoxelCoreTests/NapaVoxelCoreTests.vcxproj"
            Reason = "host-independent NapaVoxelCore tests may reference only NapaVoxelCore"
        })
    }
}
foreach ($reference in $runtimeTestsProjectReferenceSet) {
    if (-not $runtimeTestsAllowedReferences.Contains($reference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj"
            Reason = "may reference only Runtime/Test dependencies and the vendored DirectXTex project"
        })
    }
}
foreach ($forbiddenReference in @($winAppProjectPath, $appRuntimeProjectPath,
        $runtimeProjectPath, $foundationProjectPath)) {
    if ($napaProjectReferenceSet.Contains($forbiddenReference)) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj"
            Reason = "host-independent NapaVoxelCore must not reference first-party host projects"
        })
    }
}

$foundationForbiddenIncludeRegex = `
    '#include\s*[<"](?:\.\.|Sources[\\/]|Application[\\/]|DevTools[\\/]|Core[\\/]|' +
    'GGLabRuntime[\\/]|Scene[\\/]|Graphics[\\/]|Diagnostics[\\/]|Shader[\\/]|' +
    'ShaderToolchain[\\/]|Tools[\\/]|NapaVoxelCore[\\/])'
$runtimeTestsForbiddenIncludeRegex = `
    '#include\s*[<"](?:Application[\\/]|DevTools[\\/]|Artifact[\\/]|Compiler[\\/]|Targets[\\/]|DevelopmentShaderPaths\.h)'
$runtimeTestsForbiddenToolProcessRegex = `
    '\b(?:gglab-shaderc(?:\.exe)?|CreateProcess[AW]?|_w?spawn[a-z0-9_]*)\b'
$appRuntimeTestsForbiddenIncludeRegex = `
    '#include\s*[<"](?:Application[\\/]|DevTools[\\/]|NapaVoxelCore[\\/]|' +
    'ShaderToolchain[\\/]|Compiler[\\/])'
$appRuntimeForbiddenDependencyRegex = `
    'Windows[.]h|\bHWND\b|\bHINSTANCE\b|\bGameInput\b|\bIGameInput\b|' +
    '#include\s*[<"]GGLabFoundation[\\/]Platform[\\/]Win[\\/]|' +
    '#include\s*[<"](?:Application[\\/]|DevTools[\\/]|Compiler[\\/]|NapaVoxelCore[\\/])|' +
    '\bImGui\b|\bDevelopGui\w*\b|\bDXC\b|dxcapi[.]h|GetModuleFileName|' +
    'GetExecutableDirectory|GetExeOutDir|\bwin32::|\bLantern\b'
$appRuntimeWideTextRegex = '\bwchar_t\b|\bstd::wstring(?:_view)?\b'
$appRuntimePublicUpperIncludeRegex = `
    '#include\s*[<"](?:Application[\\/]|DevTools[\\/]|NapaVoxelCore[\\/]|' +
    'ShaderToolchain[\\/]|Compiler[\\/])'
$napaTestsForbiddenIncludeRegex = '#include\s*[<"](?:GGLab|Application[\\/]|DevTools[\\/]|Core[\\/]|Graphics[\\/]|Diagnostics[\\/]|Sources[\\/]|Shader[\\/])'
foreach ($itemPath in $napaTestsSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $napaTestsForbiddenIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "napa-tests-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "NapaVoxelCoreTests source includes a gglab or host-owned header"
        })
    }
}
foreach ($itemPath in $runtimeTestsSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $runtimeTestsForbiddenIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-tests-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "GGLabRuntimeTests source includes a host- or toolchain-owned header"
        })
    }
    if ($content -match $runtimeTestsForbiddenToolProcessRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-tests-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "artifact-only GGLabRuntimeTests must not invoke a tool process"
        })
    }
}
foreach ($itemPath in $appRuntimeTestsSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $appRuntimeTestsForbiddenIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "app-runtime-tests-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "GGLabAppRuntimeTests source includes a host, toolchain, or optional-content header"
        })
    }
}
foreach ($itemPath in $appRuntimeSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $appRuntimeForbiddenDependencyRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "app-runtime-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "shared app runtime contains a forbidden platform, host, tooling, compiler, or optional-content dependency"
        })
    }
    if ($extension -in $publicHeaderExtensions -and
        $content -match $appRuntimeWideTextRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "app-runtime-public-contract"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "portable public contract exposes unledgered wide-text API"
        })
    }
    if ($extension -in $publicHeaderExtensions -and
        $content -match $appRuntimePublicUpperIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "app-runtime-public-contract"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "portable public contract includes a host, tooling, compiler, or optional-content domain"
        })
    }
}

$applicationCoreContractPaths = @(
    (Join-Path $winAppSourcesDir "Application/Application.cpp"),
    (Join-Path $winAppSourcesDir "Application/Application.h")
)
$applicationCoreHostControlRegex = `
    '\bApplicationLaunchOptions\b|\bm_ListAdapters\b|' +
    '\bm_RunVulkanQualification\b|\bRunRenderingStartupPath\b'
foreach ($itemPath in $applicationCoreContractPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $applicationCoreHostControlRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-host-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "ordinary Application runtime contains host-only launch control"
        })
    }
}

$applicationContentFrameworkPaths = @(
	(Join-Path $winAppSourcesDir "Application/Application.cpp"),
	(Join-Path $winAppSourcesDir "Application/Demo/DemoLabHost.cpp")
)
$applicationConcreteContentRegex =
	'#include\s*[<"]Application[\\/]Lab[\\/]Sessions[\\/]'
foreach ($itemPath in $applicationContentFrameworkPaths) {
	$content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
	if ($content -match $applicationConcreteContentRegex) {
		$projectContractFindings.Add([pscustomobject]@{
			Rule   = "application-content-registration-boundary"
			Target = ConvertTo-RepoRelativePath $itemPath
			Reason = "shared Application framework depends on concrete Demo/Lab content"
		})
	}
}
$applicationConcreteDemoRegex =
	'#include\s*[<"]Application[\\/]Demo[\\/](?:DemoLabHost|DemoPlayground|StartDemo)[.]h'
$applicationCoreContent = Get-Content -LiteralPath $applicationCoreContractPaths[0] -Raw -ErrorAction Stop
if ($applicationCoreContent -match $applicationConcreteDemoRegex) {
	$projectContractFindings.Add([pscustomobject]@{
		Rule   = "application-content-registration-boundary"
		Target = ConvertTo-RepoRelativePath $applicationCoreContractPaths[0]
		Reason = "shared Application orchestration depends on a concrete Demo implementation"
	})
}

$applicationToolingBoundaryRegex =
    '#include\s*[<"]DevTools[\\/]|\bDevelopGuiSystem\b|\bDevelopGuiContext\b|\bImGui\w*\b'
foreach ($itemPath in $applicationCoreContractPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $applicationToolingBoundaryRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-tooling-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "common Application orchestration depends on concrete development tooling"
        })
    }
}

$toolingFrameContextPaths = @(
    (Join-Path $appRuntimeSourcesDir "ApplicationToolingIntegration.h"),
    (Join-Path $winAppSourcesDir "DevTools/DevelopGui/DevelopGuiContext.h")
)
$redundantToolingFrameMemberRegex =
    '\bm_(?:Camera|CameraController|AuthoringViewRenderProfile|' +
    'EffectiveViewRenderProfile|TemporalFramePlan)\b'
foreach ($itemPath in $toolingFrameContextPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $redundantToolingFrameMemberRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-tooling-context-surface"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "tooling frame contexts must not republish redundant camera bindings or unused frame-profile state"
        })
    }
}

$liveRenderGraphToolingMemberRegex = '\bm_RenderGraph\b'
foreach ($itemPath in $toolingFrameContextPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $liveRenderGraphToolingMemberRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-tooling-render-graph-surface"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "tooling frame contexts must observe RenderGraph state through immutable diagnostics snapshots"
        })
    }
}

$redundantToolingAliasRegex = '\bm_(?:MainRenderView|DirectionalShadowSettings)\b'
foreach ($itemPath in $toolingFrameContextPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $redundantToolingAliasRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-tooling-redundant-alias"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "tooling frame contexts must resolve main-view and directional-shadow state from their authoritative collections"
        })
    }
}

$rendererIndependentToolingPanelPaths = @(
    (Join-Path $developGuiPanelsDir "ForwardPlusInspectorPanel.cpp"),
    (Join-Path $developGuiPanelsDir "ForwardPlusInspectorPanel.h"),
    (Join-Path $developGuiPanelsDir "ProfilingPanel.cpp"),
    (Join-Path $developGuiPanelsDir "ProfilingPanel.h"),
    (Join-Path $developGuiPanelsDir "PersistentSceneBuffersPanel.cpp"),
    (Join-Path $developGuiPanelsDir "PersistentSceneBuffersPanel.h"),
    (Join-Path $developGuiPanelsDir "PipelineSystemPanel.cpp"),
    (Join-Path $developGuiPanelsDir "PipelineSystemPanel.h"),
    (Join-Path $developGuiPanelsDir "TransientResourcePoolPanel.cpp"),
    (Join-Path $developGuiPanelsDir "TransientResourcePoolPanel.h")
)
foreach ($itemPath in $rendererIndependentToolingPanelPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match '\bRenderer\b|\bm_Renderer\b') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "tooling-panel-renderer-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "panels migrated to snapshots or narrow capabilities must not retain a parallel live Renderer dependency"
        })
    }
}

$applicationFrameOrchestrationRegex =
    '\bRenderFrameBuilder\b|\bRenderGraph\b|\bDebugDrawSystem\b|' +
    '\bShaderPreloadStatus\b|\bPumpCompletions\s*\(|\bDrainLoadCompletions\s*\(|' +
    '\bTickTransitions\s*\(|\bBeginFrame\s*\(|\bBuildRenderGraph\s*\(|' +
    '\bEndFrame\s*\(|\bOnFrameSubmitted\s*\('
foreach ($itemPath in $applicationCoreContractPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $applicationFrameOrchestrationRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-frame-orchestration-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "Windows Application shell contains shared production frame orchestration"
        })
    }
}

$applicationRuntimeLifecycleRegex =
    '\bm_IsSuspended\b|GetRenderer\s*\(\s*\)\s*->\s*On(?:Suspend|Resume|Resize)\s*\('
foreach ($itemPath in $applicationCoreContractPaths) {
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $applicationRuntimeLifecycleRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-runtime-lifecycle-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "Windows Application shell duplicates shared runtime lifecycle state or renderer control"
        })
    }
}

$presentationContractChecks = @(
    [pscustomobject]@{
        Path = Join-Path $runtimePublicDir "GGLabRuntime/Graphics/RHI/RHIContext.h"
        Pattern = '\bm_WindowHandle\b|\bm_Backend\s*='
        Reason = "portable RHI context descriptor contains backend or native-window composition state"
    },
    [pscustomobject]@{
        Path = Join-Path $runtimeSourcesDir "Graphics/RHI/Vulkan/VulkanContext.cpp"
        Pattern = '\bCreateVulkanPlatformSurfaceFactory\b'
        Reason = "common Vulkan context selects a platform surface factory through build convention"
    },
    [pscustomobject]@{
        Path = Join-Path $runtimeSourcesDir "Graphics/RHI/Vulkan/VulkanDeviceProfile.h"
        Pattern = '\bm_IsWindowsX64\b|\bm_HasVulkanLoader\b|\bm_HasWin32SurfaceExtension\b|\bWin32SurfaceExtensionUnavailable\b'
        Reason = "core Vulkan device profile contains host ABI, loader, or Win32 WSI policy"
    }
)
foreach ($check in $presentationContractChecks) {
    $content = Get-Content -LiteralPath $check.Path -Raw -ErrorAction Stop
    if ($content -match $check.Pattern) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "rhi-presentation-boundary"
            Target = ConvertTo-RepoRelativePath $check.Path
            Reason = $check.Reason
        })
    }
}

$pathDiscoveryAllowed = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($allowedPath in @(
        (Join-Path $winAppSourcesDir "Application/Main.cpp"),
        (Join-Path $winAppSourcesDir "Application/Platform/Windows/Win32PathUtils.cpp"),
        (Join-Path $winAppSourcesDir "Application/Platform/Windows/Win32PathUtils.h"))) {
    [void]$pathDiscoveryAllowed.Add([System.IO.Path]::GetFullPath($allowedPath))
}
foreach ($itemPath in $winAppSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }

    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match '\bSetCurrentDirectory(?:A|W)?\b') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-runtime-paths"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "Application mutates the process current working directory"
        })
    }
    if ($content -match '\bGetExecutableDirectory\b' -and
        -not $pathDiscoveryAllowed.Contains([System.IO.Path]::GetFullPath($itemPath))) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "application-runtime-paths"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "executable-directory discovery escaped the Windows composition root"
        })
    }
}
foreach ($itemPath in $foundationSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }

    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match $foundationForbiddenIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-boundary"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "Foundation source includes an upper first-party domain"
        })
    }
}

$foundationPrivateHeaders = @(Get-ChildItem -LiteralPath $foundationPrivateDir -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in $publicHeaderExtensions })
foreach ($privateHeader in $foundationPrivateHeaders) {
    $content = Get-Content -LiteralPath $privateHeader.FullName -Raw -ErrorAction Stop
    $hasInlineAccessGuard = $content -match
        '#if\s+!defined\s*\(\s*GGLAB_FOUNDATION_PRIVATE_ACCESS\s*\)'
    $includesSharedAccessGuard = $content -match
        '#include\s*"FoundationPrivateAccess\.h"'
    if (-not $hasInlineAccessGuard -and -not $includesSharedAccessGuard) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-private-access"
            Target = ConvertTo-RepoRelativePath $privateHeader.FullName
            Reason = "private header lacks the Foundation-only compile access guard"
        })
    }
}

$nonFoundationSourceItems = @($winAppSourceItems + $appRuntimeSourceItems +
    $appRuntimeTestsSourceItems + $runtimeSourceItems +
    $foundationTestsSourceItems + $napaSourceItems + $testCoreSourceItems +
    $runtimeTestsSourceItems + $napaTestsSourceItems + $shaderToolchainSourceItems +
    $shaderCompilerSourceItems)
foreach ($itemPath in $nonFoundationSourceItems) {
    $extension = [System.IO.Path]::GetExtension($itemPath).ToLowerInvariant()
    if ($extension -notin $firstPartySourceExtensions) {
        continue
    }
    $content = Get-Content -LiteralPath $itemPath -Raw -ErrorAction Stop
    if ($content -match '\bGGLAB_FOUNDATION_PRIVATE_ACCESS\b' -or
        $content -match '#include\s*[<"]GGLabFoundation[\\/]Private[\\/]') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-private-access"
            Target = ConvertTo-RepoRelativePath $itemPath
            Reason = "non-Foundation source attempts to consume Foundation Private content"
        })
    }
}

$shaderCompilerProbePaths = @(
    (Join-Path $shaderToolchainSourcesDir "Compiler/ShaderCompiler.h"),
    (Join-Path $shaderToolchainSourcesDir "Compiler/ShaderCompiler.cpp")
)
foreach ($shaderCompilerPath in $shaderCompilerProbePaths) {
    if (-not (Test-Path -LiteralPath $shaderCompilerPath -PathType Leaf)) {
        throw "ShaderCompiler consumer probe source not found: $shaderCompilerPath"
    }
    $shaderCompilerContent = Get-Content -LiteralPath $shaderCompilerPath -Raw -ErrorAction Stop
    if ($shaderCompilerContent -match '#include\s*"Core[\\/]') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-consumer"
            Target = ConvertTo-RepoRelativePath $shaderCompilerPath
            Reason = "ShaderCompiler foundational dependencies must not come from GGLabRuntime/Core"
        })
    }
}

# Shader Toolchain contract headers must not depend on the Graphics runtime;
# the runtime may consume contract vocabulary, never the other way around.
if (-not (Test-Path -LiteralPath $shaderToolchainSourcesDir -PathType Container)) {
    throw "Shader Toolchain contracts root not found: $shaderToolchainSourcesDir"
}
Get-ChildItem -LiteralPath $shaderToolchainSourcesDir -Recurse -File |
    Where-Object { $_.Extension -eq ".h" } |
    ForEach-Object {
        $toolchainContractContent = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction Stop
        if ($toolchainContractContent -match '#include\s*"Graphics[\\/]') {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "toolchain-contract-direction"
                Target = ConvertTo-RepoRelativePath $_.FullName
                Reason = "Shader Toolchain contract headers must not depend on the Graphics runtime"
            })
        }
    }

# Runtime-facing shader demand contracts expose stable Program/Artifact identity
# only. Build descriptions and compiler policy remain host/toolchain-owned.
$shaderRuntimeIdentityBoundaryPaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/Shader/Shader.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Shader/ShaderManager.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Shader/ShaderProgramCatalog.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Asset/DerivedData/IBLDerivedDataSystem.h"),
    (Join-Path $appRuntimeSourcesDir "RuntimePaths.h"),
    (Join-Path $appRuntimeSourcesDir "ApplicationContentRegistration.h"),
    (Join-Path $appRuntimeSourcesDir "GGLabAppRuntime.h"),
    (Join-Path $appRuntimeSourcesDir "Lab/LabCatalog.h")
)
$shaderRuntimeBuildVocabularyRegex =
    '\bShaderDesc\b|\bShaderResolvedRecipe\b|\bm_SourcePath\b|\bm_Entry\b|' +
    '\bm_Defines\b|\bm_IncludeDirs\b|\bm_ShaderSourceRoot\b|\bm_ShaderCacheRoot\b'
foreach ($boundaryPath in $shaderRuntimeIdentityBoundaryPaths) {
    $boundaryContent = Get-Content -LiteralPath $boundaryPath -Raw -ErrorAction Stop
    if ($boundaryContent -match $shaderRuntimeBuildVocabularyRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "shader-runtime-identity-boundary"
            Target = ConvertTo-RepoRelativePath $boundaryPath
            Reason = "runtime-facing shader contract exposes source or compiler vocabulary"
        })
    }
}

# The WinApp development bridge may coordinate the standalone shader compiler
# process and consume Artifact Runtime records, but it must not reintroduce the
# in-process Toolchain APIs removed by the extraction.
$developmentShaderBridgePaths = @(
    (Join-Path $winAppSourcesDir "Application/Shader/DevelopmentShaderBuildBridge.h"),
    (Join-Path $winAppSourcesDir "Application/Shader/DevelopmentShaderBuildBridge.cpp")
)
$developmentShaderBridgeToolchainRegex =
    '#include\s*[<"](?:Artifact|Compiler|Targets)[\/]|' +
    '\bShaderDesc\b|\bShaderResolvedRecipe\b|\bShaderCompiler\b|' +
    '\bPublishShaderRuntimeArtifact\b|\bPublishShaderProgramRegistryArtifact\b'
foreach ($bridgePath in $developmentShaderBridgePaths) {
    if (-not (Test-Path -LiteralPath $bridgePath -PathType Leaf)) {
        throw "Development shader process bridge source not found: $bridgePath"
    }
    $bridgeContent = Get-Content -LiteralPath $bridgePath -Raw -ErrorAction Stop
    if ($bridgeContent -match $developmentShaderBridgeToolchainRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "shader-development-process-boundary"
            Target = ConvertTo-RepoRelativePath $bridgePath
            Reason = "WinApp development bridge must coordinate gglab-shaderc without consuming in-process Shader Toolchain APIs"
        })
    }
}

$logicalIncludeSpecifications = @(
    [pscustomobject]@{
        Name        = "WinApp"
        ScanRoot    = $winAppSourcesDir
        LogicalRoot = $winAppSourcesDir
    }
    [pscustomobject]@{
        Name        = "GGLabFoundation"
        ScanRoot    = $foundationPublicDir
        LogicalRoot = $foundationPublicDir
    }
    [pscustomobject]@{
        Name        = "GGLabAppRuntime"
        ScanRoot    = $appRuntimeSourcesDir
        LogicalRoot = $appRuntimeSourcesDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntimePublic"
        ScanRoot    = $runtimePublicDir
        LogicalRoot = $runtimePublicDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntimePrivate"
        ScanRoot    = $runtimePrivateDir
        LogicalRoot = $runtimePrivateDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntimeGraphicsLegacy"
        ScanRoot    = Join-Path $runtimeSourcesDir "Graphics"
        LogicalRoot = $runtimeSourcesDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntimeSceneLegacy"
        ScanRoot    = Join-Path $runtimeSourcesDir "Scene"
        LogicalRoot = $runtimeSourcesDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntimeDiagnosticsLegacy"
        ScanRoot    = Join-Path $runtimeSourcesDir "Diagnostics"
        LogicalRoot = $runtimeSourcesDir
    }
    [pscustomobject]@{
        Name        = "ShaderToolchain"
        ScanRoot    = $shaderToolchainSourcesDir
        LogicalRoot = $shaderToolchainSourcesDir
    }
    [pscustomobject]@{
        Name        = "ShaderCompiler"
        ScanRoot    = $shaderCompilerSourcesDir
        LogicalRoot = $shaderCompilerSourcesDir
    }
    [pscustomobject]@{
        Name        = "NapaVoxelCore"
        ScanRoot    = $napaPublicDir
        LogicalRoot = $napaPublicDir
    }
)
$logicalIncludes = @{}
foreach ($specification in $logicalIncludeSpecifications) {
    $headers = if (Test-Path -LiteralPath $specification.ScanRoot -PathType Container) {
        Get-ChildItem -LiteralPath $specification.ScanRoot -Recurse -File |
            Where-Object { $_.Extension.ToLowerInvariant() -in $publicHeaderExtensions }
    }
    else {
        @()
    }
    foreach ($header in $headers) {
        $logicalPath = $header.FullName.Substring($specification.LogicalRoot.Length + 1).
            Replace('\', '/')
        $requiredLogicalPrefix = if ($specification.Name -eq "GGLabFoundation") {
            "GGLabFoundation/"
        }
        elseif ($specification.Name -eq "GGLabRuntimePublic") {
            "GGLabRuntime/"
        }
        elseif ($specification.Name -eq "NapaVoxelCore") {
            "NapaVoxelCore/"
        }
        else {
            ""
        }
        if (-not [string]::IsNullOrWhiteSpace($requiredLogicalPrefix) -and
            -not $logicalPath.StartsWith($requiredLogicalPrefix,
                [System.StringComparison]::Ordinal)) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "logical-include"
                Target = ConvertTo-RepoRelativePath $header.FullName
                Reason = "$($specification.Name) public include path lacks the $requiredLogicalPrefix prefix"
            })
        }

        $logicalKey = $logicalPath.ToLowerInvariant()
        if ($logicalIncludes.ContainsKey($logicalKey)) {
            $existing = $logicalIncludes[$logicalKey]
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "logical-include"
                Target = $logicalPath
                Reason = "public include collision between $($existing.Owner) and " +
                    $specification.Name
            })
        }
        else {
            $logicalIncludes[$logicalKey] = [pscustomobject]@{
                Owner    = $specification.Name
                FullPath = $header.FullName
            }
        }
    }
}

$runtimePublicIncludeRegex =
    '#include\s*[<"](?<Path>GGLabRuntime(?:/|\\)[^>"]+)[>"]'
$runtimePrivateIncludeRegex =
    '#include\s*[<"](?:(?:Core|Graphics|Scene|Diagnostics)(?:/|\\)|' +
    '(?:\.\.(?:/|\\))+(?:Private|Core)(?:/|\\))'
foreach ($header in Get-ChildItem -LiteralPath $runtimePublicDir -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $publicHeaderExtensions }) {
    $content = Get-Content -LiteralPath $header.FullName -Raw -ErrorAction Stop
    if ($content -match $runtimePrivateIncludeRegex) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "runtime-public-closure"
            Target = ConvertTo-RepoRelativePath $header.FullName
            Reason = "Runtime Public header includes Runtime Private implementation"
        })
    }
    foreach ($match in [regex]::Matches($content, $runtimePublicIncludeRegex)) {
        $logicalPath = $match.Groups["Path"].Value.Replace('\', '/')
        $logicalKey = $logicalPath.ToLowerInvariant()
        if (-not $logicalIncludes.ContainsKey($logicalKey) -or
            $logicalIncludes[$logicalKey].Owner -ne "GGLabRuntimePublic") {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "runtime-public-closure"
                Target = ConvertTo-RepoRelativePath $header.FullName
                Reason = "Runtime Public header includes a non-Public Runtime header: $logicalPath"
            })
        }
    }
}

$napaPublicIncludeRegex = '#include\s*[<"](?<Path>NapaVoxelCore(?:/|\\)[^>"]+)[>"]'
foreach ($header in Get-ChildItem -LiteralPath $napaPublicDir -Recurse -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in $publicHeaderExtensions }) {
    $content = Get-Content -LiteralPath $header.FullName -Raw -ErrorAction Stop
    foreach ($match in [regex]::Matches($content, $napaPublicIncludeRegex)) {
        $logicalPath = $match.Groups["Path"].Value.Replace('\', '/')
        $logicalKey = $logicalPath.ToLowerInvariant()
        if (-not $logicalIncludes.ContainsKey($logicalKey) -or
            $logicalIncludes[$logicalKey].Owner -ne "NapaVoxelCore") {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "napa-public-closure"
                Target = ConvertTo-RepoRelativePath $header.FullName
                Reason = "Napa Public header includes a non-Public Napa header: $logicalPath"
            })
        }
    }
}

$ownershipFindings = New-Object System.Collections.Generic.List[object]
$platformFindings = New-Object System.Collections.Generic.List[object]
$runtimeOwnedPathSet = New-Object System.Collections.Generic.HashSet[string]
foreach ($file in $runtimeOwnedFiles) {
    [void]$runtimeOwnedPathSet.Add($file.Path)
}

foreach ($file in $runtimeOwnedFiles) {
    $content = Get-Content -LiteralPath $file.FullPath -Raw -ErrorAction Stop

    if ($file.Path.StartsWith("Application/") -or $file.Path.StartsWith("DevTools/") -or
        $content -match $ownershipIncludeRegex -or
        $content -match $ownershipSymbolRegex) {
        $ownershipFindings.Add([pscustomobject]@{ File = $file.Path; Kind = "ownership" })
    }
}

foreach ($file in $candidateFiles) {
    if ($file.Path.StartsWith("Diagnostics/") -and -not $runtimeOwnedPathSet.Contains($file.Path)) {
        $ownershipFindings.Add([pscustomobject]@{ File = $file.Path; Kind = "ownership" })
    }
}

foreach ($file in $candidateFiles) {
    $content = Get-Content -LiteralPath $file.FullPath -Raw -ErrorAction Stop
    # Platform leakage (skip exempt regions; transitional debt is scanned and
    # matched below). Covers raw Win32 tokens, direct includes of platform
    # leaves, and the win32 namespace leaking into portable files.
    if (-not (Test-IsExempt $file.Path)) {
        $platformLeaked = $content -match $platformLeakRegex -or
            $content -match $platformLeafIncludeRegex -or
            $content -match $platformNamespaceRegex -or
            $content -match $platformTypeRegex
        if ($platformLeaked) {
            $platformFindings.Add([pscustomobject]@{ File = $file.Path; Kind = "platform" })
        }
    }
}

# Classify actual findings against the ledger and the transitional debt list.
$unexpected = @()
$knownFound = @()
$debtFound = @()
foreach ($finding in ($ownershipFindings + $platformFindings)) {
    if (Test-IsKnown $finding.File $finding.Kind) {
        $knownFound += $finding
    }
    elseif ($finding.Kind -eq "platform" -and (Test-IsTransitionalDebt $finding.File)) {
        $debtFound += $finding
    }
    else {
        $unexpected += $finding
    }
}

# Stale ledger entries: known entry whose violation is no longer present.
# Transitional debt participates in the same lifecycle: a debt entry whose
# violation disappeared (or whose file is gone) is stale and fails.
$stale = @()
foreach ($entry in $knownViolations) {
    $matched = $false
    foreach ($finding in $knownFound) {
        if ($finding.File -eq $entry.File -and $finding.Kind -eq $entry.Kind) {
            $matched = $true
            break
        }
    }
    if (-not $matched) {
        $fileExists = Test-Path (Join-Path $runtimeSourcesDir $entry.File)
        $stale += [pscustomobject]@{
            File   = $entry.File
            Kind   = $entry.Kind
            Reason = if ($fileExists) { "violation resolved" } else { "file no longer exists" }
        }
    }
}
foreach ($debt in $transitionalDebt) {
    $matched = $false
    foreach ($finding in $debtFound) {
        if ($finding.File -eq $debt.File) {
            $matched = $true
            break
        }
    }
    if (-not $matched) {
        $fileExists = Test-Path (Join-Path $runtimeSourcesDir $debt.File)
        $stale += [pscustomobject]@{
            File   = $debt.File
            Kind   = "debt"
            Reason = if ($fileExists) { "transitional debt resolved" } else { "file no longer exists" }
        }
    }
}

# Report.
Write-Host "=== Project Ownership and Runtime Boundary Validation ==="
Write-Host "Root: $root"
Write-Host "Physical ownership: $($firstPartySourceFiles.Count) first-party source files"
Write-Host (("Project items: {0} WinApp, {1} VulkanQualification, " +
    "{2} AppRuntime, {3} AppRuntimeTests, {4} Foundation, " +
    "{5} FoundationTests, {6} GGLabRuntime, {7} ShaderArtifactRuntime, " +
    "{8} NapaVoxelCore, {9} TestCore, {10} RuntimeTests, " +
    "{11} ShaderToolchainTests, {12} ShaderRuntimeIntegrationTests, " +
    "{13} NapaTests") -f `
        $winAppSourceItems.Count, $vulkanQualificationSourceItems.Count,
        $appRuntimeSourceItems.Count, $appRuntimeTestsSourceItems.Count,
        $foundationSourceItems.Count, $foundationTestsSourceItems.Count,
        $runtimeSourceItems.Count, $shaderArtifactRuntimeSourceItems.Count,
        $napaSourceItems.Count, $testCoreSourceItems.Count,
        $runtimeTestsSourceItems.Count, $shaderToolchainTestsSourceItems.Count,
        $shaderRuntimeIntegrationTestsSourceItems.Count, $napaTestsSourceItems.Count)
Write-Host "Platform: $($candidateFiles.Count) candidate files (Graphics/Diagnostics)"
Write-Host (("Compile items: {0} WinApp, {1} VulkanQualification, " +
    "{2} AppRuntime, {3} AppRuntimeTests, {4} Foundation, " +
    "{5} FoundationTests, {6} GGLabRuntime, {7} ShaderArtifactRuntime, " +
    "{8} NapaVoxelCore, {9} TestCore, {10} RuntimeTests, " +
    "{11} ShaderToolchainTests, {12} ShaderRuntimeIntegrationTests, " +
    "{13} NapaTests") -f `
        $winAppCompileFiles.Count, $vulkanQualificationCompileFiles.Count,
        $appRuntimeCompileFiles.Count, $appRuntimeTestsCompileFiles.Count,
        $foundationCompileFiles.Count, $foundationTestsCompileFiles.Count,
        $runtimeCompileFiles.Count, $shaderArtifactRuntimeCompileFiles.Count,
        $napaCompileFiles.Count, $testCoreCompileFiles.Count,
        $runtimeTestsCompileFiles.Count, $shaderToolchainTestsCompileFiles.Count,
        $shaderRuntimeIntegrationTestsCompileFiles.Count, $napaTestsCompileFiles.Count)
Write-Host ""

Write-Host "PROJECT CONTRACT violations: $($projectContractFindings.Count)"
foreach ($finding in $projectContractFindings) {
    Write-Host ("  [{0}] {1} ({2})" -f $finding.Rule, $finding.Target, $finding.Reason)
}
Write-Host ""

Write-Host "KNOWN violations (ledger, enumerated): $($knownFound.Count)"
foreach ($finding in $knownFound) {
    $entry = $knownViolations | Where-Object { $_.File -eq $finding.File -and $_.Kind -eq $finding.Kind } | Select-Object -First 1
    Write-Host ("  [{0}] {1}" -f $finding.Kind, $finding.File)
    if ($ShowAll -and $entry) {
        Write-Host ("        -> {0}" -f $entry.Reason)
    }
}
Write-Host ""

Write-Host "TRANSITIONAL DEBT (scanned, matched): $($debtFound.Count)"
foreach ($finding in $debtFound) {
    $entry = $transitionalDebt | Where-Object { $_.File -eq $finding.File } | Select-Object -First 1
    Write-Host ("  [platform] {0}" -f $finding.File)
    if ($ShowAll -and $entry) {
        Write-Host ("        -> {0}" -f $entry.Reason)
    }
}
Write-Host ""

Write-Host "UNEXPECTED violations (not in ledger): $($unexpected.Count)"
foreach ($finding in $unexpected) {
    Write-Host ("  [{0}] {1}" -f $finding.Kind, $finding.File)
}
Write-Host ""

Write-Host "STALE ledger entries (no longer violating): $($stale.Count)"
foreach ($entry in $stale) {
    Write-Host ("  [{0}] {1} ({2})" -f $entry.Kind, $entry.File, $entry.Reason)
}
Write-Host ""

if ($projectContractFindings.Count -gt 0 -or $unexpected.Count -gt 0 -or $stale.Count -gt 0) {
    if ($projectContractFindings.Count -gt 0) {
        Write-Host "RESULT: FAIL - project graph or source ownership violations found."
    }
    elseif ($unexpected.Count -gt 0) {
        Write-Host "RESULT: FAIL - unexpected boundary violations found."
    }
    else {
        Write-Host "RESULT: FAIL - stale ledger entries must be removed or updated."
    }
    exit 1
}

Write-Host "RESULT: PASS - no unexpected boundary violations."
exit 0
