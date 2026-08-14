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
$foundationTestsDir = Join-Path $root "Tests/GGLabFoundation"
$runtimeSourcesDir = Join-Path $root "Sources/GGLabRuntime"
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
    [pscustomobject]@{ File = "Graphics/Shader/ShaderCompiler.cpp";                            Kind = "platform";  Reason = "HRESULT/DXC COM integration; planned: move to shader toolchain project ownership" },
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

$napaProjectPath = Join-Path $root "Projects/NapaVoxelCore/NapaVoxelCore.vcxproj"
if (-not (Test-Path $napaProjectPath)) {
    throw "NapaVoxelCore project not found: $napaProjectPath"
}
$napaProject = [xml](Get-Content -LiteralPath $napaProjectPath -Raw -ErrorAction Stop)
$napaNamespace = New-Object System.Xml.XmlNamespaceManager($napaProject.NameTable)
$napaNamespace.AddNamespace("msb", "http://schemas.microsoft.com/developer/msbuild/2003")
$napaProjectDir = Split-Path -Parent $napaProjectPath

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
$repositorySourcesIncludeRoot = '$(GGLabRepositoryRoot)Sources'
$foundationPublicIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Public'
$foundationPrivateIncludeRoot = '$(GGLabRepositoryRoot)Sources\GGLabFoundation\Private'
Test-ProjectIncludeVisibility $runtimeProject $namespace `
    "Projects/GGLabRuntime/GGLabRuntime.vcxproj" `
    @($runtimeIncludeRoot, $foundationPublicIncludeRoot) `
    @($runtimeIncludeRoot, $foundationPublicIncludeRoot)
Test-ProjectIncludeVisibility $applicationProject $applicationNamespace `
    "Projects/Application/Application.vcxproj" `
    @($applicationIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $repositorySourcesIncludeRoot) `
    @($applicationIncludeRoot, $runtimeIncludeRoot, $foundationPublicIncludeRoot,
        $repositorySourcesIncludeRoot)
Test-ProjectIncludeVisibility $foundationProject $foundationNamespace `
    "Projects/GGLabFoundation/GGLabFoundation.vcxproj" `
    @($foundationPublicIncludeRoot, $foundationPrivateIncludeRoot) `
    @($foundationPublicIncludeRoot, $foundationPrivateIncludeRoot)
Test-ProjectIncludeVisibility $foundationTestsProject $foundationTestsNamespace `
    "Projects/GGLabFoundationTests/GGLabFoundationTests.vcxproj" `
    @($foundationPublicIncludeRoot) @($foundationPublicIncludeRoot)

$firstPartySourceExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
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
        Name       = "NapaVoxelCore"
        SourceRoot = $napaSourcesDir
        ItemPaths  = $napaSourceItems
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

        if (-not (Test-IsPathUnderRoot $itemPath $specification.SourceRoot)) {
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

$shaderCompilerProbePaths = @(
    (Join-Path $runtimeSourcesDir "Graphics/Shader/ShaderCompiler.h"),
    (Join-Path $runtimeSourcesDir "Graphics/Shader/ShaderCompiler.cpp")
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

$publicHeaderExtensions = @(".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx")
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
    "{3} GGLabRuntime, {4} NapaVoxelCore") -f $applicationSourceItems.Count,
        $foundationSourceItems.Count, $foundationTestsSourceItems.Count,
        $runtimeSourceItems.Count, $napaSourceItems.Count)
Write-Host "Platform: $($candidateFiles.Count) candidate files (Core/Scene/Graphics/Diagnostics)"
Write-Host (("Compile items: {0} Application, {1} Foundation, {2} FoundationTests, " +
    "{3} GGLabRuntime, {4} NapaVoxelCore") -f $applicationCompileFiles.Count,
        $foundationCompileFiles.Count, $foundationTestsCompileFiles.Count,
        $runtimeCompileFiles.Count, $napaCompileFiles.Count)
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
