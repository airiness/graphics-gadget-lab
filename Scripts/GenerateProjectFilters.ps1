param(
    [string]$RootDir = "",
    [switch]$Check
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    param([string]$InputRoot)

    if (-not [string]::IsNullOrWhiteSpace($InputRoot)) {
        $cleanRoot = $InputRoot.Trim().Trim('"')
        return (Resolve-Path -LiteralPath $cleanRoot).Path
    }

    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

function Read-XmlDocument {
    param([string]$Path)

    $document = New-Object System.Xml.XmlDocument
    $document.PreserveWhitespace = $false
    $document.Load($Path)
    return $document
}

function ConvertTo-RepoRelativePath {
    param(
        [string]$RepoRoot,
        [string]$FullPath
    )

    $rootUri = [System.Uri]($RepoRoot.TrimEnd('\') + '\')
    $pathUri = [System.Uri]$FullPath
    $relativePath = [System.Uri]::UnescapeDataString($rootUri.MakeRelativeUri($pathUri).ToString())
    if ($relativePath.StartsWith("../", [System.StringComparison]::Ordinal)) {
        throw "Project item is outside the repository: $FullPath"
    }

    return $relativePath.Replace('/', '\')
}

function Get-FilterPath {
    param(
        [string]$ProjectName,
        [string]$RepoRelativePath,
        [array]$Mappings
    )

    $matchingMappings = @($Mappings | Where-Object {
            $RepoRelativePath.Equals($_.Root, [System.StringComparison]::OrdinalIgnoreCase) -or
            $RepoRelativePath.StartsWith($_.Root + '\', [System.StringComparison]::OrdinalIgnoreCase)
        } | Sort-Object { $_.Root.Length } -Descending)

    if ($matchingMappings.Count -eq 0) {
        throw "No presentation mapping for $ProjectName project item: $RepoRelativePath"
    }

    $mapping = $matchingMappings[0]
    $relativeItemPath = if ($RepoRelativePath.Length -eq $mapping.Root.Length) {
        ""
    }
    else {
        $RepoRelativePath.Substring($mapping.Root.Length + 1)
    }

    $relativeDirectory = if ([string]::IsNullOrWhiteSpace($relativeItemPath)) {
        ""
    }
    else {
        [System.IO.Path]::GetDirectoryName($relativeItemPath)
    }

    if ([string]::IsNullOrWhiteSpace($mapping.FilterPrefix)) {
        return $relativeDirectory
    }
    if ([string]::IsNullOrWhiteSpace($relativeDirectory)) {
        return $mapping.FilterPrefix
    }

    return $mapping.FilterPrefix + '\' + $relativeDirectory
}

function New-DeterministicGuid {
    param(
        [string]$ProjectName,
        [string]$FilterPath
    )

    # MD5 is used only to derive a stable presentation identifier, not for security.
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $identity = "$ProjectName|$FilterPath".ToLowerInvariant()
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($identity)
        $hash = $md5.ComputeHash($bytes)
        return "{" + ([System.Guid]::new($hash)).ToString().ToUpperInvariant() + "}"
    }
    finally {
        $md5.Dispose()
    }
}

function Sort-ProjectItems {
    param([array]$Items)

    $list = New-Object 'System.Collections.Generic.List[object]'
    foreach ($item in $Items) {
        [void]$list.Add($item)
    }

    $comparison = [System.Comparison[object]]{
        param($left, $right)

        $result = [System.StringComparer]::OrdinalIgnoreCase.Compare($left.Include, $right.Include)
        if ($result -ne 0) {
            return $result
        }

        return [System.StringComparer]::Ordinal.Compare($left.Include, $right.Include)
    }
    $list.Sort($comparison)
    return $list.ToArray()
}

function New-MsbuildElement {
    param(
        [xml]$Document,
        [string]$Name
    )

    return $Document.CreateElement($Name, $Document.DocumentElement.NamespaceURI)
}

function Get-ProjectPresentationItems {
    param(
        [string]$RepoRoot,
        [pscustomobject]$Specification
    )

    $projectPath = Join-Path $RepoRoot $Specification.ProjectPath
    if (-not (Test-Path -LiteralPath $projectPath)) {
        throw "Project file not found: $projectPath"
    }

    $projectDocument = Read-XmlDocument $projectPath
    $namespace = New-Object System.Xml.XmlNamespaceManager($projectDocument.NameTable)
    $namespace.AddNamespace("msb", $projectDocument.DocumentElement.NamespaceURI)
    $projectDirectory = Split-Path -Parent $projectPath
    $itemNodes = @($projectDocument.SelectNodes("/msb:Project/msb:ItemGroup/*[@Include]", $namespace))
    $presentationItemTypes = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([System.StringComparer]::Ordinal)
    $ignoredItemTypes = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([System.StringComparer]::Ordinal)
    $seenPaths = New-Object 'System.Collections.Generic.HashSet[string]' `
        ([System.StringComparer]::OrdinalIgnoreCase)

    foreach ($itemType in $Specification.ItemTypes) {
        [void]$presentationItemTypes.Add($itemType)
    }
    foreach ($itemType in $Specification.IgnoredItemTypes) {
        [void]$ignoredItemTypes.Add($itemType)
    }

    $items = @()
    foreach ($node in $itemNodes) {
        $itemType = $node.LocalName
        if ($ignoredItemTypes.Contains($itemType)) {
            continue
        }
        if (-not $presentationItemTypes.Contains($itemType)) {
            throw "No item-type policy for $($Specification.Name): $itemType $($node.Include)"
        }

        $fullPath = [System.IO.Path]::GetFullPath((Join-Path $projectDirectory $node.Include))
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Project item not found: $($Specification.Name) $itemType $($node.Include)"
        }
        if (-not $seenPaths.Add($fullPath)) {
            throw "Duplicate project item: $($Specification.Name) $fullPath"
        }

        $repoRelativePath = ConvertTo-RepoRelativePath $RepoRoot $fullPath
        $filterPath = Get-FilterPath $Specification.Name $repoRelativePath $Specification.Mappings
        $items += [pscustomobject]@{
            ItemType = $itemType
            Include = $node.Include.Replace('/', '\')
            FilterPath = $filterPath.Replace('/', '\')
        }
    }

    return $items
}

function New-FiltersDocument {
    param(
        [pscustomobject]$Specification,
        [array]$Items
    )

    $document = New-Object System.Xml.XmlDocument
    $declaration = $document.CreateXmlDeclaration("1.0", "utf-8", $null)
    [void]$document.AppendChild($declaration)

    $projectElement = $document.CreateElement(
        "Project",
        "http://schemas.microsoft.com/developer/msbuild/2003")
    [void]$projectElement.SetAttribute("ToolsVersion", "4.0")
    [void]$document.AppendChild($projectElement)

    $filterNames = New-Object 'System.Collections.Generic.SortedSet[string]' `
        ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $Items) {
        if ([string]::IsNullOrWhiteSpace($item.FilterPath)) {
            continue
        }

        $current = ""
        foreach ($part in $item.FilterPath.Split('\')) {
            $current = if ([string]::IsNullOrWhiteSpace($current)) {
                $part
            }
            else {
                $current + '\' + $part
            }
            [void]$filterNames.Add($current)
        }
    }

    if ($filterNames.Count -gt 0) {
        $filterGroup = New-MsbuildElement $document "ItemGroup"
        foreach ($filterName in $filterNames) {
            $filter = New-MsbuildElement $document "Filter"
            [void]$filter.SetAttribute("Include", $filterName)

            $identifier = New-MsbuildElement $document "UniqueIdentifier"
            $identifier.InnerText = New-DeterministicGuid $Specification.Name $filterName
            [void]$filter.AppendChild($identifier)
            [void]$filterGroup.AppendChild($filter)
        }
        [void]$projectElement.AppendChild($filterGroup)
    }

    foreach ($itemType in $Specification.ItemTypes) {
        $itemsOfType = @(Sort-ProjectItems @($Items | Where-Object { $_.ItemType -eq $itemType }))
        if ($itemsOfType.Count -eq 0) {
            continue
        }

        $itemGroup = New-MsbuildElement $document "ItemGroup"
        foreach ($item in $itemsOfType) {
            $itemElement = New-MsbuildElement $document $item.ItemType
            [void]$itemElement.SetAttribute("Include", $item.Include)
            if (-not [string]::IsNullOrWhiteSpace($item.FilterPath)) {
                $filterElement = New-MsbuildElement $document "Filter"
                $filterElement.InnerText = $item.FilterPath
                [void]$itemElement.AppendChild($filterElement)
            }
            [void]$itemGroup.AppendChild($itemElement)
        }
        [void]$projectElement.AppendChild($itemGroup)
    }

    return $document
}

function Get-XmlBytes {
    param([xml]$Document)

    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.IndentChars = "  "
    $settings.NewLineChars = "`r`n"
    $settings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
    $settings.Encoding = New-Object System.Text.UTF8Encoding($true)

    $stream = New-Object System.IO.MemoryStream
    try {
        $writer = [System.Xml.XmlWriter]::Create($stream, $settings)
        try {
            $Document.Save($writer)
        }
        finally {
            $writer.Close()
        }

        return $stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

function Test-BytesEqual {
    param(
        [byte[]]$Left,
        [byte[]]$Right
    )

    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length) {
        return $false
    }

    for ($i = 0; $i -lt $Left.Length; $i++) {
        if ($Left[$i] -ne $Right[$i]) {
            return $false
        }
    }

    return $true
}

$root = Get-RepoRoot $RootDir
$projectSpecifications = @(
    [pscustomobject]@{
        Name = "Application"
        ProjectPath = "Projects\Application\Application.vcxproj"
        FiltersPath = "Projects\Application\Application.vcxproj.filters"
        ItemTypes = @("ClCompile", "ClInclude", "FxCompile", "None")
        IgnoredItemTypes = @("ProjectConfiguration", "ProjectReference")
        Mappings = @(
            [pscustomobject]@{ Root = "Sources\Application"; FilterPrefix = "" }
            [pscustomobject]@{ Root = "Shaders"; FilterPrefix = "Shaders" }
            [pscustomobject]@{ Root = "Externals\Vender\imgui"; FilterPrefix = "ThirdParty\ImGui" }
            [pscustomobject]@{ Root = "Projects\Application\packages.config"; FilterPrefix = "" }
        )
    }
    [pscustomobject]@{
        Name = "GGLabRuntime"
        ProjectPath = "Projects\GGLabRuntime\GGLabRuntime.vcxproj"
        FiltersPath = "Projects\GGLabRuntime\GGLabRuntime.vcxproj.filters"
        ItemTypes = @("ClCompile", "ClInclude")
        IgnoredItemTypes = @("ProjectConfiguration", "ProjectReference")
        Mappings = @(
            [pscustomobject]@{ Root = "Sources\Core"; FilterPrefix = "Core" }
            [pscustomobject]@{ Root = "Sources\Scene"; FilterPrefix = "Scene" }
            [pscustomobject]@{ Root = "Sources\Graphics"; FilterPrefix = "Graphics" }
            [pscustomobject]@{ Root = "Sources\Diagnostics"; FilterPrefix = "Diagnostics" }
            [pscustomobject]@{
                Root = "Externals\Vender\D3D12MemoryAllocator"
                FilterPrefix = "ThirdParty\D3D12MemoryAllocator"
            }
            [pscustomobject]@{
                Root = "Externals\Vender\VulkanMemoryAllocator"
                FilterPrefix = "ThirdParty\VulkanMemoryAllocator"
            }
        )
    }
    [pscustomobject]@{
        Name = "NapaVoxelCore"
        ProjectPath = "Projects\NapaVoxelCore\NapaVoxelCore.vcxproj"
        FiltersPath = "Projects\NapaVoxelCore\NapaVoxelCore.vcxproj.filters"
        ItemTypes = @("ClCompile", "ClInclude")
        IgnoredItemTypes = @("ProjectConfiguration")
        Mappings = @(
            [pscustomobject]@{ Root = "Sources\NapaVoxelCore"; FilterPrefix = "" }
        )
    }
)

$staleFilters = @()
foreach ($specification in $projectSpecifications) {
    $items = Get-ProjectPresentationItems $root $specification
    $document = New-FiltersDocument $specification $items
    $expectedBytes = Get-XmlBytes $document
    $filtersPath = Join-Path $root $specification.FiltersPath
    $currentBytes = if (Test-Path -LiteralPath $filtersPath) {
        [System.IO.File]::ReadAllBytes($filtersPath)
    }
    else {
        $null
    }

    if (Test-BytesEqual $currentBytes $expectedBytes) {
        Write-Host "$($specification.Name): unchanged ($($items.Count) project items)"
        continue
    }

    if ($Check) {
        Write-Host "$($specification.Name): stale ($($items.Count) project items)"
        $staleFilters += $specification.FiltersPath
        continue
    }

    [System.IO.File]::WriteAllBytes($filtersPath, $expectedBytes)
    Write-Host "$($specification.Name): updated ($($items.Count) project items)"
}

if ($staleFilters.Count -gt 0) {
    throw "Generated project filters are stale: $($staleFilters -join ', ')"
}
