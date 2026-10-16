<#
.SYNOPSIS
Checks default non-finite scans of baked/cache-restored IBL and active atmosphere generations.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sessionScript = Join-Path $root 'Scripts/Verification/GGLabSession.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteGenerations' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$cases = @()

function Save([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), $utf8)
}

function Call([string]$Verb, [string]$Session, [hashtable]$Options = @{}) {
    $json = ($Options | ConvertTo-Json -Compress -Depth 12).Replace("'", "''")
    $script = $sessionScript.Replace("'", "''")
    $command = "`$ProgressPreference='SilentlyContinue'; `$p=@{}; `$data='$json'|ConvertFrom-Json; " +
        "foreach (`$v in `$data.PSObject.Properties) {`$p[`$v.Name]=`$v.Value}; & '$script' '$Verb' -Session '$Session' @p"
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command)))
    if ($LASTEXITCODE) { throw "Session $Session $Verb failed: $text" }
    return ($text -join "`n") | ConvertFrom-Json
}

function Wait-Generations([string]$Session, [bool]$Atmosphere, [long]$After = 0) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $status = Call status $Session
        $report = $status.watch.report
        if ($null -ne $report.firstAnomaly) { throw "Unexpected anomaly: $($report.firstAnomaly | ConvertTo-Json -Compress -Depth 16)" }
        $targets = @($report.targets | Where-Object { $_.watchTier -eq 2 -and $null -ne $_.generation })
        $expected = if ($Atmosphere) { 6 } else { 3 }
        $clean = $report.boundary -eq 'Passed' -and $report.production -eq 'Passed' -and
            @($targets | Group-Object semantic).Count -eq $expected
        foreach ($target in $targets) {
            $clean = $clean -and $target.lastCheckedFrameSerial -gt $After -and $target.checked -gt 0 -and
                $target.claim -in @('Passed', 'NotApplicable')
        }
        if ($clean) { return $status }
        if ((Get-Date) -ge $deadline) { throw "Generation scans did not complete: $($report | ConvertTo-Json -Compress -Depth 20)" }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

function Fingerprint($Status) {
    return @($Status.watch.report.targets | Where-Object { $_.watchTier -eq 2 -and $null -ne $_.generation } |
        ForEach-Object { '{0}|{1}|{2}|{3}|{4}|{5}' -f $_.semantic, $_.generation,
            $_.resourceIdentity.index, $_.resourceIdentity.generation, $_.address.mip, $_.lastCheckedFrameSerial } |
        Sort-Object) -join ';'
}

