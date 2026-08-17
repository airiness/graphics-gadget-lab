param(
    [string]$RootDir = "",
    [switch]$ShowAll
)

# Project ownership and first-party boundary validation.
# Enforces the first-party source ownership and dependency direction contracts:
#   Project graph - Application must reference GGLabRuntime and NapaVoxelCore;
#                   Foundation must remain Tier-0; its tests may reference only
#                   Foundation; NapaVoxelCore remains an independent sibling.
#   Source ownership - every first-party source item must live below its owning
#                      project's source root, and every physical source file must
#                      belong to exactly one owning project.
#   Include visibility - Foundation sees only its Public/Private roots, while its
#                        tests see only the Foundation Public root.
#   Include identity - public logical include paths must be unique across owners.
#   Foundation boundary - Foundation must not include any upper first-party domain.
#   Foundation private access - every private header is compiler-gated to the
#                               Foundation project even when a broad consumer
#                               include root can physically resolve its path.
#   Public header closure - every Foundation Public header must compile in its
#                           own translation unit without aggregate include help.
#   Foundation consumers - ShaderCompiler foundational dependencies must come
#                          from Foundation rather than Runtime Core infrastructure.
#   Ownership boundary - runtime candidates must not include Application/*,
#                        DevTools/*, or the Application-owned Core/Input/*.
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
$applicationSourcesDir = Join-Path $root "Sources/Application"
$foundationSourcesDir = Join-Path $root "Sources/GGLabFoundation"
$foundationPublicDir = Join-Path $foundationSourcesDir "Public"
$foundationPrivateDir = Join-Path $foundationSourcesDir "Private"
$testCoreSourcesDir = Join-Path $root "Sources/GGLabTestCore"
$foundationTestsDir = Join-Path $root "Tests/GGLabFoundation"
$runtimeTestsDir = Join-Path $root "Tests/GGLabRuntime"
$napaTestsDir = Join-Path $root "Tests/NapaVoxelCore"
$runtimeSourcesDir = Join-Path $root "Sources/GGLabRuntime"
$shaderToolchainSourcesDir = Join-Path $root "Sources/ShaderToolchain"
$shaderCompilerSourcesDir = Join-Path $root "Sources/Tools/ShaderCompiler"
$napaSourcesDir = Join-Path $root "Sources/NapaVoxelCore"

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

# Candidate runtime directories (portable runtime candidates; backend leaves included).
$candidateDirs = @("Core", "Scene", "Graphics", "Diagnostics")

# Platform / backend leaf allowlists.
# Permanent leaves are reviewed and need no removal condition.
$platformLeafPrefixes = @(
    "Core/Platform/Win", # Windows implementation leaves
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
$knownViolations = @(
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanQualification.cpp";                   Kind = "platform";  Reason = "Win32 window-control behavior; planned: decide leaf vs host ownership" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanQualification.h";                     Kind = "platform";  Reason = "Windows.h/HWND; same family as VulkanQualification.cpp; planned: decide leaf vs host ownership" },
    [pscustomobject]@{ File = "Graphics/Shader/ShaderManager.cpp";                             Kind = "platform";  Reason = "Windows.h/IsDebuggerPresent debug-flag policy; planned: host-injected debug policy seam" },
    [pscustomobject]@{ File = "Graphics/Asset/DerivedData/LocalDerivedDataMaintenanceLock.cpp"; Kind = "platform";  Reason = "Platform mutex implementation in portable cpp; root identity carries Windows named-mutex name semantics; planned: narrow platform lock leaf" },
    [pscustomobject]@{ File = "Graphics/Asset/DerivedData/LocalDerivedDataStore.cpp";              Kind = "platform";  Reason = "Platform process/lock utilities used by portable cpp; planned: narrow DDC platform leaf" }
)

$ownershipIncludeRegex = '#include\s*"(Application|DevTools|Core/Input)/'
$ownershipSymbolRegex = '\bDevelopGuiSystem\b'
$platformLeakRegex = 'Windows\.h|GameInput|IGameInput|\bHWND\b|\bHMODULE\b|\bHRESULT\b|CoInitializeEx|SetThreadDescription|MultiByteToWideChar|WideCharToMultiByte|GetModuleFileName|CreateSymbolicLink|GetCurrentProcessId|GetTickCount64|GetExeOutDir|bcrypt\.h'
$platformLeafIncludeRegex = '#include\s*"(Core/Platform/Win/|Graphics/RHI/Vulkan/VulkanWin32Surface\.h)'
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

