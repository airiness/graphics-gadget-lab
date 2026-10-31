# Shared routing for IDE membership only. Compilation recipes and deployment are separate.
Set-StrictMode -Version 3.0

$shaderItemCondition = "'`$(GGLAB_ARTIFACT_ONLY_RUNTIME)'!='1'"
$shaderOwnerOverrides = @{
    # These pipelines/queries are authored by Labs, not Runtime recipes.
    'Passes/PassCoordinateConformance.hlsl' = 'WinAppHost'
    'Passes/PassNapaVoxel.hlsl' = 'WinAppHost'
    'Passes/PassRayQuery.hlsl' = 'WinAppHost'
    'Passes/PassRaySceneProbe.hlsl' = 'WinAppHost'
    'Passes/PassRenderGraphComputeSmoke.hlsl' = 'WinAppHost'
    # Keep the generated HLSL identifiers with their CPU artifact ABI and inventory.
    'Common/NonFiniteSiteIds.hlsli' = 'ShaderArtifactRuntime'
    'shadertoolsconfig.json' = 'ShaderCompiler'
}

function Get-ShaderSourceOwner {
    param([string]$RelativePath)

    if ($shaderOwnerOverrides.ContainsKey($RelativePath)) {
        return $shaderOwnerOverrides[$RelativePath]
    }
    if ($RelativePath -like 'Tests/*') { return 'ShaderRuntimeIntegrationTests' }
    if ($RelativePath -like 'Metadata/*.json') { return 'ShaderArtifactRuntime' }
    if ($RelativePath -like 'Common/*.hlsli' -or $RelativePath -like 'Lighting/*.hlsli' -or
        $RelativePath -like 'PBR/*.hlsli' -or $RelativePath -like 'Passes/*.hlsl') {
        return 'GGLabRuntime'
    }
    throw "No shader project ownership policy for: Shaders/$RelativePath"
}

