<#
.SYNOPSIS
Audits every Lab and Demo with the non-finite watch and reports site and target totals.

.DESCRIPTION
Starts one hidden, counter-capable session per content and backend, configures watch in
report mode (boundary semantic defaults and production) before the first frame settles,
waits for readiness, then plays every registered camera path as a sequence, or observes
the running content when it has none. The final status report's since-configuration
totals list every site with replacements or clamps and every scanned target.

Report mode (the default) never stops a sequence, but per-frame boundary scans yield
when an observation group cannot be bound, so ranges can report coverage gaps. Fail mode
defers such frames, so sequence ranges carry complete per-frame evidence; a sequence then
stops at its first anomaly or policy failure, which the run records.

Writes result.json (all runs) and summary.md (per-site table with the reviewed policy)
under a new run directory. The audit does not judge clamp policies: it gathers the
evidence for reviewing them. Any replacement or boundary anomaly is listed first.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('report', 'fail')][string]$WatchMode = 'report',
    [ValidateSet('dx12', 'vulkan')][string[]]$Rhi = @('dx12', 'vulkan'),
    # Content names from the table below; empty audits everything.
    [string[]]$Content,
    [ValidatePattern('^\d+[xX]\d+$')][string]$WindowSize = '960x540',
    [int]$ReadyTimeoutSeconds = 120,
    [int]$ObserveSeconds = 20,
    [int]$TimeoutSeconds = 300,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sessionScript = Join-Path $PSScriptRoot 'GGLabSession.ps1'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/NonFiniteAudit' }
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) $runId
New-Item -ItemType Directory -Path $output | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)

$coastalPaths = @('SEQ_StaticRailings', 'SEQ_PanStopRailings', 'SEQ_LateralPanRailings', 'SEQ_StaticGlassTerrace',
    'SEQ_PanGlassTerrace', 'SEQ_DollyDoorway', 'SEQ_OrbitLounge', 'SEQ_HorizonPan', 'SEQ_CutCourtyardToStairs')
$catalog = @(
    @{ Name = 'alpha-test'; Lab = 'gglab.lab.alpha_test' },
    @{ Name = 'atmosphere-range'; Lab = 'gglab.lab.atmosphere_range' },
    @{ Name = 'coordinate-conformance'; Lab = 'gglab.lab.coordinate_conformance' },
    @{ Name = 'culling'; Lab = 'gglab.lab.culling' },
    @{ Name = 'forward-plus'; Lab = 'gglab.lab.forward_plus' },
    @{ Name = 'gtao'; Lab = 'gglab.lab.gtao'; Paths = @('SEQ_GTAOLab_Static', 'SEQ_GTAOLab_Pan', 'SEQ_GTAOLab_MovingOccluder',
        'SEQ_GTAOLab_OccluderStep', 'SEQ_GTAOLab_EmptyView') },
    @{ Name = 'lighting-contract'; Lab = 'gglab.lab.lighting_contract' },
    @{ Name = 'math-foundation'; Lab = 'gglab.lab.math_foundation' },
    @{ Name = 'mini-pbr-grid'; Lab = 'gglab.lab.mini_pbr_grid' },
    @{ Name = 'napa-voxel'; Lab = 'gglab.lab.napa_voxel' },
    @{ Name = 'post-process'; Lab = 'gglab.lab.post_process' },
    @{ Name = 'render-graph-compute'; Lab = 'gglab.lab.render_graph_compute' },
    @{ Name = 'sampleable-depth'; Lab = 'gglab.lab.sampleable_depth' },
    @{ Name = 'surface-probe'; Lab = 'gglab.lab.surface_probe' },
    @{ Name = 'temporal-aa'; Lab = 'gglab.lab.temporal_aa'; Paths = @('SEQ_TemporalAALab_MovingObject', 'SEQ_TemporalAALab_LightChange') },
    @{ Name = 'texture-contract'; Lab = 'gglab.lab.texture_contract' },
    @{ Name = 'island'; Demo = 'island' },
    @{ Name = 'coastal-atrium'; Demo = 'atrium'; Paths = $coastalPaths }
)
if ($Content) {
    # powershell -File passes a comma-separated list as one string.
    $Content = @($Content | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
    $unknown = @($Content | Where-Object { $_ -notin $catalog.Name })
    if ($unknown) { throw "Unknown content: $($unknown -join ', ')" }
    $catalog = @($catalog | Where-Object { $_.Name -in $Content })
}

function Save([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 32), $utf8)
}
function Call([string]$Verb, [string]$Session, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $json = ($Options | ConvertTo-Json -Compress -Depth 12).Replace("'", "''")
    $script = $sessionScript.Replace("'", "''")
    $command = "`$ProgressPreference='SilentlyContinue'; `$p=@{}; `$data='$json'|ConvertFrom-Json; " +
        "foreach (`$v in `$data.PSObject.Properties) {`$p[`$v.Name]=`$v.Value}; & '$script' '$Verb' -Session '$Session' @p"
    $text = & powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command)))
    if ($LASTEXITCODE -and -not $AllowFailure) { throw "Session $Session $Verb failed: $text" }
    return ($text -join "`n") | ConvertFrom-Json
}
# Debug DX12 sessions can stop answering while GPU-based validation patches pipelines.
function Get-Status([string]$Session) {
    $status = Call status $Session -AllowFailure
    if ($status.PSObject.Properties['ok'] -and -not $status.ok) { return $null }
    return $status
}

