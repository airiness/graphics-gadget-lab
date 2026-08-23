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

    $nodes = @($Document.SelectNodes("//msb:None[contains(@Include, '\Shaders\') or contains(@Include, '/Shaders/')]", $NamespaceManager))
    $nodes += @($Document.SelectNodes("//msb:FxCompile[contains(@Include, '\Shaders\') or contains(@Include, '/Shaders/')]", $NamespaceManager))

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

function Test-BytesEqual {
    param(
        [byte[]]$Left,
        [byte[]]$Right
    )

    if ($Left.Length -ne $Right.Length) {
        return $false
    }

    for ($i = 0; $i -lt $Left.Length; $i++) {
        if ($Left[$i] -ne $Right[$i]) {
            return $false
        }
    }

    return $true
}

function Save-XmlDocumentIfChanged {
    param(
        [System.Xml.XmlDocument]$Document,
        [string]$Path
    )

    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.IndentChars = "  "
    $settings.NewLineChars = "`r`n"
    $settings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)

    $stream = New-Object System.IO.MemoryStream
    try {
        $writer = [System.Xml.XmlWriter]::Create($stream, $settings)
        try {
            $Document.Save($writer)
        }
        finally {
            $writer.Close()
        }

        $newBytes = $stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }

    $oldBytes = [System.IO.File]::ReadAllBytes($Path)
    if (Test-BytesEqual $oldBytes $newBytes) {
        return $false
    }

    [System.IO.File]::WriteAllBytes($Path, $newBytes)
    return $true
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

$root = Get-RepoRoot $RootDir
$shaderDir = Join-Path $root "Shaders"
$projectDir = Join-Path $root "Projects\WinApp"
$projectPath = Join-Path $projectDir "WinApp.vcxproj"

if (-not (Test-Path $shaderDir)) {
    throw "Shader directory not found: $shaderDir"
}
if (-not (Test-Path $projectPath)) {
    throw "Project file not found: $projectPath"
}

$shaderFiles = Get-ChildItem $shaderDir -Recurse -File |
    Where-Object { $_.Extension -in @(".hlsl", ".hlsli", ".json") } |
    Sort-Object FullName |
    ForEach-Object {
        [pscustomobject]@{
            FullName = $_.FullName
            Extension = $_.Extension
            ProjectInclude = ConvertTo-ProjectInclude $projectDir $_.FullName
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
$projectChanged = Save-XmlDocumentIfChanged $projectDoc $projectPath

$filterGeneratorPath = Join-Path $PSScriptRoot "GenerateProjectFilters.ps1"
if (-not (Test-Path -LiteralPath $filterGeneratorPath)) {
    throw "Project filter generator not found: $filterGeneratorPath"
}
& $filterGeneratorPath -RootDir $root

Write-Host "Synced $($shaderFiles.Count) shader files to Visual Studio project."
Write-Host "WinApp.vcxproj: $(if ($projectChanged) { 'updated' } else { 'unchanged' })"