function Get-ShaderProjectAssignments {
    param([Parameter(Mandatory=$true)][string]$RootDir)

    $RootDir = [IO.Path]::GetFullPath($RootDir)
    $shaderRoot = Join-Path $RootDir 'Shaders'
    if (-not (Test-Path -LiteralPath $shaderRoot -PathType Container)) {
        throw "Shader directory not found: $shaderRoot"
    }
    foreach ($file in (Get-ChildItem -LiteralPath $shaderRoot -Recurse -File | Sort-Object FullName)) {
        if ($file.Extension -notin @('.hlsl', '.hlsli', '.json')) { continue }
        $relativePath = $file.FullName.Substring($shaderRoot.Length + 1).Replace('\', '/')
        [pscustomobject]@{
            FullName = $file.FullName
            RelativePath = $relativePath
            Owner = Get-ShaderSourceOwner $relativePath
            ItemType = if ($file.Extension -ieq '.hlsl') { 'FxCompile' } else { 'None' }
            Condition = $shaderItemCondition
        }
    }
}

function Get-ShaderProjectItems {
    param(
        [Parameter(Mandatory=$true)][string]$RootDir,
        [Parameter(Mandatory=$true)][string]$ProjectPath,
        [Parameter(Mandatory=$true)][xml]$Document
    )

    $RootDir = [IO.Path]::GetFullPath($RootDir)
    $shaderRoot = (Join-Path $RootDir 'Shaders').TrimEnd('\') + '\'
    $projectDirectory = Split-Path -Parent $ProjectPath
    foreach ($node in $Document.SelectNodes('//*[@Include]')) {
        if ($node.LocalName -in @('ProjectConfiguration', 'ProjectReference')) { continue }
        $include = $node.GetAttribute('Include')
        $expanded = $include.Replace('$(GGLabRepositoryRoot)', $RootDir.TrimEnd('\') + '\')
        $expanded = $expanded.Replace('$(SolutionDir)', $RootDir.TrimEnd('\') + '\')
        $expanded = $expanded.Replace('$(ShaderRoot)', $shaderRoot.TrimEnd('\'))
        $isShaderSource = [IO.Path]::GetExtension($include) -in @('.hlsl', '.hlsli')
        if ($expanded.Contains('$(')) {
            if ($isShaderSource) { throw "Unresolved shader project item: $ProjectPath $include" }
            continue
        }
        $fullPath = [IO.Path]::GetFullPath([IO.Path]::Combine($projectDirectory, $expanded))
        if (-not $fullPath.StartsWith($shaderRoot, [StringComparison]::OrdinalIgnoreCase)) {
            if ($isShaderSource) { throw "Shader project item is outside Shaders: $ProjectPath $include" }
            continue
        }
        [pscustomobject]@{
            FullName = $fullPath
            RelativePath = $fullPath.Substring($shaderRoot.Length).Replace('\', '/')
            Node = $node
        }
    }
}

function Get-ShaderProjectOwnershipFindings {
    param([Parameter(Mandatory=$true)][string]$RootDir)

    $RootDir = [IO.Path]::GetFullPath($RootDir)
    $assignments = @{}
    foreach ($assignment in (Get-ShaderProjectAssignments $RootDir)) {
        $assignments[$assignment.FullName] = $assignment
    }
    $memberships = @{}
    foreach ($project in (Get-ChildItem -LiteralPath (Join-Path $RootDir 'Projects') -Recurse -Filter '*.vcxproj')) {
        $document = [xml](Get-Content -LiteralPath $project.FullName -Raw)
        foreach ($item in (Get-ShaderProjectItems $RootDir $project.FullName $document)) {
            $target = 'Shaders/' + $item.RelativePath
            if (-not $memberships.ContainsKey($item.FullName)) { $memberships[$item.FullName] = @() }
            $memberships[$item.FullName] += $project.BaseName
            if (-not $assignments.ContainsKey($item.FullName)) {
                [pscustomobject]@{ Rule='shader-membership'; Target=$target;
                    Reason="$($project.BaseName) lists a missing or unsupported shader file" }
                continue
            }
            $expected = $assignments[$item.FullName]
            $ownerPath = Join-Path $RootDir "Projects/$($expected.Owner)/$($expected.Owner).vcxproj"
            if ($project.FullName -ine [IO.Path]::GetFullPath($ownerPath)) {
                [pscustomobject]@{ Rule='shader-ownership'; Target=$target;
                    Reason="$($project.FullName) lists a shader owned by $($expected.Owner)" }
            }
            if ($item.Node.LocalName -ne $expected.ItemType) {
                [pscustomobject]@{ Rule='shader-item-type'; Target=$target;
                    Reason="$($project.BaseName) must use $($expected.ItemType), not $($item.Node.LocalName)" }
            }
            if ($item.Node.ParentNode.LocalName -ne 'ItemGroup' -or
                $item.Node.ParentNode.ParentNode.LocalName -ne 'Project' -or
                $item.Node.ParentNode.GetAttribute('Condition') -cne $expected.Condition -or
                $item.Node.GetAttribute('Condition')) {
                [pscustomobject]@{ Rule='shader-item-condition'; Target=$target;
                    Reason="$($project.BaseName) must retain the artifact-only presentation condition" }
            }
            foreach ($excluded in $item.Node.SelectNodes("*[local-name()='ExcludedFromBuild']")) {
                if ($excluded.InnerText.Trim() -ine 'true') {
                    [pscustomobject]@{ Rule='shader-build-policy'; Target=$target;
                        Reason="$($project.BaseName) must not enable MSBuild shader compilation" }
                }
            }
        }
    }
    foreach ($assignment in ($assignments.Values | Sort-Object RelativePath)) {
        if (-not $memberships.ContainsKey($assignment.FullName)) {
            [pscustomobject]@{ Rule='shader-membership'; Target=('Shaders/' + $assignment.RelativePath);
                Reason="$($assignment.Owner)-owned shader is missing from its project" }
        }
    }
    foreach ($path in ($memberships.Keys | Sort-Object)) {
        if ($memberships[$path].Count -gt 1) {
            [pscustomobject]@{ Rule='shader-membership'; Target=('Shaders/' + $path.Substring(
                    (Join-Path $RootDir 'Shaders').Length + 1).Replace('\', '/'));
                Reason=('shader is listed multiple times: ' + ($memberships[$path] -join ', ')) }
        }
    }
}

Export-ModuleMember -Function Get-ShaderProjectAssignments, Get-ShaderProjectItems, Get-ShaderProjectOwnershipFindings
