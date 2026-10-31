<# Checks shader routing, generated membership, negative ownership cases and no-op runs. #>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/ShaderProjectOwnership.psm1') -Force
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
$fixtureRoot = Join-Path $tempRoot ('gglab-shader-ownership-' + [Guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($false)
$checks = [Collections.Generic.List[string]]::new()
$cases = @(
    @{path='Common/Common.hlsli'; owner='GGLabRuntime'},
    @{path='Lighting/GIShaderConstants.hlsli'; owner='GGLabRuntime'},
    @{path='Lighting/GISampling.hlsli'; owner='GGLabRuntime'},
    @{path='PBR/BRDF.hlsli'; owner='GGLabRuntime'},
    @{path='Passes/PassGIReference.hlsl'; owner='GGLabRuntime'},
    @{path='Passes/PassCoordinateConformance.hlsl'; owner='WinAppHost'},
    @{path='Passes/PassNapaVoxel.hlsl'; owner='WinAppHost'},
    @{path='Passes/PassRayQuery.hlsl'; owner='WinAppHost'},
    @{path='Passes/PassRaySceneProbe.hlsl'; owner='WinAppHost'},
    @{path='Passes/PassRenderGraphComputeSmoke.hlsl'; owner='WinAppHost'},
    @{path='Common/NonFiniteSiteIds.hlsli'; owner='ShaderArtifactRuntime'},
    @{path='Metadata/NonFiniteSites.json'; owner='ShaderArtifactRuntime'},
    @{path='shadertoolsconfig.json'; owner='ShaderCompiler'},
    @{path='Tests/RenderingContractCompile.hlsl'; owner='ShaderRuntimeIntegrationTests'},
    @{path='Tests/Generated/pinned.hlsli'; owner='ShaderRuntimeIntegrationTests'}
)
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $checks.Add($Name)
}
function Project-Path([string]$Owner) { return "$fixtureRoot/Projects/$Owner/$Owner.vcxproj" }
function Read-Project([string]$Owner) { return [xml](Get-Content -LiteralPath (Project-Path $Owner) -Raw) }
function Save-Project([string]$Owner, [xml]$Document) {
    [IO.File]::WriteAllText((Project-Path $Owner), $Document.OuterXml, $utf8)
}
function Shader-Node([xml]$Document, [string]$Path) {
    $include = '..\..\Shaders\' + $Path.Replace('/', '\')
    return $Document.SelectSingleNode("//*[@Include='$include']")
}
function Add-ShaderNode([xml]$Document, [string]$Path, [string]$Type='FxCompile') {
    $group = $Document.CreateElement('ItemGroup', $Document.DocumentElement.NamespaceURI)
    $group.SetAttribute('Condition', "'`$(GGLAB_ARTIFACT_ONLY_RUNTIME)'!='1'")
    $node = $Document.CreateElement($Type, $Document.DocumentElement.NamespaceURI)
    $node.SetAttribute('Include', '..\..\Shaders\' + $Path.Replace('/', '\'))
    [void]$group.AppendChild($node)
    [void]$Document.DocumentElement.AppendChild($group)
    return $node
}
function Expect-Finding([string]$Rule, [string]$Name, [string]$ProjectPath='') {
    $findings = @(Get-ShaderProjectOwnershipFindings $fixtureRoot)
    $matches = @($findings | Where-Object {
        $_.Rule -eq $Rule -and (-not $ProjectPath -or
            $_.Reason.StartsWith([IO.Path]::GetFullPath($ProjectPath), [StringComparison]::OrdinalIgnoreCase))
    })
    Check ($matches.Count -gt 0) $Name
}
function Sync-Fixture {
    $execution = Invoke-VerificationScript (Join-Path $root 'Scripts/SyncShadersToVS.ps1') @{RootDir=$fixtureRoot}
    if ($execution.exitCode -ne 0) { throw "Shader synchronization failed: $($execution.output) $($execution.error)" }
}
try {
    # All project shells are needed by the real filter generator; no product code is copied.
    foreach ($project in (Get-ChildItem -LiteralPath (Join-Path $root 'Projects') -Recurse -Filter '*.vcxproj')) {
        $path = Project-Path $project.BaseName
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        [IO.File]::WriteAllText($path,
            '<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"></Project>', $utf8)
    }
    $sourceBytes = @{}
    foreach ($case in $cases) {
        $path = Join-Path "$fixtureRoot/Shaders" $case.path
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        $content = if ($case.path -like '*.json') { "{}`r`n" } else { "// shader fixture`r`n" }
        if ($case.path -eq 'Tests/Generated/pinned.hlsli') { $content = "// pinned LF bytes`n" }
        $sourceBytes[$case.path] = [Convert]::ToBase64String($utf8.GetBytes($content))
        [IO.File]::WriteAllText($path, $content, $utf8)
    }
    # Seed the legacy arrangement, including an unrelated deployment import and C++ item.
    $legacy = Read-Project WinApp
    foreach ($case in $cases) {
        $type = if ($case.path -like '*.hlsl') { 'FxCompile' } else { 'None' }
        $null = Add-ShaderNode $legacy $case.path $type
    }
    $deployment = $legacy.CreateElement('Import', $legacy.DocumentElement.NamespaceURI)
    $deployment.SetAttribute('Project', '..\..\PropertySheets\LinkShaderDirectory.targets')
    $deployment.SetAttribute('Condition', "'`$(GGLAB_ARTIFACT_ONLY_RUNTIME)'!='1'")
    [void]$legacy.DocumentElement.AppendChild($deployment)
    Save-Project WinApp $legacy
    $cppPath = "$fixtureRoot/Sources/GGLabRuntime/Private/owned.h"
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $cppPath) -Force
    [IO.File]::WriteAllText($cppPath, "#pragma once`r`n", $utf8)
    $runtime = Read-Project GGLabRuntime
    $cppGroup = $runtime.CreateElement('ItemGroup', $runtime.DocumentElement.NamespaceURI)
    $cpp = $runtime.CreateElement('ClInclude', $runtime.DocumentElement.NamespaceURI)
    $cpp.SetAttribute('Include', '..\..\Sources\GGLabRuntime\Private\owned.h')
    [void]$cppGroup.AppendChild($cpp)
    [void]$runtime.DocumentElement.AppendChild($cppGroup)
    Save-Project GGLabRuntime $runtime

    Expect-Finding 'shader-ownership' 'legacy-WinApp-membership-is-rejected'
    Sync-Fixture
    Check (@(Get-ShaderProjectOwnershipFindings $fixtureRoot).Count -eq 0) 'synchronization-establishes-unique-owners'
    $assignments = @(Get-ShaderProjectAssignments $fixtureRoot)
    foreach ($case in $cases) {
        $assignment = @($assignments | Where-Object RelativePath -eq $case.path)[0]
        $document = Read-Project $case.owner
        $node = Shader-Node $document $case.path
        Check ($assignment.Owner -eq $case.owner -and $null -ne $node) "owner-$($case.path)"
        $actualBytes = [Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path "$fixtureRoot/Shaders" $case.path)))
        Check ($actualBytes -ceq $sourceBytes[$case.path]) "unchanged-bytes-$($case.path)"
    }
    $hostDocument = Read-Project WinApp
    Check (@(Get-ShaderProjectItems $fixtureRoot (Project-Path WinApp) $hostDocument).Count -eq 0) 'WinApp-no-longer-owns-shaders'
    Check ($hostDocument.SelectSingleNode("//*[local-name()='Import']").OuterXml -ceq $deployment.OuterXml) 'deployment-import-is-preserved'
    Check ($null -ne (Read-Project GGLabRuntime).SelectSingleNode("//*[@Include='..\..\Sources\GGLabRuntime\Private\owned.h']")) 'Cpp-membership-is-preserved'

    $timestamps = @{}
    $projectBytes = @{}
    foreach ($file in (Get-ChildItem -LiteralPath "$fixtureRoot/Projects" -Recurse -File)) {
        $timestamps[$file.FullName] = $file.LastWriteTimeUtc
        $projectBytes[$file.FullName] = [Convert]::ToBase64String([IO.File]::ReadAllBytes($file.FullName))
    }
    Sync-Fixture
    foreach ($path in $timestamps.Keys) {
        Check ([IO.File]::GetLastWriteTimeUtc($path) -eq $timestamps[$path] -and
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $projectBytes[$path]) 'second-sync-is-a-byte-and-timestamp-no-op'
    }
    $execution = Invoke-VerificationScript (Join-Path $root 'Scripts/GenerateProjectFilters.ps1') @{RootDir=$fixtureRoot;Check=$true}
    Check ($execution.exitCode -eq 0) 'generated-filters-pass-check'

    $runtimeBytes = [IO.File]::ReadAllBytes((Project-Path GGLabRuntime))
    $hostBytes = [IO.File]::ReadAllBytes((Project-Path WinApp))
    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    [void]$node.ParentNode.RemoveChild($node)
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-membership' 'missing-membership-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    [void]$node.ParentNode.AppendChild($node.CloneNode($true))
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-membership' 'same-project-duplicate-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project WinApp
    $null = Add-ShaderNode $document 'Passes/PassGIReference.hlsl'
    Save-Project WinApp $document
    Expect-Finding 'shader-membership' 'cross-project-duplicate-is-rejected'
    Expect-Finding 'shader-ownership' 'wrong-owner-is-rejected'
    # Scanning is not restricted to projects with declared shader destinations.
    $extraDirectory = "$fixtureRoot/Projects/Unexpected"
    $null = New-Item -ItemType Directory -Path $extraDirectory
    [IO.File]::WriteAllText("$extraDirectory/Unexpected.vcxproj", $document.OuterXml, $utf8)
    Expect-Finding 'shader-ownership' 'unexpected-project-cannot-hide-a-shader' "$extraDirectory/Unexpected.vcxproj"
    [IO.File]::WriteAllText("$extraDirectory/GGLabRuntime.vcxproj", (Read-Project GGLabRuntime).OuterXml, $utf8)
    Expect-Finding 'shader-ownership' 'same-name-project-does-not-impersonate-the-owner' "$extraDirectory/GGLabRuntime.vcxproj"
    Sync-Fixture
    Check (@(Get-ShaderProjectOwnershipFindings $fixtureRoot).Count -eq 0) 'sync-repairs-cross-project-membership'
    Remove-Item -LiteralPath $extraDirectory -Recurse -Force

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    $node.ParentNode.SetAttribute('Condition', '')
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-item-condition' 'artifact-only-condition-is-preserved'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    $node.SetAttribute('Condition', "'`$(Configuration)'=='Debug'")
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-item-condition' 'configuration-specific-ownership-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    $replacement = $document.CreateElement('ClCompile', $document.DocumentElement.NamespaceURI)
    $replacement.SetAttribute('Include', $node.GetAttribute('Include'))
    [void]$node.ParentNode.ReplaceChild($replacement, $node)
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-item-type' 'accidental-Cpp-compilation-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    $excluded = $document.CreateElement('ExcludedFromBuild', $document.DocumentElement.NamespaceURI)
    $excluded.InnerText = 'false'
    [void]$node.AppendChild($excluded)
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-build-policy' 'accidental-MSBuild-shader-compilation-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $null = Add-ShaderNode $document 'Passes/Deleted.hlsl'
    Save-Project GGLabRuntime $document
    Expect-Finding 'shader-membership' 'stale-project-entry-is-rejected'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $document = Read-Project GGLabRuntime
    $node = Shader-Node $document 'Passes/PassGIReference.hlsl'
    $node.SetAttribute('Include', '$(GGLabRepositoryRoot)Shaders/Passes/PassGIReference.hlsl')
    Save-Project GGLabRuntime $document
    Check (@(Get-ShaderProjectOwnershipFindings $fixtureRoot).Count -eq 0) 'canonical-macro-and-forward-slash-path-is-recognized'
    [IO.File]::WriteAllBytes((Project-Path GGLabRuntime), $runtimeBytes)

    $unclassified = "$fixtureRoot/Shaders/Unclassified/new.hlsl"
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $unclassified)
    [IO.File]::WriteAllText($unclassified, '// unclassified', $utf8)
    $execution = Invoke-VerificationScript (Join-Path $root 'Scripts/SyncShadersToVS.ps1') @{RootDir=$fixtureRoot}
    Check ($execution.exitCode -ne 0 -and ($execution.output + $execution.error) -match 'No shader project ownership policy') 'unclassified-source-is-rejected-before-writing'
    Check ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Project-Path GGLabRuntime))) -ceq
        [Convert]::ToBase64String($runtimeBytes) -and
        [Convert]::ToBase64String([IO.File]::ReadAllBytes((Project-Path WinApp))) -ceq
        [Convert]::ToBase64String($hostBytes)) 'routing-error-leaves-projects-unchanged'
    $result = @{ok=$true;checks=$checks.ToArray();shaderFixtures=$cases.Count}
}
catch { $result = @{ok=$false;checks=$checks.ToArray();error=$_.Exception.Message} }
finally {
    $cleanupPath = [IO.Path]::GetFullPath($fixtureRoot)
    if (-not $cleanupPath.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Path]::GetFileName($cleanupPath).StartsWith('gglab-shader-ownership-')) {
        throw "Refusing cleanup outside the test temporary directory: $cleanupPath"
    }
    if (Test-Path -LiteralPath $cleanupPath) { Remove-Item -LiteralPath $cleanupPath -Recurse -Force }
}
$json = $result | ConvertTo-Json -Depth 5 -Compress
if ($OutputDirectory) {
    $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'result.json'), $json, $utf8)
}
[Console]::Out.WriteLine($json)
if (-not $result.ok) { exit 1 }
