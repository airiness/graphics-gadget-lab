param(
    [string]$RootDir = "",
    [string]$BaseRef = "origin/main",
    [switch]$AsJson,
    [switch]$SelfTest
)

# Read-only Atlas navigation, baseline traceability and impact hints.
$ErrorActionPreference = "Stop"

function Invoke-AtlasGit {
    param([string]$Root, [string[]]$Arguments)
    $ErrorActionPreference = "Continue"
    $output = @(& git --no-optional-locks -C $Root @Arguments 2>&1)
    $code = $LASTEXITCODE
    return [pscustomobject]@{ Code = $code; Text = ($output | ForEach-Object { "$($_)" }) -join "`n" }
}

function Get-AtlasHeadingSlugs {
    param([string]$Text)
    $seen = @{}
    $fence = ""
    foreach ($line in ($Text -split '\r?\n')) {
        if ($line -match '^ {0,3}(`{3,}|~{3,})') {
            $marker = $Matches[1].Substring(0, 1)
            if ($fence -eq "") { $fence = $marker }
            elseif ($fence -eq $marker) { $fence = "" }
            continue
        }
        if ($fence -ne "" -or $line -notmatch '^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$') { continue }
        $slug = $Matches[1].ToLowerInvariant().Replace('`', '')
        $slug = $slug -replace '[^\p{L}\p{N}\p{M}\s_-]', '' -replace '\s', '-'
        $count = 0
        if ($seen.ContainsKey($slug)) { $count = $seen[$slug] }
        $seen[$slug] = $count + 1
        if ($count -gt 0) { "$slug-$count" } else { $slug }
    }
}

function Read-AtlasEntries {
    param([string[]]$Lines, [string]$File)
    $scopes = [System.Collections.Generic.List[object]]::new()
    $scopes.Add([pscustomobject]@{ Level = 0; Baseline = ""; Entry = $null })
    $list = ""
    $fence = ""
    foreach ($line in $Lines) {
        if ($line -match '^ {0,3}(`{3,}|~{3,})') {
            $marker = $Matches[1].Substring(0, 1)
            if ($fence -eq "") { $fence = $marker }
            elseif ($fence -eq $marker) { $fence = "" }
            $list = ""
            continue
        }
        if ($fence -eq "" -and $line -match '^(#{1,6})\s+') {
            $level = $Matches[1].Length
            while ($scopes.Count -gt 1 -and $scopes[$scopes.Count - 1].Level -ge $level) {
                $scopes.RemoveAt($scopes.Count - 1)
            }
            $scopes.Add([pscustomobject]@{ Level = $level; Baseline = ""; Entry = $null })
        }
        $scope = $scopes[$scopes.Count - 1]
        if ($fence -eq "" -and $line -match '^Atlas ID: (\S+)\s*$') {
            $baseline = ""
            foreach ($parent in $scopes) {
                if ($parent.Baseline -ne "") { $baseline = $parent.Baseline }
            }
            $entry = [pscustomobject]@{
                Id = $Matches[1]; File = $File; VerifiedAgainst = $baseline
                Anchors = [System.Collections.Generic.List[string]]::new()
                ReviewPaths = [System.Collections.Generic.List[string]]::new()
            }
            $scope.Entry = $entry
            $entry
        }
        if ($fence -eq "" -and $line -match '^\s*(?:-\s*)?VerifiedAgainst:\s*`?([0-9a-fA-F]{7,40})`?\s*$') {
            $scope.Baseline = $Matches[1]
            if ($null -ne $scope.Entry) { $scope.Entry.VerifiedAgainst = $Matches[1] }
        }
        $owner = $null
        foreach ($parent in $scopes) {
            if ($null -ne $parent.Entry) { $owner = $parent.Entry }
        }
        if ($line -eq "Anchors:" -or $line -eq "ReviewPaths:") {
            $list = $line.TrimEnd(':')
        }
        elseif ($list -ne "" -and $line -match '^  - (.+)$' -and $null -ne $owner) {
            $owner.$list.Add($Matches[1])
        }
        else { $list = "" }
    }
}