$applicationProjectPath = Join-Path $root "Projects/Application/Application.vcxproj"
if (-not (Test-Path $applicationProjectPath)) {
    throw "Application project not found: $applicationProjectPath"
}
$applicationProject = [xml](Get-Content -LiteralPath $applicationProjectPath -Raw -ErrorAction Stop)
$applicationNamespace = New-Object System.Xml.XmlNamespaceManager($applicationProject.NameTable)
$applicationNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$applicationProjectDir = Split-Path -Parent $applicationProjectPath

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
$applicationCompileFiles = Get-ProjectItemPaths $applicationProject $applicationNamespace `
    $applicationProjectDir "//msb:ClCompile" "Application compile item"
$foundationCompileFiles = Get-ProjectItemPaths $foundationProject $foundationNamespace `
    $foundationProjectDir "//msb:ClCompile" "GGLabFoundation compile item"
$foundationTestsCompileFiles = Get-ProjectItemPaths `
    $foundationTestsProject $foundationTestsNamespace $foundationTestsProjectDir `
    "//msb:ClCompile" "GGLabFoundationTests compile item"
$napaCompileFiles = Get-ProjectItemPaths $napaProject $napaNamespace $napaProjectDir `
    "//msb:ClCompile" "NapaVoxelCore compile item"
$runtimeSourceItems = Get-ProjectItemPaths $runtimeProject $namespace $runtimeProjectDir `
    "//msb:ClCompile | //msb:ClInclude" "Runtime source item"