try {
    foreach ($backend in $Rhi) {
        foreach ($mode in @('bake', 'cache', 'atmosphere')) {
            $directory = Join-Path $output "$Configuration-$backend-$mode"
            New-Item -ItemType Directory -Path $directory | Out-Null
            $state = Join-Path $output "$backend-$(if ($mode -eq 'atmosphere') { 'sky' } else { 'ibl' })-state"
            $session = "generations-$runId-$backend-$mode"
            $atmosphere = $mode -eq 'atmosphere'
            $lab = if ($atmosphere) { 'gglab.lab.atmosphere_range' } else { 'gglab.lab.temporal_aa' }
            $start = $null
            try {
                $start = Call start $session @{ Configuration = $Configuration; Rhi = $backend; Lab = $lab;
                    NoDevTools = $true; WindowSize = '320x180'; StateRoot = $state; WatchNonFinite = $true;
                    StartTimeoutSeconds = $TimeoutSeconds }
                Save "$directory/start.json" $start
                $null = Call capture $session @{ SettleFrames = 2; TimeoutSeconds = $TimeoutSeconds;
                    OutputDirectory = "$directory/Startup"; Label = 'startup' }
                $null = Call watch $session
                $first = Wait-Generations $session $atmosphere
                Save "$directory/first-watch.json" $first
                $null = Call graph $session @{ OutputDirectory = $directory; Label = 'active'; TimeoutSeconds = $TimeoutSeconds }
                $graphFile = @(Get-ChildItem -LiteralPath $directory -Filter '*.graph.json')
                if ($graphFile.Count -ne 1) { throw 'Missing active-generation graph' }
                $graph = Get-Content -Raw $graphFile[0].FullName | ConvertFrom-Json
                foreach ($group in @($first.watch.report.targets | Where-Object { $_.watchTier -eq 2 -and $null -ne $_.generation } | Group-Object semantic)) {
                    $targets = @($group.Group | Sort-Object { $_.address.mip })
                    $resource = @($graph.compile.resources | Where-Object { $_.name -eq $targets[0].address.resource })
                    if ($resource.Count -ne 1 -or $targets.Count -ne $resource[0].texture.mips) { throw "Incomplete mip coverage: $($group.Name)" }
                    for ($mip = 0; $mip -lt $targets.Count; ++$mip) {
                        if ($targets[$mip].address.mip -ne $mip -or $targets[$mip].address.slices[0] -ne 0 -or
                            $targets[$mip].address.slices[1] -ne $resource[0].texture.slices) { throw "Incomplete face/slice coverage: $($group.Name)" }
                        $width = [Math]::Max(1, [int]$resource[0].texture.extent[0] -shr $mip)
                        $height = [Math]::Max(1, [int]$resource[0].texture.extent[1] -shr $mip)
                        $expected = [long]$width * $height * $resource[0].texture.slices * 4
                        if ($targets[$mip].checked -ne $expected) { throw "Incomplete scalar coverage: $($group.Name), mip $mip" }
                    }
                }
                $null = Call capture $session @{ SettleFrames = 16; TimeoutSeconds = $TimeoutSeconds;
                    OutputDirectory = "$directory/Stable"; Label = 'stable' }
                $stable = Wait-Generations $session $atmosphere
                Save "$directory/stable-watch.json" $stable
                if ((Fingerprint $first) -cne (Fingerprint $stable)) { throw 'An unchanged active generation was rescanned' }
                $lastChecked = ($stable.watch.report.targets | Measure-Object lastCheckedFrameSerial -Maximum).Maximum
                $null = Call watch $session @{ WatchOff = $true }
                $null = Call watch $session
                $reconfigured = Wait-Generations $session $atmosphere $lastChecked
                Save "$directory/reconfigured-watch.json" $reconfigured
            } finally {
                if ($null -ne $start) {
                    $stop = Call stop $session
                    Save "$directory/stop.json" $stop
                    if ($stop.exitCode -ne 0) { throw "Session exited with $($stop.exitCode)" }
                }
            }
            $log = Get-Content -Raw $start.outputLog
            if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw 'Native validation errors in generation session' }
            $activeGeneration = @($first.watch.report.targets | Where-Object { $_.semantic -eq 'ibl.environment' })[0].generation
            $publications = @([regex]::Matches($log, 'IBL bake generation (\d+) published atomically \(cacheStages=(\d+), gpuStages=(\d+)') |
                Where-Object { [long]$_.Groups[1].Value -eq $activeGeneration })
            if ($publications.Count -ne 1) { throw 'The scanned IBL generation has no unique publication receipt' }
            $cacheStages = [int]$publications[0].Groups[2].Value
            $gpuStages = [int]$publications[0].Groups[3].Value
            # The source-independent BRDF LUT can already be in this process's CPU
            # cache after bootstrap, while the active lighting generation is baked.
            if ($mode -eq 'bake' -and ($gpuStages -lt 3 -or $gpuStages + $cacheStages -ne 4)) { throw 'Fresh-state session did not bake the active lighting generation' }
            if ($mode -eq 'cache' -and ($cacheStages -ne 4 -or $gpuStages -ne 0)) { throw 'Second session did not restore the active IBL generation from cache' }
            $cases += @{ configuration = $Configuration; backend = $backend; mode = $mode; passed = $true }
        }
    }
    $result = @{ ok = $true; cases = $cases; directory = $output }
    Save "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
} catch {
    $result = @{ ok = $false; cases = $cases; directory = $output; error = $_.Exception.Message }
    Save "$output/result.json" $result
    [Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
    exit 1
}