function Test-AtlasPathMatch {
    param([string]$Path, [string]$Pattern)
    $patternText = [regex]::Escape($Pattern.Replace('\', '/'))
    $patternText = $patternText.Replace('\*\*/', '(?:.*/)?').Replace('\*\*', '.*')
    $patternText = $patternText.Replace('\*', '[^/]*').Replace('\?', '[^/]')
    return $Path.Replace('\', '/') -imatch ("^" + $patternText + "$")
}

function Read-AtlasChanges {
    param([string]$Text, [string]$Origin)
    $tokens = $Text -split "`0"
    for ($i = 0; $i -lt $tokens.Count -and $tokens[$i] -ne "";) {
        $kind = $tokens[$i++]
        $count = 1
        if ($kind -match '^[RC]') { $count = 2 }
        for ($j = 0; $j -lt $count; $j++) {
            if ($i -ge $tokens.Count -or $tokens[$i] -eq "") { throw "Incomplete Git name-status record." }
            [pscustomobject]@{ Path = $tokens[$i++]; Kind = $kind; Origin = $Origin }
        }
    }
}

function Get-AtlasImpactReasons {
    param($Entry, [object[]]$Changes)
    # The containing file is not implicit; honor any dependency declared on it.
    $patterns = @($Entry.Anchors | ForEach-Object { ($_ -split '::', 2)[0] }) + @($Entry.ReviewPaths)
    foreach ($change in $Changes) {
        foreach ($pattern in $patterns) {
            if (Test-AtlasPathMatch $change.Path $pattern) {
                "$($change.Origin) $($change.Kind): $($change.Path) matches $pattern"
                break
            }
        }
    }
}

function Get-AtlasExternalReferences {
    param([string]$Text)
    # Known design-repository names and explicit reading instructions only; publication
    # review covers any other wording.
    $pattern = '(?i)graphics-gadget-lab-docs|GraphicsGadgetLabDocs|gglab-docs|\bGGLab_\w+\.md\b|' +
        '\b(?:documentation|design|docs) repo(?:sitory)?\b|\bbootstrap protocol\b'
    $number = 0
    foreach ($line in ($Text -split '\r?\n')) {
        $number++
        foreach ($match in [regex]::Matches($line, $pattern)) { "line ${number}: $($match.Value)" }
    }
}

function Get-AtlasAncestry {
    param([bool]$ObjectAvailable, [int]$AncestorExit, [bool]$Shallow)
    if (-not $ObjectAvailable -or $AncestorExit -gt 1) { return "EvidenceIncomplete" }
    if ($AncestorExit -eq 0) { return "Ancestor" }
    if ($Shallow) { return "EvidenceIncomplete" }
    return "NonAncestor"
}

function Get-AtlasAnchorIssue {
    param([bool]$FileExists, [string]$Text, [string]$Symbol)
    if (-not $FileExists) { return "MissingAnchorFile" }
    $name = ($Symbol -split '::')[-1]
    if ($name -and -not $Text.Contains($name)) { return "MissingSymbolText" }
}

function Invoke-AtlasSelfTests {
    $checks = [System.Collections.Generic.List[bool]]::new()
    $checks.Add((Test-AtlasPathMatch 'Sources/A.cpp' 'Sources/*.cpp'))
    $checks.Add((-not (Test-AtlasPathMatch 'Sources/Sub/A.cpp' 'Sources/*.cpp')))
    $checks.Add((Test-AtlasPathMatch 'Sources/A.cpp' 'Sources/**/*.cpp'))
    $checks.Add((Test-AtlasPathMatch 'Sources/Sub/A.cpp' 'Sources/**/*.cpp'))
    $checks.Add((-not (Test-AtlasPathMatch 'Other/A.cpp' 'Sources/**/*.cpp')))
    $checks.Add((Test-AtlasPathMatch 'Sources/A.cpp' 'sources\A.cpp'))
    $changes = @(Read-AtlasChanges "R100`0old name.cpp`0new name.cpp`0D`0gone.cpp`0" "Staged")
    $checks.Add(($changes.Count -eq 3 -and $changes[0].Path -eq 'old name.cpp'))
    $checks.Add(($changes[1].Path -eq 'new name.cpp' -and $changes[2].Kind -eq 'D'))
    $fixture = @('# Map', 'Atlas ID: atlas.fixture.map', '- VerifiedAgainst: `1234567`',
        '## A', 'Atlas ID: atlas.fixture.a', '- VerifiedAgainst: `7654321`',
        '```text', 'Anchors:', '  - old name.cpp::Owner::Run', 'ReviewPaths:',
        '  - Tests/*.cpp', '```', '## B', 'Atlas ID: atlas.fixture.b')
    $entries = @(Read-AtlasEntries $fixture 'Docs/Map.md')
    $checks.Add(($entries.Count -eq 3 -and $entries[0].VerifiedAgainst -eq '1234567'))
    $checks.Add(($entries[1].VerifiedAgainst -eq '7654321' -and $entries[2].VerifiedAgainst -eq '1234567'))
    $checks.Add(($entries[1].Anchors.Count -eq 1 -and $entries[1].ReviewPaths.Count -eq 1))
    $checks.Add((@(Get-AtlasImpactReasons $entries[1] $changes).Count -eq 1))
    foreach ($origin in @('Committed', 'Staged', 'Unstaged', 'Untracked')) {
        $checks.Add((@(Get-AtlasImpactReasons $entries[1] @(
            [pscustomobject]@{ Path = 'Tests/New.cpp'; Kind = 'A'; Origin = $origin })).Count -eq 1))
    }
    $checks.Add((@(Get-AtlasImpactReasons $entries[1] @(
        [pscustomobject]@{ Path = 'Unrelated.cpp'; Kind = 'M'; Origin = 'Unstaged' })).Count -eq 0))
    $checks.Add((@(Get-AtlasImpactReasons $entries[1] @(
        [pscustomobject]@{ Path = 'Docs/Map.md'; Kind = 'M'; Origin = 'Unstaged' })).Count -eq 0))
    $globEntry = [pscustomobject]@{ File = 'Docs/Map.md'; Anchors = @(); ReviewPaths = @('Docs/*.md') }
    $checks.Add((@(Get-AtlasImpactReasons $globEntry @(
        [pscustomobject]@{ Path = 'Docs/Map.md'; Kind = 'M'; Origin = 'Staged' },
        [pscustomobject]@{ Path = 'Docs/Other.md'; Kind = 'M'; Origin = 'Staged' })).Count -eq 2))
    $sameFileChange = @([pscustomobject]@{ Path = 'Docs/Map.md'; Kind = 'M'; Origin = 'Unstaged' })
    $reviewEntry = [pscustomobject]@{ File = 'Docs/Map.md'; Anchors = @(); ReviewPaths = @('Docs/Map.md') }
    $checks.Add((@(Get-AtlasImpactReasons $reviewEntry $sameFileChange).Count -eq 1))
    $anchorEntry = [pscustomobject]@{ File = 'Docs/Map.md'; Anchors = @('Docs/Map.md::Contract'); ReviewPaths = @() }
    $checks.Add((@(Get-AtlasImpactReasons $anchorEntry $sameFileChange).Count -eq 1))
    $checks.Add(((Get-AtlasAnchorIssue $false '' 'Owner::Run') -eq 'MissingAnchorFile'))
    $checks.Add(($null -eq (Get-AtlasAnchorIssue $true 'void Run() {}' 'Owner::Run')))
    $checks.Add(((Get-AtlasAnchorIssue $true 'class Owner {};' 'Owner::Run') -eq 'MissingSymbolText'))
    $checks.Add(((Get-AtlasAncestry $true 0 $false) -eq 'Ancestor'))
    $checks.Add(((Get-AtlasAncestry $true 1 $false) -eq 'NonAncestor'))
    $checks.Add(((Get-AtlasAncestry $false 1 $false) -eq 'EvidenceIncomplete'))
    $checks.Add(((Get-AtlasAncestry $true 1 $true) -eq 'EvidenceIncomplete'))
    $checks.Add(((Get-AtlasAncestry $true 128 $false) -eq 'EvidenceIncomplete'))
    $slugs = @(Get-AtlasHeadingSlugs "# A / B`n# A / B`n# Contract ``Run```n``````text`n# Hidden`n``````")
    $checks.Add(($slugs.Count -eq 3 -and $slugs[1] -eq 'a--b-1' -and $slugs[2] -eq 'contract-run'))
    $references = @(Get-AtlasExternalReferences (
        "Read [it](https://github.com/airiness/graphics-gadget-lab-docs/blob/main/GGLab_Plan.md).`n" +
        "Ask the documentation repository.`nSee the design repo or docs repository.`n" +
        "Follow the Bootstrap Protocol."))
    $checks.Add(($references.Count -eq 6 -and $references[0] -eq 'line 1: graphics-gadget-lab-docs'))
    $checks.Add(($references[2] -eq 'line 2: documentation repository' -and $references[5] -like 'line 4: *'))
    $checks.Add((@(Get-AtlasExternalReferences ("Documentation-only changes keep the baseline.`n" +
        "See Docs/Architecture/README.md in the graphics-gadget-lab repository for the design reason.")).Count -eq 0))
    if ($checks.Contains($false)) { throw "Atlas self-tests failed: $($checks.IndexOf($false) + 1)" }
    Write-Output "SELF-TEST PASS - Atlas navigation/impact/baseline/publication predicates ($($checks.Count) checks)."
}

if ($SelfTest) {
    Invoke-AtlasSelfTests
    exit 0
}

$rootInput = $RootDir
if ([string]::IsNullOrWhiteSpace($rootInput)) { $rootInput = Join-Path $PSScriptRoot '..' }
$root = (Resolve-Path -LiteralPath $rootInput).Path
$head = Invoke-AtlasGit $root @('rev-parse', '--verify', 'HEAD')
if ($head.Code -ne 0) { throw "Cannot read repository HEAD: $($head.Text)" }
$files = @('AGENTS.md', 'README.md')
$files += @(Get-ChildItem -LiteralPath (Join-Path $root 'Docs/Architecture') -Filter '*.md' -Recurse |
    ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') })
$files += @(Get-ChildItem -LiteralPath (Join-Path $root '.agents/skills/gglab-architecture') -Filter '*.md' -Recurse |
    ForEach-Object { $_.FullName.Substring($root.Length + 1).Replace('\', '/') })
$entries = @()
$issues = [System.Collections.Generic.List[object]]::new()
$publication = [System.Collections.Generic.List[object]]::new()
$linkCount = 0
foreach ($file in $files) {
    $full = Join-Path $root $file
    $text = [System.IO.File]::ReadAllText($full)
    $entries += @(Read-AtlasEntries ($text -split '\r?\n') $file)
    foreach ($reference in @(Get-AtlasExternalReferences $text)) {
        $publication.Add([pscustomobject]@{ File = $file; Kind = 'ExternalDocsReference'; Target = $reference })
    }
    foreach ($match in [regex]::Matches($text, '\]\(([^)\s]+)\)')) {
        $target = $match.Groups[1].Value
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') { continue }
        $linkCount++
        $parts = $target -split '#', 2
        $resolved = [System.IO.Path]::GetFullPath((Join-Path (Split-Path $full) ([uri]::UnescapeDataString($parts[0]))))
        if ($parts[0] -eq '') { $resolved = $full }
        $kind = ''
        if (-not $resolved.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $kind = 'OutsideRepository'
        }
        elseif (-not (Test-Path -LiteralPath $resolved)) { $kind = 'MissingLinkFile' }
        elseif ($parts.Count -gt 1 -and $parts[1] -ne '') {
            $slugs = @(Get-AtlasHeadingSlugs ([System.IO.File]::ReadAllText($resolved)))
            if ($slugs -cnotcontains ([uri]::UnescapeDataString($parts[1]))) { $kind = 'MissingHeadingAnchor' }
        }
        if ($kind) { $issues.Add([pscustomobject]@{ File = $file; Kind = $kind; Target = $target }) }
    }
}
$ids = @{}
foreach ($entry in $entries) {
    if ($ids.ContainsKey($entry.Id)) {
        $issues.Add([pscustomobject]@{ File = $entry.File; Kind = 'DuplicateAtlasId'; Target = $entry.Id })
    }
    $ids[$entry.Id] = $true
    foreach ($anchor in $entry.Anchors) {
        $parts = $anchor -split '::', 2
        $full = Join-Path $root $parts[0]
        $exists = Test-Path -LiteralPath $full -PathType Leaf
        $text = ''
        if ($exists) { $text = [System.IO.File]::ReadAllText($full) }
        $symbol = ''
        if ($parts.Count -gt 1) { $symbol = $parts[1] }
        $kind = Get-AtlasAnchorIssue $exists $text $symbol
        if ($kind) { $issues.Add([pscustomobject]@{ File = $entry.File; Kind = $kind; Target = $anchor }) }
    }
}
$baselineResults = @()
$shallow = Invoke-AtlasGit $root @('rev-parse', '--is-shallow-repository')
foreach ($baseline in @($entries.VerifiedAgainst | Sort-Object -Unique)) {
    $object = Invoke-AtlasGit $root @('rev-parse', '--verify', '--end-of-options', "$baseline`^{commit}")
    $ancestry = [pscustomobject]@{ Code = 128 }
    if ($object.Code -eq 0) {
        $ancestry = Invoke-AtlasGit $root @('merge-base', '--is-ancestor', $object.Text, $head.Text)
    }
    $status = Get-AtlasAncestry ($object.Code -eq 0) $ancestry.Code ($shallow.Code -ne 0 -or $shallow.Text -eq 'true')
    $availability = 'Available'
    $reason = 'Available commit; ancestry establishes traceability only.'
    if ($object.Code -ne 0) {
        $availability = 'EvidenceIncomplete'
        $reason = 'Baseline commit unavailable in the local checkout.'
    }
    elseif ($status -eq 'EvidenceIncomplete') {
        $reason = 'Ancestry could not be established with the available local history.'
    }
    elseif ($status -eq 'NonAncestor') { $reason = 'Proven non-ancestor; review represented content and history.' }
    $baselineResults += [pscustomobject]@{
        VerifiedAgainst = $baseline; Availability = $availability; Ancestry = $status; Reason = $reason
    }
}
$changes = @()
$incomplete = [System.Collections.Generic.List[string]]::new()
$comparison = Invoke-AtlasGit $root @('rev-parse', '--verify', '--end-of-options', "$BaseRef`^{commit}")
$mergeBase = [pscustomobject]@{ Code = 128; Text = '' }
if ($comparison.Code -eq 0) { $mergeBase = Invoke-AtlasGit $root @('merge-base', $comparison.Text, $head.Text) }
if ($mergeBase.Code -eq 0) {
    $diff = Invoke-AtlasGit $root @('diff', '--name-status', '-z', '--find-renames', '--ignore-submodules=all', $mergeBase.Text, 'HEAD', '--')
    if ($diff.Code -eq 0) { $changes += @(Read-AtlasChanges $diff.Text 'Committed') }
    else { $incomplete.Add('Committed change paths unavailable.') }
}
else { $incomplete.Add("Merge base unavailable for $BaseRef; committed impact coverage is incomplete.") }
foreach ($layer in @('Staged', 'Unstaged')) {
    $arguments = @('diff', '--name-status', '-z', '--find-renames', '--ignore-submodules=all')
    if ($layer -eq 'Staged') { $arguments += @('--cached', 'HEAD') }
    $diff = Invoke-AtlasGit $root ($arguments + '--')
    if ($diff.Code -eq 0) { $changes += @(Read-AtlasChanges $diff.Text $layer) }
    else { $incomplete.Add("$layer change paths unavailable.") }
}
$untracked = Invoke-AtlasGit $root @('ls-files', '--others', '--exclude-standard', '-z')
if ($untracked.Code -eq 0) {
    foreach ($file in ($untracked.Text -split "`0" | Where-Object { $_ -ne '' })) {
        $changes += [pscustomobject]@{ Path = $file; Kind = 'A'; Origin = 'Untracked' }
    }
}
else { $incomplete.Add('Untracked change paths unavailable.') }
$candidates = @()
foreach ($entry in $entries) {
    $reasons = @(Get-AtlasImpactReasons $entry $changes | Sort-Object -Unique)
    if ($reasons.Count -gt 0) {
        $candidates += [pscustomobject]@{ Id = $entry.Id; File = $entry.File; Status = 'ReviewRequired'; Reasons = $reasons }
    }
}
$report = [pscustomobject]@{
    Head = $head.Text; BaseRef = $BaseRef; MergeBase = $mergeBase.Text
    ImpactCoverage = $(if ($incomplete.Count -gt 0) { 'EvidenceIncomplete' } else { 'Available' })
    IncompleteReasons = @($incomplete.ToArray()); Baselines = @($baselineResults)
    CheckedFiles = $files.Count; LocalLinks = $linkCount; Entries = $entries.Count
    NavigationIssues = @($issues.ToArray()); PublicationFindings = @($publication.ToArray())
    ChangedPaths = @($changes); ReviewCandidates = @($candidates)
}
if ($AsJson) { $report | ConvertTo-Json -Depth 8 }
else {
    Write-Output "Atlas report: $($entries.Count) entries, $linkCount local links; impact coverage $($report.ImpactCoverage)."
    foreach ($baseline in $baselineResults) {
        Write-Output "Baseline $($baseline.VerifiedAgainst): availability $($baseline.Availability), ancestry $($baseline.Ancestry). $($baseline.Reason)"
    }
    foreach ($issue in @($issues) + @($publication)) { Write-Output "$($issue.Kind): $($issue.File) -> $($issue.Target)" }
    foreach ($candidate in $candidates) {
        Write-Output "ReviewRequired: $($candidate.Id)"
        foreach ($reason in $candidate.Reasons) { Write-Output "  $reason" }
    }
    foreach ($reason in $incomplete) { Write-Output "EvidenceIncomplete: $reason" }
    Write-Output 'Review source and contracts for semantic freshness; empty candidates do not establish it.'
}