$applicationSourceItems = Get-ProjectItemPaths $applicationProject $applicationNamespace `
    $applicationProjectDir "//msb:ClCompile | //msb:ClInclude" "Application source item"
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
$applicationProjectReferences = Get-ProjectItemPaths $applicationProject $applicationNamespace `
    $applicationProjectDir "//msb:ProjectReference" "Application project reference"
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
$applicationProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
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
$napaTestsProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderToolchainProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$shaderCompilerProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in $runtimeProjectReferences) {
    [void]$runtimeProjectReferenceSet.Add($path)
}
foreach ($path in $applicationProjectReferences) {
    [void]$applicationProjectReferenceSet.Add($path)
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
foreach ($path in $napaTestsProjectReferences) {
    [void]$napaTestsProjectReferenceSet.Add($path)
}
foreach ($path in $shaderToolchainProjectReferences) {
    [void]$shaderToolchainProjectReferenceSet.Add($path)
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
            if ($includeRoot.StartsWith('$(GGLabRepositoryRoot)Sources',
                    [System.StringComparison]::OrdinalIgnoreCase) -and
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
$applicationIncludeRoot = '$(GGLabRepositoryRoot)Sources\Application'
$shaderToolchainIncludeRoot = '$(GGLabRepositoryRoot)Sources\ShaderToolchain'
$shaderCompilerIncludeRoot = '$(GGLabRepositoryRoot)Sources\Tools\ShaderCompiler'
# Allowed, but deliberately not required: the current NapaVoxelCore/... layout
# still needs this broad root. Foundation Private access is compiler-gated below.
$repositorySourcesIncludeRoot = '$(GGLabRepositoryRoot)Sources'
$foundationPublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Public'
$foundationPrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Private'
$testCorePublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabTestCore\Public'
$testCorePrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabTestCore\Private'
$runtimeTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\GGLabRuntime'
$napaTestsIncludeRoot = '$(GGLabRepositoryRoot)Tests\NapaVoxelCore'
$napaIncludeRoot = '$(GGLabRepositoryRoot)Sources'
Test-ProjectIncludeVisibility $runtimeProject $namespace `
    "Projects/GGLabRuntime/GGLabRuntime.vcxproj" `
    @($runtimeIncludeRoot, $foundationPublicIncludeRoot) `
    @($runtimeIncludeRoot, $foundationPublicIncludeRoot, $shaderToolchainIncludeRoot)
Test-ProjectIncludeVisibility $applicationProject $applicationNamespace `
    "Projects/Application/Application.vcxproj" `
    @($applicationIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot) `
    @($applicationIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot, $repositorySourcesIncludeRoot,
        $shaderToolchainIncludeRoot)
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
    @($runtimeTestsIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot) `
    @($runtimeTestsIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $testCorePublicIncludeRoot, $shaderToolchainIncludeRoot)
Test-ProjectIncludeVisibility $shaderToolchainProject $shaderToolchainNamespace `
    "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj" `
    @($shaderToolchainIncludeRoot, $foundationPublicIncludeRoot) `
    @($shaderToolchainIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $shaderCompilerProject $shaderCompilerNamespace `
    "Projects/ShaderCompiler/ShaderCompiler.vcxproj" `
    @($shaderCompilerIncludeRoot, $shaderToolchainIncludeRoot, $foundationPublicIncludeRoot) `
    @($shaderCompilerIncludeRoot, $shaderToolchainIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $napaTestsProject $napaTestsNamespace `
    "Projects/NapaVoxelCoreTests/NapaVoxelCoreTests.vcxproj" `
    @($napaTestsIncludeRoot, $napaIncludeRoot) `
    @($napaTestsIncludeRoot, $napaIncludeRoot)

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
Test-ProjectPrivateAccessDefinition $applicationProject $applicationNamespace `
    "Projects/Application/Application.vcxproj" $false
Test-ProjectPrivateAccessDefinition $napaProject $napaNamespace `
    "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj" $false
Test-ProjectPrivateAccessDefinition $testCoreProject $testCoreNamespace `
    "Projects/GGLabTestCore/GGLabTestCore.vcxproj" $false
Test-ProjectPrivateAccessDefinition $runtimeTestsProject $runtimeTestsNamespace `
    "Projects/GGLabRuntimeTests/GGLabRuntimeTests.vcxproj" $false

# Project-file encoding contract: every vcxproj and .filters file must be
# UTF-8 with BOM so editor/tool round-trips cannot silently drop it.
foreach ($projectEncodingFile in @(
        Get-ChildItem -LiteralPath (Join-Path $root "Projects") -Recurse -File |
        Where-Object { $_.Extension -in @(".vcxproj", ".filters") })) {
    $projectEncodingBytes = [System.IO.File]::ReadAllBytes($projectEncodingFile.FullName)
    $projectEncodingHasBom = ($projectEncodingBytes.Length -ge 3 -and
        $projectEncodingBytes[0] -eq 0xEF -and $projectEncodingBytes[1] -eq 0xBB -and
        $projectEncodingBytes[2] -eq 0xBF)
    if (-not $projectEncodingHasBom) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-encoding"
            Target = ConvertTo-RepoRelativePath $projectEncodingFile.FullName
            Reason = "vcxproj/.filters file must be UTF-8 with BOM"
        })
    }
}

$firstPartySourceExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
$publicHeaderExtensions = @(".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
$ownershipSpecifications = @(
    [pscustomobject]@{
        Name       = "Application"
        SourceRoot = $applicationSourcesDir
        ItemPaths  = $applicationSourceItems
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

if (-not $applicationProjectReferenceSet.Contains($runtimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/Application/Application.vcxproj"
        Reason = "missing ProjectReference to GGLabRuntime"
    })
}
if (-not $runtimeProjectReferenceSet.Contains($shaderToolchainProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "missing ProjectReference to ShaderToolchainCore"
    })
}
if (-not $shaderToolchainProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
        Reason = "missing ProjectReference to GGLabFoundation"
    })
}
if ($shaderToolchainProjectReferenceSet.Contains($runtimeProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($applicationProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($napaProjectPath) -or
    $shaderToolchainProjectReferenceSet.Contains($shaderCompilerProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderToolchainCore/ShaderToolchainCore.vcxproj"
        Reason = "ShaderToolchainCore must not reference GGLabRuntime, Application, NapaVoxelCore, or ShaderCompiler"
    })
}
if (-not $shaderCompilerProjectReferenceSet.Contains($shaderToolchainProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
        Reason = "missing ProjectReference to ShaderToolchainCore"
    })
}
foreach ($reference in $shaderCompilerProjectReferenceSet) {
    if ($reference -ne $shaderToolchainProjectPath) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "project-graph"
            Target = "Projects/ShaderCompiler/ShaderCompiler.vcxproj"
            Reason = "gglab-shaderc may reference only ShaderToolchainCore"
        })
    }
}
if (-not $applicationProjectReferenceSet.Contains($napaProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/Application/Application.vcxproj"
        Reason = "missing ProjectReference to NapaVoxelCore"
    })
}
if (-not $applicationProjectReferenceSet.Contains($foundationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/Application/Application.vcxproj"
        Reason = "missing direct ProjectReference to GGLabFoundation"
    })
}
if (-not $applicationProjectReferenceSet.Contains($testCoreProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/Application/Application.vcxproj"
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
if ($runtimeProjectReferenceSet.Contains($applicationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "must not reference Application"
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
$runtimeTestsRequiredReferences = @($runtimeProjectPath, $foundationProjectPath, $testCoreProjectPath)
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
    $testCoreProjectPath, $shaderToolchainProjectPath, $shaderCompilerProjectPath,
    $directXTexProjectPath)
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
            Reason = "may reference only GGLabRuntime, GGLabFoundation and GGLabTestCore, plus the vendored DirectXTex project"
        })
    }
}
foreach ($forbiddenReference in @($applicationProjectPath, $runtimeProjectPath, $foundationProjectPath)) {
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
$runtimeTestsForbiddenIncludeRegex = '#include\s*[<"](?:Application[\\/]|DevTools[\\/])'
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
            Reason = "GGLabRuntimeTests source includes an Application-owned header"
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
    if ($content -notmatch
        '#if\s+!defined\s*\(\s*GGLAB_FOUNDATION_PRIVATE_ACCESS\s*\)') {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "foundation-private-access"
            Target = ConvertTo-RepoRelativePath $privateHeader.FullName
            Reason = "private header lacks the Foundation-only compile access guard"
        })
    }
}