$runs = @()
foreach ($backend in $Rhi) {
    foreach ($item in $catalog) {
        $name = $item.Name
        $directory = Join-Path $output "$backend-$name"
        New-Item -ItemType Directory -Path $directory | Out-Null
        $session = "audit-$runId-$backend-$name"
        if ($session.Length -gt 64) { $session = "audit-$($runId.Substring(16))-$backend-$name" }
        $run = [ordered]@{ content = $name; backend = $backend; configuration = $Configuration; watchMode = $WatchMode; ready = $false;
            pendingGates = @(); sequences = @(); report = $null; logErrors = @(); error = $null }
        $start = $null
        try {
            $options = @{ Configuration = $Configuration; Rhi = $backend; NoDevTools = $true; WindowSize = $WindowSize;
                StateRoot = "$output/$backend-state"; WatchNonFinite = $true; StartTimeoutSeconds = $TimeoutSeconds }
            if ($item.ContainsKey('Lab')) { $options['Lab'] = $item.Lab } else { $options['Demo'] = $item.Demo }
            $start = Call start $session $options
            # Watch from the first frames, so loading and bakes are audited too.
            $null = Call watch $session @{ WatchMode = $WatchMode }
            $deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
            $status = $null
            do {
                Start-Sleep -Milliseconds 500
                $status = Get-Status $session
            } while ((Get-Date) -lt $deadline -and -not ($status -and $status.frame -and $status.frame.ready))
            $run.ready = [bool]($status -and $status.frame -and $status.frame.ready)
            if ($status -and $status.frame) {
                $run.pendingGates = @($status.frame.gates | Where-Object { $_.state -ne 'ready' } | ForEach-Object { "$($_.name): $($_.detail)" })
            }
            if ($run.ready -and $item.ContainsKey('Paths')) {
                foreach ($path in $item.Paths) {
                    $sequence = Call sequence $session @{ CameraPath = $path; TimeoutSeconds = $TimeoutSeconds } -AllowFailure
                    Save "$directory/sequence-$path.json" $sequence
                    $coverage = if ($sequence.PSObject.Properties['sequence'] -and $sequence.sequence -and $sequence.sequence.PSObject.Properties['watch']) { $sequence.sequence.watch } else { $null }
                    $run.sequences += [ordered]@{ path = $path;
                        state = if ($sequence.PSObject.Properties['sequence'] -and $sequence.sequence) { $sequence.sequence.state } else { $sequence.error }
                        boundary = if ($coverage) { $coverage.boundary } else { $null }
                        production = if ($coverage) { $coverage.production } else { $null }
                        frames = if ($coverage) { $coverage.frames } else { 0 }
                        failure = if ($coverage) { $coverage.failure } else { $null } }
                }
            } else {
                Start-Sleep -Seconds $ObserveSeconds
            }
            $final = Get-Status $session
            Save "$directory/status.json" $final
            if ($final) {
                $report = $final.watch.report
                $run.report = [ordered]@{ boundary = $report.boundary; production = $report.production; failure = $report.failure;
                    productionFailure = $report.productionFailure; productionFrames = $report.productionFrames;
                    firstAnomaly = $report.firstAnomaly; siteTotals = @($report.siteTotals);
                    targetTotals = @($report.targetTotals) }
            }
            $null = Call watch $session @{ WatchOff = $true } -AllowFailure
        } catch {
            $run.error = $_.Exception.Message
        } finally {
            if ($null -ne $start) {
                $stop = Call stop $session -AllowFailure
                Save "$directory/stop.json" $stop
                if ($start.PSObject.Properties['outputLog'] -and (Test-Path $start.outputLog)) {
                    $run.logErrors = @(Select-String -LiteralPath $start.outputLog -Pattern '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed' |
                        Select-Object -First 20 | ForEach-Object { $_.Line })
                }
            }
        }
        Save "$directory/run.json" $run
        $runs += [pscustomobject]$run
        [Console]::Error.WriteLine("audited $backend $name ready=$($run.ready) error=$($run.error)")
    }
}

