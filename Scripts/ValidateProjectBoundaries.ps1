param(
    [string]$RootDir = "",
    [switch]$ShowAll
)

# Runtime project boundary validation.
# Enforces the source dependency direction contract for the runtime library:
#   Ownership boundary - runtime candidates must not include Application/*,
#                        DevTools/*, or Application-owned regions.
#   Platform leakage - portable runtime files must not depend on unapproved
#                      Win32 / GameInput / COM semantics.
# Current known violations are enumerated as an explicit ledger; any
# violation outside the ledger fails the validation.
#
# Candidate scanning is directory-driven (Core/Scene/Graphics/Diagnostics)
# until the runtime library project exists; scanning then switches to
# project-ownership driven.

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
$sourcesDir = Join-Path $root "Sources"

# Candidate runtime directories (portable runtime candidates; backend leaves included).
$candidateDirs = @("Core", "Scene", "Graphics", "Diagnostics")

# Application-owned regions that physically live inside candidate directories.
# Ownership wins over directory name.
$applicationOwnedRegions = @(
    "Core/Input" # GameInput implementation, Application-owned
)

# Platform / backend leaf allowlists.
$prefixAllow = @(
    "Core/Platform/Win", # Windows implementation leaves
    "Graphics/RHI/DX12"  # DX12 backend leaf (Windows-native by design)
)
$exactAllow = @(
    "Core/Hash/Sha256.cpp",                     # BCrypt implementation leaf
    "Core/HResult.h",                           # transitional: Windows/DX12 helper ownership in flight
    "Core/HResult.cpp",                         # transitional: Windows/DX12 helper ownership in flight
    "Graphics/Asset/Loading/TextureLoader.cpp", # DirectXTex (Windows third-party) consumer
    "Graphics/RHI/Vulkan/VulkanWin32Surface.h", # Win32 WSI leaf
    "Graphics/RHI/Vulkan/VulkanWin32Surface.cpp" # Win32 WSI leaf
)

# Known violations ledger. Each entry: File, Kind (ownership/platform), Reason
# (planned owner / removal condition). New entries may only be added with an
# explicit plan; entries whose violation disappears are reported as stale and
# should be removed from the ledger.
$knownViolations = @(
    [pscustomobject]@{ File = "Graphics/RenderPass/RenderPassDevelopGui.cpp";                  Kind = "ownership"; Reason = "DevTools include; planned: DevelopGui render extension seam" },
    [pscustomobject]@{ File = "Diagnostics/Snapshots/LabSnapshot.h";                           Kind = "ownership"; Reason = "Application/Lab types; Lab snapshot stays Application-owned" },
    [pscustomobject]@{ File = "Diagnostics/Builders/LabSnapshotProvider.cpp";                  Kind = "ownership"; Reason = "Application/Lab types; Lab snapshot provider stays Application-owned" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanBootstrap.h";                         Kind = "platform";  Reason = "Windows.h/HWND in portable Vulkan contract; planned: narrow to the Win32 WSI leaf" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanInstance.h";                          Kind = "platform";  Reason = "Windows.h in portable Vulkan contract; planned: narrow to the Win32 WSI leaf" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanSwapChain.h";                         Kind = "platform";  Reason = "Windows.h in portable Vulkan contract; planned: narrow to the Win32 WSI leaf" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanQualification.cpp";                   Kind = "platform";  Reason = "Win32 window-control behavior; planned: decide leaf vs host ownership" },
    [pscustomobject]@{ File = "Graphics/RHI/Vulkan/VulkanQualification.h";                     Kind = "platform";  Reason = "Windows.h/HWND; same family as VulkanQualification.cpp; planned: decide leaf vs host ownership" },
    [pscustomobject]@{ File = "Graphics/Shader/ShaderCompiler.cpp";                            Kind = "platform";  Reason = "HRESULT/DXC COM integration; planned: move to shader toolchain project ownership" },
    [pscustomobject]@{ File = "Graphics/Shader/ShaderManager.cpp";                             Kind = "platform";  Reason = "Windows.h/IsDebuggerPresent debug-flag policy; planned: host-injected debug policy seam" }
)

$ownershipIncludeRegex = '#include\s*"(Application|DevTools)/'
$platformLeakRegex = 'Windows\.h|GameInput|IGameInput|\bHWND\b|\bHMODULE\b|\bHRESULT\b|CoInitializeEx|SetThreadDescription|MultiByteToWideChar|WideCharToMultiByte|GetModuleFileName|CreateSymbolicLink'

function Test-Allowed {
    param([string]$RelativePath)

    foreach ($region in $applicationOwnedRegions) {
        if ($RelativePath.StartsWith($region + "/")) {
            return $true
        }
    }
    foreach ($prefix in $prefixAllow) {
        if ($RelativePath.StartsWith($prefix + "/")) {
            return $true
        }
    }
    foreach ($exact in $exactAllow) {
        if ($RelativePath -eq $exact) {
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

# Collect candidate files with repository-relative paths.
$files = @()
foreach ($dir in $candidateDirs) {
    $dirPath = Join-Path $sourcesDir $dir
    if (-not (Test-Path $dirPath)) {
        throw "Candidate directory not found: $dirPath"
    }
    $files += Get-ChildItem -Path $dirPath -Recurse -File |
        Where-Object { $_.Extension -in @(".h", ".cpp") } |
        ForEach-Object {
            [pscustomobject]@{
                Path     = $_.FullName.Substring($sourcesDir.Length + 1).Replace('\', '/')
                FullPath = $_.FullName
            }
        }
}

$ownershipFindings = New-Object System.Collections.Generic.List[object]
$platformFindings = New-Object System.Collections.Generic.List[object]

foreach ($file in $files) {
    $content = Get-Content -LiteralPath $file.FullPath -Raw -ErrorAction Stop

    # Ownership boundary (skip Application-owned regions; allowlists are platform policy).
    if (-not ($file.Path.StartsWith("Core/Input/"))) {
        if ($content -match $ownershipIncludeRegex) {
            $ownershipFindings.Add([pscustomobject]@{ File = $file.Path; Kind = "ownership" })
        }
    }

    # Platform leakage (skip Application-owned regions and allowlisted leaves).
    if (-not (Test-Allowed $file.Path)) {
        if ($content -match $platformLeakRegex) {
            $platformFindings.Add([pscustomobject]@{ File = $file.Path; Kind = "platform" })
        }
    }
}

# Classify actual findings against the ledger.
$unexpected = @()
$knownFound = @()
foreach ($finding in ($ownershipFindings + $platformFindings)) {
    if (Test-IsKnown $finding.File $finding.Kind) {
        $knownFound += $finding
    }
    else {
        $unexpected += $finding
    }
}

# Stale ledger entries: known entry whose violation is no longer present.
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
        $fileExists = Test-Path (Join-Path $sourcesDir $entry.File)
        $stale += [pscustomobject]@{
            File   = $entry.File
            Kind   = $entry.Kind
            Reason = if ($fileExists) { "violation resolved" } else { "file no longer exists" }
        }
    }
}

# Report.
Write-Host "=== Runtime Project Boundary Validation ==="
Write-Host "Root: $root"
Write-Host "Scanned: $($files.Count) candidate files (Core/Scene/Graphics/Diagnostics)"
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

if ($unexpected.Count -gt 0) {
    Write-Host "RESULT: FAIL - unexpected boundary violations found."
    exit 1
}

Write-Host "RESULT: PASS - no unexpected boundary violations."
exit 0
