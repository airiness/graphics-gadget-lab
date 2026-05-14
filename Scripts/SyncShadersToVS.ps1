param(
    [string]$RootDir = ""
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

function ConvertTo-ProjectInclude {
    param(
        [string]$ProjectDir,
        [string]$FilePath
    )

    $projectUri = [System.Uri]((Resolve-Path $ProjectDir).Path.TrimEnd('\') + '\')
    $fileUri = [System.Uri]((Resolve-Path $FilePath).Path)
    return [System.Uri]::UnescapeDataString($projectUri.MakeRelativeUri($fileUri).ToString()).Replace('/', '\')
}

function New-DeterministicGuid {
    param([string]$Text)

    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text.ToLowerInvariant())
        $hash = $md5.ComputeHash($bytes)
        return "{" + ([System.Guid]::new($hash)).ToString().ToUpperInvariant() + "}"
    }
    finally {
        $md5.Dispose()
    }
}

function New-MsbuildElement {
    param(
        [xml]$Document,
        [string]$Name
    )

    return $Document.CreateElement($Name, $Document.DocumentElement.NamespaceURI)
}

function Remove-ShaderItems {
    param(
        [xml]$Document,
        [System.Xml.XmlNamespaceManager]$NamespaceManager
    )

    $nodes = @($Document.SelectNodes("//msb:None[starts-with(@Include, '..\Assets\Shaders\') or starts-with(@Include, '../Assets/Shaders/')]", $NamespaceManager))
    $nodes += @($Document.SelectNodes("//msb:FxCompile[starts-with(@Include, '..\Assets\Shaders\') or starts-with(@Include, '../Assets/Shaders/')]", $NamespaceManager))

    foreach ($node in $nodes) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}

function Remove-ShaderFilters {
    param(
        [xml]$Document,
        [System.Xml.XmlNamespaceManager]$NamespaceManager
    )

    $nodes = @($Document.SelectNodes("//msb:Filter[starts-with(@Include, 'Shaders')]", $NamespaceManager))
    foreach ($node in $nodes) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}

function Read-XmlDocument {
    param([string]$Path)

    $document = New-Object System.Xml.XmlDocument
    $document.PreserveWhitespace = $false
    $document.Load($Path)
    return $document
}

function Save-XmlDocument {
    param(
        [System.Xml.XmlDocument]$Document,
        [string]$Path
    )

    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.IndentChars = "  "
    $settings.NewLineChars = "`r`n"
    $settings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
    $settings.Encoding = New-Object System.Text.UTF8Encoding($true)

    $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
    try {
        $Document.Save($writer)
    }
    finally {
        $writer.Close()
    }
}

function Remove-WhitespaceTextNodes {
    param([System.Xml.XmlNode]$Node)

    $children = @($Node.ChildNodes)
    foreach ($child in $children) {
        if ($child.NodeType -eq [System.Xml.XmlNodeType]::Text -and [string]::IsNullOrWhiteSpace($child.Value)) {
            [void]$Node.RemoveChild($child)
            continue
        }

        Remove-WhitespaceTextNodes $child
    }
}

function Remove-EmptyItemGroups {
    param(
        [xml]$Document,
        [System.Xml.XmlNamespaceManager]$NamespaceManager
    )

    $nodes = @($Document.SelectNodes("//msb:ItemGroup[not(@*) and not(*)]", $NamespaceManager))
    foreach ($node in $nodes) {
        [void]$node.ParentNode.RemoveChild($node)
    }
}

function Insert-BeforeCppTargetsImport {
    param(
        [xml]$Document,
        [System.Xml.XmlElement]$Element
    )

    $targetsImport = $Document.Project.ChildNodes |
        Where-Object { $_.LocalName -eq "Import" -and $_.GetAttribute("Project") -eq '$(VCTargetsPath)\Microsoft.Cpp.targets' } |
        Select-Object -First 1

    if ($targetsImport) {
        [void]$Document.Project.InsertBefore($Element, $targetsImport)
    }
    else {
        [void]$Document.Project.AppendChild($Element)
    }
}

function Add-ShaderItemsToProject {
    param(
        [xml]$Document,
        [array]$ShaderFiles
    )

    $noneGroup = New-MsbuildElement $Document "ItemGroup"
    $fxGroup = New-MsbuildElement $Document "ItemGroup"

    foreach ($shader in $ShaderFiles) {
        $itemName = if ($shader.Extension -ieq ".hlsl") { "FxCompile" } else { "None" }
        $item = New-MsbuildElement $Document $itemName
        [void]$item.SetAttribute("Include", $shader.ProjectInclude)

        if ($itemName -eq "FxCompile") {
            [void]$fxGroup.AppendChild($item)
        }
        else {
            [void]$noneGroup.AppendChild($item)
        }
    }

    if ($noneGroup.ChildNodes.Count -gt 0) {
        Insert-BeforeCppTargetsImport $Document $noneGroup
    }
    if ($fxGroup.ChildNodes.Count -gt 0) {
        Insert-BeforeCppTargetsImport $Document $fxGroup
    }
}

function Add-ShaderItemsToFilters {
    param(
        [xml]$Document,
        [array]$ShaderFiles,
        [string]$ShaderRoot
    )

    $filterGroup = New-MsbuildElement $Document "ItemGroup"
    $noneGroup = New-MsbuildElement $Document "ItemGroup"
    $fxGroup = New-MsbuildElement $Document "ItemGroup"

    $filterNames = New-Object System.Collections.Generic.SortedSet[string]
    [void]$filterNames.Add("Shaders")

    foreach ($shader in $ShaderFiles) {
        $relativeDir = Split-Path $shader.ShaderRelativePath -Parent
        if (-not [string]::IsNullOrWhiteSpace($relativeDir)) {
            $parts = $relativeDir -split '[\\/]'
            $current = "Shaders"
            foreach ($part in $parts) {
                if ([string]::IsNullOrWhiteSpace($part)) {
                    continue
                }
                $current = "$current\$part"
                [void]$filterNames.Add($current)
            }
        }
    }

    foreach ($filterName in $filterNames) {
        $filter = New-MsbuildElement $Document "Filter"
        [void]$filter.SetAttribute("Include", $filterName)

        $guid = New-MsbuildElement $Document "UniqueIdentifier"
        $guid.InnerText = New-DeterministicGuid $filterName
        [void]$filter.AppendChild($guid)

        [void]$filterGroup.AppendChild($filter)
    }

    foreach ($shader in $ShaderFiles) {
        $itemName = if ($shader.Extension -ieq ".hlsl") { "FxCompile" } else { "None" }
        $item = New-MsbuildElement $Document $itemName
        [void]$item.SetAttribute("Include", $shader.ProjectInclude)

        $relativeDir = Split-Path $shader.ShaderRelativePath -Parent
        $filterName = if ([string]::IsNullOrWhiteSpace($relativeDir)) {
            "Shaders"
        }
        else {
            "Shaders\" + $relativeDir.Replace('/', '\')
        }

        $filter = New-MsbuildElement $Document "Filter"
        $filter.InnerText = $filterName
        [void]$item.AppendChild($filter)

        if ($itemName -eq "FxCompile") {
            [void]$fxGroup.AppendChild($item)
        }
        else {
            [void]$noneGroup.AppendChild($item)
        }
    }

    if ($filterGroup.ChildNodes.Count -gt 0) {
        [void]$Document.Project.InsertBefore($filterGroup, $Document.Project.FirstChild)
    }
    if ($noneGroup.ChildNodes.Count -gt 0) {
        [void]$Document.Project.AppendChild($noneGroup)
    }
    if ($fxGroup.ChildNodes.Count -gt 0) {
        [void]$Document.Project.AppendChild($fxGroup)
    }
}

$root = Get-RepoRoot $RootDir
$shaderDir = Join-Path $root "Assets\Shaders"
$projectDir = Join-Path $root "Application"
$projectPath = Join-Path $projectDir "Application.vcxproj"
$filtersPath = Join-Path $projectDir "Application.vcxproj.filters"

if (-not (Test-Path $shaderDir)) {
    throw "Shader directory not found: $shaderDir"
}
if (-not (Test-Path $projectPath)) {
    throw "Project file not found: $projectPath"
}
if (-not (Test-Path $filtersPath)) {
    throw "Filters file not found: $filtersPath"
}

$shaderFiles = Get-ChildItem $shaderDir -Recurse -File |
    Where-Object { $_.Extension -in @(".hlsl", ".hlsli", ".json") } |
    Sort-Object FullName |
    ForEach-Object {
        $shaderRootUri = [System.Uri]((Resolve-Path $shaderDir).Path.TrimEnd('\') + '\')
        $fileUri = [System.Uri]($_.FullName)
        $shaderRelative = [System.Uri]::UnescapeDataString($shaderRootUri.MakeRelativeUri($fileUri).ToString()).Replace('/', '\')

        [pscustomobject]@{
            FullName = $_.FullName
            Extension = $_.Extension
            ProjectInclude = ConvertTo-ProjectInclude $projectDir $_.FullName
            ShaderRelativePath = $shaderRelative
        }
    }

$projectDoc = Read-XmlDocument $projectPath
$projectNs = New-Object System.Xml.XmlNamespaceManager($projectDoc.NameTable)
$projectNs.AddNamespace("msb", $projectDoc.DocumentElement.NamespaceURI)
Remove-ShaderItems $projectDoc $projectNs
Remove-EmptyItemGroups $projectDoc $projectNs
Add-ShaderItemsToProject $projectDoc $shaderFiles
Remove-EmptyItemGroups $projectDoc $projectNs
Remove-WhitespaceTextNodes $projectDoc
Save-XmlDocument $projectDoc $projectPath

$filtersDoc = Read-XmlDocument $filtersPath
$filtersNs = New-Object System.Xml.XmlNamespaceManager($filtersDoc.NameTable)
$filtersNs.AddNamespace("msb", $filtersDoc.DocumentElement.NamespaceURI)
Remove-ShaderItems $filtersDoc $filtersNs
Remove-ShaderFilters $filtersDoc $filtersNs
Remove-EmptyItemGroups $filtersDoc $filtersNs
Add-ShaderItemsToFilters $filtersDoc $shaderFiles $shaderDir
Remove-EmptyItemGroups $filtersDoc $filtersNs
Remove-WhitespaceTextNodes $filtersDoc
Save-XmlDocument $filtersDoc $filtersPath

Write-Host "Synced $($shaderFiles.Count) shader files to Visual Studio project."