# Per-site table across runs, with the reviewed inventory policy.
$inventory = (Get-Content -Raw -Encoding UTF8 (Join-Path $root 'Shaders/Metadata/NonFiniteSites.json') | ConvertFrom-Json).sites
$sites = @{}
foreach ($run in $runs) {
    if ($null -eq $run.report) { continue }
    foreach ($totals in $run.report.siteTotals) {
        $key = "$($totals.site)|$($totals.program)"
        if (-not $sites.ContainsKey($key)) {
            $policy = @($inventory | Where-Object { $_.id -eq $totals.site })[0].policy
            $sites[$key] = [ordered]@{ site = $totals.site; program = $totals.program; negativePolicy = $policy.negative;
                fp16Policy = $policy.fp16Maximum; frames = 0; failedFrames = 0; replacements = 0; negativeClamps = 0;
                fp16Clamps = 0; mostNegative = $null; maximum = $null; runs = @() }
        }
        $entry = $sites[$key]
        $entry.frames += $totals.frames
        $entry.failedFrames += $totals.failedFrames
        $entry.replacements += $totals.nonFiniteReplacements
        $entry.negativeClamps += $totals.negativeClamps
        $entry.fp16Clamps += $totals.fp16MaximumClamps
        if ($null -ne $totals.mostNegative -and ($null -eq $entry.mostNegative -or $totals.mostNegative -lt $entry.mostNegative)) { $entry.mostNegative = $totals.mostNegative }
        if ($null -ne $totals.maximum -and ($null -eq $entry.maximum -or $totals.maximum -gt $entry.maximum)) { $entry.maximum = $totals.maximum }
        $entry.runs += "$($run.backend)/$($run.content)"
    }
}
$siteRows = @($sites.Values | Sort-Object { -$_.replacements }, { -$_.failedFrames }, { -$_.frames })
$boundaryRows = @($runs | Where-Object { $null -ne $_.report } | ForEach-Object {
    $run = $_
    $run.report.targetTotals | Where-Object { $_.failedFrames -gt 0 } | ForEach-Object {
        [ordered]@{ run = "$($run.backend)/$($run.content)"; resource = $_.resource; mip = $_.mip; semantic = $_.semantic;
            failedFrames = $_.failedFrames; checkedFrames = $_.checkedFrames; nan = $_.nan; positiveInf = $_.positiveInf;
            negativeInf = $_.negativeInf; firstFailedFrameSerial = $_.firstFailedFrameSerial }
    }
})

$lines = @("# Non-finite audit $runId", '', "Configuration $Configuration, watch mode $WatchMode, window $WindowSize, backends $($Rhi -join ', ').", '')
$lines += '## Runs', '', '| Run | Ready | Production frames | Boundary | Production | Sequences | Notes |', '| --- | --- | --- | --- | --- | --- | --- |'
foreach ($run in $runs) {
    $report = $run.report
    $sequenceText = (@($run.sequences | ForEach-Object { "$($_.path):$($_.state)/$($_.production)" }) -join '<br>')
    $notes = @()
    if ($run.error) { $notes += "error: $($run.error)" }
    if (-not $run.ready) { $notes += "not ready: $($run.pendingGates -join '; ')" }
    if ($run.logErrors.Count) { $notes += "$($run.logErrors.Count) log errors" }
    $lines += "| $($run.backend)/$($run.content) | $($run.ready) | $(if ($report) { $report.productionFrames }) | $(if ($report) { $report.boundary }) | $(if ($report) { $report.production }) | $sequenceText | $(($notes -join '; ').Replace('|', '/')) |"
}
$lines += '', '## Sites with replacements or clamps', '', '| Site | Program | Policy (neg/fp16) | Frames | Failed frames | Replacements | Negative clamps | FP16 clamps | Most negative | Maximum | Runs |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |'
foreach ($row in $siteRows) {
    $lines += "| $($row.site) | $($row.program) | $($row.negativePolicy)/$($row.fp16Policy) | $($row.frames) | $($row.failedFrames) | $($row.replacements) | $($row.negativeClamps) | $($row.fp16Clamps) | $($row.mostNegative) | $($row.maximum) | $((@($row.runs | Sort-Object -Unique)) -join ', ') |"
}
$lines += '', '## Boundary targets with anomalies', ''
if ($boundaryRows.Count -eq 0) { $lines += 'None.' } else {
    $lines += '| Run | Resource | Mip | Semantic | Failed / checked frames | NaN | +Inf | -Inf | First failed frame |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- |'
    foreach ($row in $boundaryRows) {
        $lines += "| $($row.run) | $($row.resource) | $($row.mip) | $($row.semantic) | $($row.failedFrames) / $($row.checkedFrames) | $($row.nan) | $($row.positiveInf) | $($row.negativeInf) | $($row.firstFailedFrameSerial) |"
    }
}
[IO.File]::WriteAllText((Join-Path $output 'summary.md'), ($lines -join "`r`n") + "`r`n", $utf8)
$result = [ordered]@{ ok = -not ($runs | Where-Object { $_.error }); directory = $output; runs = $runs; sites = $siteRows; boundary = $boundaryRows }
Save "$output/result.json" $result
[Console]::Out.WriteLine((@{ ok = $result.ok; directory = $output; runs = $runs.Count; sites = $siteRows.Count; boundaryAnomalies = $boundaryRows.Count } | ConvertTo-Json -Compress))
if (-not $result.ok) { exit 1 }
