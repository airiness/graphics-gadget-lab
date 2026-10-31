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
        [string]$Root,
        [string]$ProjectPath,
        [xml]$Document
    )

    $items = @(Get-ShaderProjectItems $Root $ProjectPath $Document)
    foreach ($item in $items) {
        [void]$item.Node.ParentNode.RemoveChild($item.Node)
    }
    return $items.Count
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

    $nodes = @($Document.SelectNodes("//msb:ItemGroup[not(*)]", $NamespaceManager))
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
    [void]$noneGroup.SetAttribute("Label", "ShaderSources")
    [void]$fxGroup.SetAttribute("Label", "ShaderSources")
    [void]$noneGroup.SetAttribute("Condition", $ShaderFiles[0].Condition)
    [void]$fxGroup.SetAttribute("Condition", $ShaderFiles[0].Condition)

    foreach ($shader in $ShaderFiles) {
        $itemName = $shader.ItemType
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
Import-Module (Join-Path $PSScriptRoot 'ShaderProjectOwnership.psm1') -Force
$assignments = @(Get-ShaderProjectAssignments $root)

# Validate all routing destinations before changing any project. Existing project
# membership remains authoritative; routing regenerates only the shader entries.
foreach ($owner in ($assignments.Owner | Select-Object -Unique)) {
    $ownerPath = Join-Path $root "Projects/$owner/$owner.vcxproj"
    if (-not (Test-Path -LiteralPath $ownerPath -PathType Leaf)) {
        throw "Shader owner project not found: $ownerPath"
    }
}
$plans = @()
foreach ($project in (Get-ChildItem -LiteralPath (Join-Path $root 'Projects') -Recurse -Filter '*.vcxproj' |
        Sort-Object FullName)) {
    $projectDoc = Read-XmlDocument $project.FullName
    $removed = Remove-ShaderItems $root $project.FullName $projectDoc
    $ownedShaders = @($assignments | Where-Object {
        $ownerPath = Join-Path $root "Projects/$($_.Owner)/$($_.Owner).vcxproj"
        [IO.Path]::GetFullPath($ownerPath) -ieq $project.FullName
    })
    if ($removed -eq 0 -and $ownedShaders.Count -eq 0) { continue }
    $projectDir = $project.DirectoryName
    $shaderFiles = @($ownedShaders | ForEach-Object {
        [pscustomobject]@{
            ItemType = $_.ItemType
            Condition = $_.Condition
            ProjectInclude = ConvertTo-ProjectInclude $projectDir $_.FullName
        }
    })
    $projectNs = New-Object System.Xml.XmlNamespaceManager($projectDoc.NameTable)
    $projectNs.AddNamespace('msb', $projectDoc.DocumentElement.NamespaceURI)
    Remove-EmptyItemGroups $projectDoc $projectNs
    if ($shaderFiles.Count -gt 0) { Add-ShaderItemsToProject $projectDoc $shaderFiles }
    Remove-WhitespaceTextNodes $projectDoc
    $plans += [pscustomobject]@{ Path=$project.FullName; Name=$project.BaseName;
        Document=$projectDoc; Count=$shaderFiles.Count }
}
foreach ($plan in $plans) {
    $changed = Save-XmlDocumentIfChanged $plan.Document $plan.Path
    Write-Host "$($plan.Name): $(if ($changed) { 'updated' } else { 'unchanged' }) ($($plan.Count) shader items)"
}

$filterGeneratorPath = Join-Path $PSScriptRoot "GenerateProjectFilters.ps1"
if (-not (Test-Path -LiteralPath $filterGeneratorPath)) {
    throw "Project filter generator not found: $filterGeneratorPath"
}
& $filterGeneratorPath -RootDir $root

$findings = @(Get-ShaderProjectOwnershipFindings $root)
if ($findings.Count -gt 0) {
    throw "Shader ownership validation failed after synchronization: $($findings | Out-String)"
}
Write-Host "Synced $($assignments.Count) shader files to their owning Visual Studio projects."
