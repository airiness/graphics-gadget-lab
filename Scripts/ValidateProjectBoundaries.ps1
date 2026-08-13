param(
    [string]$RootDir = "",
    [switch]$ShowAll
)

# Runtime project boundary validation.
# Enforces the source dependency direction contract for the runtime library:
#   Project graph - Application must reference GGLabRuntime; GGLabRuntime must
#                   not reference Application.
#   Compile ownership - source files must have one compilation owner, and all
#                       runtime-candidate .cpp files must have an owner.
#   Ownership boundary - runtime candidates must not include Application/*,
#                        DevTools/*, or the Application-owned Core/Input/*.
#   Platform leakage - portable runtime files must not depend on unapproved
#                      Win32 / GameInput / COM semantics.
# Current known violations are enumerated as an explicit ledger; any
# violation outside the ledger fails the validation.
#
# Ownership scanning derives from actual GGLabRuntime project items;
# all Diagnostics source/header files are required to be runtime-owned;
# portable-platform scanning remains classification-driven.

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
$runtimeSourcesDir = Join-Path $root "Sources/GGLabRuntime"

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
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanPipelineSystem.cpp";                   Kind = "platform";  Reason = "Win32 wide-string entry conversion; planned: UTF-8 entry point contract in shader runtime" },
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
$runtimeProjectReferences = Get-ProjectItemPaths $runtimeProject $namespace $runtimeProjectDir `
    "//msb:ProjectReference" "Runtime project reference"
$applicationProjectReferences = Get-ProjectItemPaths $applicationProject $applicationNamespace `
    $applicationProjectDir "//msb:ProjectReference" "Application project reference"

$runtimeCompilePathSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$applicationCompilePathSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$runtimeProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
$applicationProjectReferenceSet = New-Object 'System.Collections.Generic.HashSet[string]' `
    ([System.StringComparer]::OrdinalIgnoreCase)
foreach ($path in $runtimeCompileFiles) {
    [void]$runtimeCompilePathSet.Add($path)
}
foreach ($path in $applicationCompileFiles) {
    [void]$applicationCompilePathSet.Add($path)
}
foreach ($path in $runtimeProjectReferences) {
    [void]$runtimeProjectReferenceSet.Add($path)
}
foreach ($path in $applicationProjectReferences) {
    [void]$applicationProjectReferenceSet.Add($path)
}

$projectContractFindings = New-Object System.Collections.Generic.List[object]
if (-not $applicationProjectReferenceSet.Contains($runtimeProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/Application/Application.vcxproj"
        Reason = "missing ProjectReference to GGLabRuntime"
    })
}
if ($runtimeProjectReferenceSet.Contains($applicationProjectPath)) {
    $projectContractFindings.Add([pscustomobject]@{
        Rule   = "project-graph"
        Target = "Projects/GGLabRuntime/GGLabRuntime.vcxproj"
        Reason = "must not reference Application"
    })
}

foreach ($path in $runtimeCompileFiles) {
    if ($applicationCompilePathSet.Contains($path)) {
        $relativePath = $path.Substring($root.Length + 1).Replace('\', '/')
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "compile-ownership"
            Target = $relativePath
            Reason = "compiled by both Application and GGLabRuntime"
        })
    }
}

foreach ($file in ($candidateFiles | Where-Object { $_.Path.EndsWith(".cpp") })) {
    $ownedByRuntime = $runtimeCompilePathSet.Contains($file.FullPath)
    $ownedByApplication = $applicationCompilePathSet.Contains($file.FullPath)
    if (-not $ownedByRuntime -and -not $ownedByApplication) {
        $projectContractFindings.Add([pscustomobject]@{
            Rule   = "compile-ownership"
            Target = $file.Path
            Reason = "runtime-candidate source has no compilation owner"
        })
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
Write-Host "=== Runtime Project Boundary Validation ==="
Write-Host "Root: $root"
Write-Host "Ownership: $($runtimeOwnedFiles.Count) GGLabRuntime project items"
Write-Host "Platform: $($candidateFiles.Count) candidate files (Core/Scene/Graphics/Diagnostics)"
Write-Host "Compile owners: $($runtimeCompileFiles.Count) GGLabRuntime, $($applicationCompileFiles.Count) Application"
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
        Write-Host "RESULT: FAIL - project graph or compile ownership violations found."
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