$nonFoundationSourceItems = @($applicationSourceItems + $runtimeSourceItems +
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

$logicalIncludeSpecifications = @(
    [pscustomobject]@{
        Name        = "Application"
        ScanRoot    = $applicationSourcesDir
        LogicalRoot = $applicationSourcesDir
    }
    [pscustomobject]@{
        Name        = "GGLabFoundation"
        ScanRoot    = $foundationPublicDir
        LogicalRoot = $foundationPublicDir
    }
    [pscustomobject]@{
        Name        = "GGLabRuntime"
        ScanRoot    = $runtimeSourcesDir
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
        ScanRoot    = $napaSourcesDir
        LogicalRoot = $repositorySourcesDir
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
        if ($specification.Name -eq "GGLabFoundation" -and
            -not $logicalPath.StartsWith("GGLabFoundation/",
                [System.StringComparison]::Ordinal)) {
            $projectContractFindings.Add([pscustomobject]@{
                Rule   = "logical-include"
                Target = ConvertTo-RepoRelativePath $header.FullName
                Reason = "Foundation public include path lacks the GGLabFoundation/ prefix"
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
Write-Host (("Project items: {0} Application, {1} Foundation, {2} FoundationTests, " +
    "{3} GGLabRuntime, {4} NapaVoxelCore, {5} TestCore, {6} RuntimeTests, {7} NapaTests") -f `
        $applicationSourceItems.Count, $foundationSourceItems.Count,
        $foundationTestsSourceItems.Count, $runtimeSourceItems.Count,
        $napaSourceItems.Count, $testCoreSourceItems.Count,
        $runtimeTestsSourceItems.Count, $napaTestsSourceItems.Count)
Write-Host "Platform: $($candidateFiles.Count) candidate files (Core/Scene/Graphics/Diagnostics)"
Write-Host (("Compile items: {0} Application, {1} Foundation, {2} FoundationTests, " +
    "{3} GGLabRuntime, {4} NapaVoxelCore, {5} TestCore, {6} RuntimeTests, {7} NapaTests") -f `
        $applicationCompileFiles.Count, $foundationCompileFiles.Count,
        $foundationTestsCompileFiles.Count, $runtimeCompileFiles.Count,
        $napaCompileFiles.Count, $testCoreCompileFiles.Count,
        $runtimeTestsCompileFiles.Count, $napaTestsCompileFiles.Count)
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
