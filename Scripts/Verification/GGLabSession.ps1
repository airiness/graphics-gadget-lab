<#
.SYNOPSIS
Starts, queries, captures from and stops GraphicsGadgetLab control sessions.

.DESCRIPTION
A session is a GraphicsGadgetLab process started with --session <id>. It serves
the session control protocol (version 4) on \\.\pipe\gglab-session-<id>: one
JSON request line per connection, answered by one JSON response line.

Every command prints one JSON object on stdout and exits 0 when the command
succeeded, or 1 with an "error" field otherwise. A capture that ran but failed
still exits 0; inspect its "status".

Commands:
  start    Launch a session (hidden by default) and wait until it accepts requests.
           -LabParameters "id=value",... sets startup Lab parameters (Debug and Develop builds).
           -Validation off|standard|gpu overrides validation (gpu: DX12 only).
  status   Report readiness gates, settled frames, camera and pending captures.
  parameters
           List Host and active Lab descriptors, current/default values and revision.
  set      Submit -Changes/-ChangesFile as [{"id":"gtao.radius","type":"float","value":2.0}].
           Waits for application; -WaitConsumed waits for all terminal effects.
           -NoWait returns admission. Terminal results are consumed unless -KeepResult.
  parameter-result
           Query -BatchId; -Consume releases its terminal receipt. Status also reports it.
  capture  Submit required SameFrame products; waits and explicitly consumes the
           terminal receipt unless -NoWait or -KeepResult. -Products/-ProductsFile
           accepts typed products instead of the default single image.
           -View restores a camera reference view of the active content first.
  graph    Export the full frame graph, or discovery addresses with -Summary.
  export   Export one frame's resource group as NPY arrays and JSON metadata.
           -Targets/-TargetsFile accepts resource addresses; -IncludeGraph pairs
           the exact frame graph. Address format:
           .agents/skills/gglab-frame-capture/references/graph-and-resources.md
  watch    Configure semantic defaults or explicit non-finite targets and mode.
           Omit -Targets for tiered defaults; -WatchOff disables reporting.
           -WatchClaims selects boundary, production or both. Debug and Develop builds accept
           -Inject '{"point":"<id>","value":"NaN","x":0,"y":0,"component":0}'
           to inject at a registered point; it needs -WatchNonFinite at start.
  batch    Capture every reference view of the active content (or -Views) in
           order and wait for all of them.
  result   Query -JobId without consuming; -Consume releases a terminal receipt.
  capture-cancel
           Cancel delivery for -JobId; query its actual terminal result separately.
  sequence Play a camera path registered by the active content (-CameraPath),
           capturing -CaptureFrames ("0,10,20-23"); waits until the sequence
           and its captures finish unless -NoWait. Poll with 'status'.
           -Exports/-ExportsFile adds one target group at each selected frame.
           -Source diagnostic -DiagnosticTap <tap> records one diagnostic tap,
           such as temporal-history-weight, instead of the scene.
           -ReferenceSamples <n> renders every frame as a supersampled reference
           of n jittered samples with Temporal AA inactive and time held;
           -ReferenceTextureLodBias <b> adds b to its material texture LOD.
           -TemporalAA "name=value,..." evaluates Temporal AA overrides, such as
           neighborhoodClampExpansion=1 or historyFilter=bilinear, on every frame.
           -GTAO "name=value,..." evaluates GTAO overrides, such as temporal=true or
           temporalMaxSamples=16, on every frame. -GpuTiming records
           per-scope GPU times of the sequence frames.
  sequence-cancel
           Cancel the active sequence.
  stop     Stop a session and wait for the process to exit.
  list     List running sessions.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/GGLabSession.ps1 start -Session atrium -Rhi vulkan -Demo atrium
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/GGLabSession.ps1 capture -Session atrium -SettleFrames 16 -Label overview
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/GGLabSession.ps1 batch -Session atrium -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/GGLabSession.ps1 stop -Session atrium
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('start', 'status', 'parameters', 'set', 'parameter-result', 'capture', 'graph', 'export', 'watch', 'batch', 'result', 'capture-cancel', 'sequence', 'sequence-cancel', 'stop', 'list')]
    [string]$Command,

    [ValidatePattern('^[A-Za-z0-9_-]{1,64}$')]
    [string]$Session,

    # start
    [ValidateSet('dx12', 'vulkan')]
    [string]$Rhi = 'dx12',
    [string]$Lab,
    [string]$Demo,
    [ValidatePattern('^\d+[xX]\d+$')]
    [string]$WindowSize,
    [ValidateSet('Debug', 'Develop', 'Release')]
    [string]$Configuration = 'Debug',
    [ValidateSet('off', 'standard', 'gpu')]
    [string]$Validation,
    [double]$IdleTimeout = 900,
    [double]$FixedDeltaTime,
    [switch]$Visible,
    [switch]$NoDevTools,
    [string]$StateRoot,
    [string]$SessionRoot,
    [int]$StartTimeoutSeconds = 300,
    # Debug and Develop builds only: startup Lab parameter assignments as "id=value".
    [string[]]$LabParameters,

    # capture
    [ValidateSet('scene', 'composited', 'diagnostic')]
    [string]$Source = 'scene',
    # Required with -Source diagnostic, for example temporal-history-weight.
    [string]$DiagnosticTap,
    [ValidateSet('after-ready', 'next-frame')]
    [string]$Timing = 'after-ready',
    [ValidateRange(0, 10000)]
    [int]$SettleFrames = 8,
    [string]$RequiredContentId,
    [string]$View,
    # batch: reference view ids; empty captures every view the session reports.
    [string[]]$Views,
    [string]$OutputDirectory,
    [string]$Label,
    [string]$Note,
    [switch]$NoWait,
    [switch]$KeepResult,
    [string]$Products,
    [string]$ProductsFile,
    # graph: only executed passes with resource-version addresses.
    [switch]$Summary,
    # export: a JSON array of resource addresses, directly or from a UTF-8 file.
    [string]$Targets,
    [string]$TargetsFile,
    [switch]$IncludeGraph,
    [switch]$WatchNonFinite,
    [switch]$WatchOff,
    [ValidateSet('report', 'fail')]
    [string]$WatchMode = 'report',
    [ValidateSet('boundary', 'production')]
    [string[]]$WatchClaims = @('boundary', 'production'),
    # watch (Debug and Develop builds): one non-finite injection request as a JSON object.
    [string]$Inject,

    # set: typed changes [{id, type, value}], directly or from a UTF-8 file.
    [string]$Changes,
    [string]$ChangesFile,
    [switch]$WaitConsumed,
    # parameter-result: separate from capture JobId.
    [uint64]$BatchId,

    # result / capture-cancel
    [uint64]$JobId,
    [switch]$Consume,

    # sequence: camera path id and frames to capture, as numbers and ranges.
    [string]$CameraPath,
    [string]$CaptureFrames,
    # sequence: [{frame: n, targets: [...], graph: true}], directly or from a file.
    [string]$Exports,
    [string]$ExportsFile,
    # sequence: [{frame: n, set: {parameterId: value}, expectEffects: []}].
    [string]$Events,
    [string]$EventsFile,
    [ValidateRange(0, 4096)]
    [int]$ReferenceSamples = 0,
    [double]$ReferenceTextureLodBias = 0,
    [string]$TemporalAA,
    [string]$GTAO,
    [switch]$GpuTiming,

    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$ProtocolVersion = 4
$RepositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $SessionRoot) {
    $SessionRoot = Join-Path $RepositoryRoot 'Build\Sessions'
}

function Write-Result([hashtable]$Result, [int]$ExitCode = 0) {
    [Console]::Out.WriteLine(($Result | ConvertTo-Json -Compress -Depth 32))
    exit $ExitCode
}

function Fail([string]$Message, [hashtable]$Extra = @{}) {
    $result = @{ ok = $false; error = $Message }
    foreach ($key in $Extra.Keys) { $result[$key] = $Extra[$key] }
    Write-Result $result 1
}

# Reads a file that another process is still writing.
function Read-SharedText([string]$Path) {
    if (-not (Test-Path $Path)) { return '' }
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    }
    finally {
        $stream.Dispose()
    }
}

function Get-SessionDirectory([string]$Id) {
    return Join-Path $SessionRoot $Id
}

function Get-PipeName([string]$Id) {
    return "gglab-session-$Id"
}

function Test-SessionPipe([string]$Id) {
    $pipes = [System.IO.Directory]::GetFiles('\\.\pipe\')
    return [bool]($pipes | Where-Object { $_ -ieq "\\.\pipe\$(Get-PipeName $Id)" })
}

# Sends one request and returns the parsed response object. Transport failures
# throw so a waiting client can retain an accepted jobId; commands report them
# through Invoke-SessionCommand.
function Invoke-SessionRequest([string]$Id, [hashtable]$Request, [int]$ResponseTimeoutSeconds) {
    $Request['protocol'] = $ProtocolVersion
    $Request['id'] = Get-Random -Minimum 1 -Maximum 2147483647
    $client = New-Object System.IO.Pipes.NamedPipeClientStream('.', (Get-PipeName $Id),
        [System.IO.Pipes.PipeDirection]::InOut)
    try {
        try {
            $client.Connect(5000)
        }
        catch {
            throw "Session '$Id' is not running or does not accept requests."
        }
        $encoding = New-Object System.Text.UTF8Encoding($false)
        $writer = New-Object System.IO.StreamWriter($client, $encoding, 4096, $true)
        $writer.NewLine = "`n"
        $writer.WriteLine(($Request | ConvertTo-Json -Compress -Depth 32))
        $writer.Flush()
        $reader = New-Object System.IO.StreamReader($client, $encoding, $false, 4096, $true)
        $readTask = $reader.ReadLineAsync()
        if (-not $readTask.Wait([TimeSpan]::FromSeconds($ResponseTimeoutSeconds))) {
            throw "Session '$Id' did not answer within $ResponseTimeoutSeconds seconds."
        }
        $line = $readTask.Result
        if (-not $line) {
            throw "Session '$Id' closed the connection without a response."
        }
        $response = $line | ConvertFrom-Json
        if ((Get-Field $response 'protocol') -ne $ProtocolVersion -or
            (Get-Field $response 'id') -ne $Request.id) {
            throw 'The session response has an incompatible protocol or correlation id.'
        }
        return $response
    }
    finally {
        $client.Dispose()
    }
}

# Every command prints one JSON result, including when the session is missing,
# does not answer or returns an incompatible response.
function Invoke-SessionCommand([string]$Id, [hashtable]$Request, [int]$ResponseTimeoutSeconds) {
    try {
        return Invoke-SessionRequest $Id $Request $ResponseTimeoutSeconds
    }
    catch {
        Fail $_.Exception.Message @{ session = $Id }
    }
}

function ConvertTo-Hashtable($Object) {
    $table = @{}
    foreach ($property in $Object.PSObject.Properties) { $table[$property.Name] = $property.Value }
    return $table
}

function Complete-SessionResponse($Response, [string]$Id) {
    $result = ConvertTo-Hashtable $Response
    $result.Remove('id')
    $result['session'] = if ($result.ContainsKey('session')) { $result['session'] } else { $Id }
    if ($Response.ok) { Write-Result $result 0 } else { Write-Result $result 1 }
}

# Property value of a parsed JSON object, or $null when the property is absent.
function Get-Field($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

function Get-CaptureOutputDirectory {
    $directory = $OutputDirectory
    if (-not $directory) {
        $directory = Join-Path (Get-SessionDirectory $Session) 'Captures'
    }
    return [System.IO.Path]::GetFullPath($directory)
}

function New-CaptureRequest([string]$ViewId) {
    $request = @{
        command = 'capture'; source = $Source; timing = $Timing; settleFrames = $SettleFrames
        outputDirectory = (Get-CaptureOutputDirectory)
    }
    if ($RequiredContentId) { $request['requiredContentId'] = $RequiredContentId }
    if ($DiagnosticTap) { $request['diagnosticTap'] = $DiagnosticTap }
    if ($ViewId) { $request['view'] = $ViewId }
    if ($Label) { $request['label'] = $Label }
    if ($Note) { $request['note'] = $Note }
    return $request
}

# The server never waits or moves a receipt into a transport-owned cache.
# Waiting clients explicitly consume the terminal result. A timeout does not
# cancel or consume pending work; the returned jobId remains recoverable.
function Wait-CaptureJob($Admission, [DateTime]$Deadline) {
    if (-not $Admission.ok) { return $Admission }
    do {
        $remaining = [Math]::Max(1, [int][Math]::Ceiling(($Deadline - [DateTime]::UtcNow).TotalSeconds))
        try {
            $response = Invoke-SessionRequest $Session @{
                command = 'result'; jobId = $Admission.jobId; consume = (-not $KeepResult)
            } $remaining
        }
        catch {
            # Do not invent a terminal outcome or lose the accepted identity.
            # A consuming query's response can be lost after consumption.
            return [pscustomobject]@{
                protocol = $ProtocolVersion; ok = $false; jobId = $Admission.jobId
                error = "$($_.Exception.Message) Query jobId explicitly; a consuming response may have been lost."
            }
        }
        if (-not $response.ok -or $response.status -ne 'pending') { return $response }
        if ([DateTime]::UtcNow -ge $Deadline) {
            $result = ConvertTo-Hashtable $response
            $result.ok = $false
            $result.error = 'Capture job timed out; query or cancel jobId explicitly.'
            return [pscustomobject]$result
        }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

# Polls the Lab owner; a timeout/transport failure never submits a second batch.
function Wait-ParameterBatch($Admission, [DateTime]$Deadline) {
    do {
        $remaining = [Math]::Max(1, [int][Math]::Ceiling(($Deadline - [DateTime]::UtcNow).TotalSeconds))
        try {
            $response = Invoke-SessionRequest $Session @{
                command = 'parameter-result'; batchId = $Admission.batchId; consume = (-not $KeepResult)
            } $remaining
        }
        catch {
            return [pscustomobject]@{
                protocol = $ProtocolVersion; ok = $false; batchId = $Admission.batchId
                error = "$($_.Exception.Message) Query batchId explicitly; a consuming response may have been lost."
            }
        }
        if (-not $response.ok -or $response.batch.terminal) { return $response }
        # Queued has no receipt items/revision. Applied/published stamps come from the built frame.
        $applied = $null -ne $response.batch.revision -and $response.batch.items.Count -gt 0
        foreach ($item in $response.batch.items) {
            if ($null -eq $item.applied -and $item.state -ne 'NoChange') { $applied = $false }
        }
        if (-not $WaitConsumed -and $applied) { return $response }
        if ([DateTime]::UtcNow -ge $Deadline) {
            $result = ConvertTo-Hashtable $response
            $result.ok = $false
            $result.error = 'Parameter batch timed out; query batchId explicitly. The batch remains owned.'
            return [pscustomobject]$result
        }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

# Expands "0,10,20-23" into frame numbers.
function ConvertTo-FrameList([string]$Text) {
    $frames = @()
    foreach ($part in @($Text -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        if ($part -match '^(\d+)-(\d+)$') {
            $first = [long]$Matches[1]; $last = [long]$Matches[2]
            if ($last -lt $first) { Fail "Capture frame range '$part' is descending." }
            for ($frame = $first; $frame -le $last; ++$frame) { $frames += $frame }
        }
        elseif ($part -match '^\d+$') {
            $frames += [long]$part
        }
        else {
            Fail "Capture frame '$part' is not a frame number or range."
        }
    }
    return ,$frames
}

function ConvertTo-SettingOverrides([string]$Text, [string]$Owner) {
    $overrides = @{}
    foreach ($part in @($Text -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        if ($part -notmatch '^([A-Za-z]+)=([-+.0-9A-Za-z]+)$') {
            Fail "$Owner override '$part' is not name=value."
        }
        $number = 0.0
        if ($Matches[2] -eq 'true' -or $Matches[2] -eq 'false') {
            $overrides[$Matches[1]] = $Matches[2] -eq 'true'
        }
        elseif ([double]::TryParse($Matches[2], [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
            $overrides[$Matches[1]] = $number
        }
        else {
            $overrides[$Matches[1]] = $Matches[2]
        }
    }
    return $overrides
}

function Read-JsonArray([string]$Text, [string]$File, [string]$Name) {
    if ($Text -and $File) { Fail "Use either -$Name or -${Name}File, not both." }
    if ($File) { $Text = Get-Content -LiteralPath $File -Encoding UTF8 -Raw }
    if (-not $Text -or -not $Text.TrimStart().StartsWith('[')) { Fail "-$Name requires a JSON array." }
    try { $array = ConvertFrom-Json -InputObject $Text }
    catch { Fail "-$Name contains invalid JSON: $($_.Exception.Message)" }
    return ,([object[]]$array)
}

function Require-Session {
    if (-not $Session) {
        Fail "Command '$Command' requires -Session."
    }
}

switch ($Command) {
    'start' {
        if (-not $Session) {
            $Session = 's' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + (Get-Random -Maximum 10000)
        }
        if (Test-SessionPipe $Session) {
            Fail "Session '$Session' is already running." @{ session = $Session }
        }
        $executable = Join-Path $RepositoryRoot "Build\Output\x64\$Configuration\GraphicsGadgetLab.exe"
        if (-not (Test-Path $executable)) {
            Fail "GraphicsGadgetLab.exe was not found at '$executable'. Build the $Configuration configuration first."
        }
        $sessionDirectory = Get-SessionDirectory $Session
        New-Item -ItemType Directory -Force -Path $sessionDirectory | Out-Null
        $outputLog = Join-Path $sessionDirectory 'output.log'
        if (Test-Path $outputLog) { Remove-Item $outputLog -Force }

        $arguments = @('--rhi', $Rhi, '--session', $Session, '--idle-timeout', "$IdleTimeout",
            '--output-log', "`"$outputLog`"")
        if ($Lab) { $arguments += @('--lab', $Lab) }
        if ($Demo) { $arguments += @('--demo', $Demo) }
        if ($WindowSize) { $arguments += @('--window-size', $WindowSize) }
        if (-not $Visible) { $arguments += '--hidden' }
        if ($WatchNonFinite) { $arguments += '--watch-nonfinite' }
        if ($Validation) { $arguments += @('--validation', $Validation) }
        foreach ($assignment in $LabParameters) { $arguments += @('--lab-parameter', $assignment) }
        if ($PSBoundParameters.ContainsKey('FixedDeltaTime')) {
            $arguments += @('--fixed-delta-time', "$FixedDeltaTime")
        }
        if ($NoDevTools) { $arguments += '--no-devtools' }
        if ($StateRoot) { $arguments += @('--state-root', "`"$StateRoot`"") }

        # The session outlives this script, so it must not inherit any handle of
        # the caller: a caller reading this script's output until end of stream
        # would otherwise wait for the session to exit. Starting through the shell
        # inherits nothing; the session writes its own output log.
        $process = Start-Process -FilePath $executable -ArgumentList $arguments `
            -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Hidden -PassThru
        $deadline = (Get-Date).AddSeconds($StartTimeoutSeconds)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            if ($process.HasExited) { break }
            if ((Read-SharedText $outputLog) -match '(?m)^session-ready: ') {
                $ready = $true
                break
            }
            Start-Sleep -Milliseconds 200
        }
        if (-not $ready) {
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
            $tail = @()
            $tail = @((Read-SharedText $outputLog) -split "`r?`n" | Select-Object -Last 20)
            Fail "Session '$Session' did not become ready." @{
                session = $Session; outputLog = $outputLog; log = $tail
                exitCode = $(if ($process.HasExited) { $process.ExitCode } else { $null })
            }
        }
        $record = @{
            session = $Session; pid = $process.Id; rhi = $Rhi; arguments = $arguments
            startedAt = (Get-Date).ToString('o'); outputLog = $outputLog
            directory = $sessionDirectory
        }
        ($record | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 -Path (Join-Path $sessionDirectory 'session.json')
        $record['ok'] = $true
        Write-Result $record 0
    }
    'status' {
        Require-Session
        $response = Invoke-SessionCommand $Session @{ command = 'status' } 30
        Complete-SessionResponse $response $Session
    }
    'parameters' {
        Require-Session
        $response = Invoke-SessionCommand $Session @{ command = 'parameters' } 30
        Complete-SessionResponse $response $Session
    }
    'set' {
        Require-Session
        if ($NoWait -and $WaitConsumed) { Fail '-NoWait and -WaitConsumed cannot be combined.' }
        try { $items = Read-JsonArray $Changes $ChangesFile 'Changes' }
        catch { Fail $_.Exception.Message @{ session = $Session } }
        try {
            $response = Invoke-SessionRequest $Session @{ command = 'set'; changes = $items } ([Math]::Max(30, $TimeoutSeconds))
        }
        catch {
            Fail "$($_.Exception.Message) Admission is unknown; inspect status before submitting again." @{
                session = $Session; admissionUnknown = $true
            }
        }
        if ($response.ok -and -not $NoWait) {
            $response = Wait-ParameterBatch $response ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds))
        }
        Complete-SessionResponse $response $Session
    }
    'parameter-result' {
        Require-Session
        if ($BatchId -eq 0) { Fail "Command 'parameter-result' requires a positive -BatchId." }
        try {
            $response = Invoke-SessionRequest $Session @{
                command = 'parameter-result'; batchId = $BatchId; consume = [bool]$Consume
            } 30
        }
        catch {
            Fail $_.Exception.Message @{ session = $Session; batchId = $BatchId; consumptionUnknown = [bool]$Consume }
        }
        Complete-SessionResponse $response $Session
    }
    'capture' {
        Require-Session
        $request = New-CaptureRequest $View
        if ($Products -or $ProductsFile) {
            if ($PSBoundParameters.ContainsKey('Source') -or $DiagnosticTap) {
                Fail '-Products cannot be combined with -Source or -DiagnosticTap.'
            }
            $request.Remove('source')
            $request['products'] = (Read-JsonArray $Products $ProductsFile 'Products')
        }
        $response = Invoke-SessionCommand $Session $request ([Math]::Max(30, $TimeoutSeconds))
        if (-not $NoWait) { $response = Wait-CaptureJob $response ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds)) }
        Complete-SessionResponse $response $Session
    }
    'graph' {
        Require-Session
        $request = New-CaptureRequest $View
        $request['command'] = 'graph'
        $request.Remove('source')
        $request['summary'] = [bool]$Summary
        $response = Invoke-SessionCommand $Session $request ([Math]::Max(30, $TimeoutSeconds))
        if (-not $NoWait) { $response = Wait-CaptureJob $response ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds)) }
        Complete-SessionResponse $response $Session
    }
    'export' {
        Require-Session
        $request = New-CaptureRequest $View
        $request['command'] = 'export'
        $request.Remove('source')
        $request.Remove('diagnosticTap')
        $request['targets'] = (Read-JsonArray $Targets $TargetsFile 'Targets')
        $request['graph'] = [bool]$IncludeGraph
        $response = Invoke-SessionCommand $Session $request ([Math]::Max(30, $TimeoutSeconds))
        if (-not $NoWait) { $response = Wait-CaptureJob $response ([DateTime]::UtcNow.AddSeconds($TimeoutSeconds)) }
        Complete-SessionResponse $response $Session
    }
    'batch' {
        Require-Session
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        # 'powershell -File' passes '-Views a,b' as one string.
        $Views = @($Views | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
        if ($Views.Count -eq 0) {
            # Content registers its reference views while it loads, so the view
            # list is taken once the session reports ready content.
            while ($true) {
                $status = Invoke-SessionCommand $Session @{ command = 'status' } 30
                $frame = Get-Field $status 'frame'
                $reported = @(Get-Field $frame 'referenceViews' | Where-Object { $_ })
                if ((Get-Field $frame 'ready') -and $reported.Count -gt 0) {
                    $Views = $reported
                    break
                }
                if ((Get-Date) -ge $deadline) {
                    Fail "Session '$Session' reported no reference views within $TimeoutSeconds seconds." @{
                        session = $Session; demoId = (Get-Field $frame 'demoId'); labId = (Get-Field $frame 'labId')
                    }
                }
                Start-Sleep -Milliseconds 500
            }
        }

        # Admit one view at a time. Do not hide Busy or create an unbounded set of
        # retained receipts just because the authored view list is large.
        $captures = @()
        $completed = 0
        foreach ($viewId in $Views) {
            $admission = Invoke-SessionCommand $Session (New-CaptureRequest $viewId) ([Math]::Max(30, $TimeoutSeconds))
            $response = Wait-CaptureJob $admission $deadline.ToUniversalTime()
            if (-not $response.ok) { Complete-SessionResponse $response $Session }
            if ($response.status -eq 'completed') { ++$completed }
            $captures += @{ view = $viewId; result = $response }
        }
        Write-Result @{
            ok = $true; session = $Session; outputDirectory = (Get-CaptureOutputDirectory)
            completed = $completed; failed = ($captures.Count - $completed); captures = $captures
        } 0
    }
    'result' {
        Require-Session
        if ($JobId -eq 0) { Fail "Command 'result' requires a positive -JobId." }
        $response = Invoke-SessionCommand $Session @{ command = 'result'; jobId = $JobId; consume = [bool]$Consume } 30
        Complete-SessionResponse $response $Session
    }
    'capture-cancel' {
        Require-Session
        if ($JobId -eq 0) { Fail "Command 'capture-cancel' requires a positive -JobId." }
        $response = Invoke-SessionCommand $Session @{ command = 'capture-cancel'; jobId = $JobId } 30
        Complete-SessionResponse $response $Session
    }
    'sequence' {
        Require-Session
        if (-not $CameraPath) {
            Fail "Command 'sequence' requires -CameraPath."
        }
        $request = @{
            command = 'sequence'; path = $CameraPath; source = $Source
            captureFrames = (ConvertTo-FrameList $CaptureFrames)
            outputDirectory = (Get-CaptureOutputDirectory)
        }
        if ($RequiredContentId) { $request['requiredContentId'] = $RequiredContentId }
        if ($DiagnosticTap) { $request['diagnosticTap'] = $DiagnosticTap }
        if ($ReferenceSamples -gt 0) { $request['referenceSamples'] = $ReferenceSamples }
        if ($Events -or $EventsFile) { $request['events'] = (Read-JsonArray $Events $EventsFile 'Events') }
        if ($Exports -or $ExportsFile) { $request['exports'] = (Read-JsonArray $Exports $ExportsFile 'Exports') }
        if ($ReferenceTextureLodBias -ne 0) { $request['referenceTextureLodBias'] = $ReferenceTextureLodBias }
        if ($TemporalAA) { $request['temporalAA'] = (ConvertTo-SettingOverrides $TemporalAA 'Temporal AA') }
        if ($GTAO) { $request['gtao'] = (ConvertTo-SettingOverrides $GTAO 'GTAO') }
        if ($GpuTiming) { $request['gpuTiming'] = $true }
        if ($Label) { $request['label'] = $Label }
        if ($Note) { $request['note'] = $Note }
        $response = Invoke-SessionCommand $Session $request 30
        if ($NoWait -or -not $response.ok) { Complete-SessionResponse $response $Session }

        $sequenceId = $response.sequence.id
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ($true) {
            $status = Invoke-SessionCommand $Session @{ command = 'status' } 30
            $sequence = Get-Field $status 'sequence'
            if ($null -eq $sequence -or $sequence.id -ne $sequenceId) {
                Fail "Session '$Session' no longer reports sequence $sequenceId." @{ session = $Session }
            }
            if (@('completed', 'failed', 'cancelled') -contains $sequence.state) {
                Write-Result @{
                    ok = $true; session = $Session; outputDirectory = (Get-CaptureOutputDirectory)
                    sequence = $sequence
                } 0
            }
            if ((Get-Date) -ge $deadline) {
                Fail "Sequence $sequenceId did not finish within $TimeoutSeconds seconds." @{
                    session = $Session; sequence = $sequence
                }
            }
            Start-Sleep -Milliseconds 250
        }
    }
    'watch' {
        Require-Session
        $request = @{ command = 'watch'; enabled = (-not $WatchOff) }
        if (-not $WatchOff) {
            $request['mode'] = $WatchMode
            $request['claims'] = @($WatchClaims)
            if ($Targets -or $TargetsFile) { $request['targets'] = (Read-JsonArray $Targets $TargetsFile 'Targets') }
            if ($Inject) {
                try { $request['inject'] = ($Inject | ConvertFrom-Json) } catch { Fail "-Inject is not a JSON object: $($_.Exception.Message)" }
            }
        }
        $response = Invoke-SessionCommand $Session $request 30
        Complete-SessionResponse $response $Session
    }
    'sequence-cancel' {
        Require-Session
        $response = Invoke-SessionCommand $Session @{ command = 'sequence-cancel' } 30
        Complete-SessionResponse $response $Session
    }
    'stop' {
        Require-Session
        $recordPath = Join-Path (Get-SessionDirectory $Session) 'session.json'
        $process = $null
        if (Test-Path $recordPath) {
            $sessionProcessId = (Get-Content -Raw $recordPath | ConvertFrom-Json).pid
            $process = Get-Process -Id $sessionProcessId -ErrorAction SilentlyContinue
            if ($process) {
                # Opening the handle before exit keeps the exit code readable.
                $null = $process.Handle
            }
        }
        $response = Invoke-SessionCommand $Session @{ command = 'stop' } 30
        $exitCode = $null
        if ($process) {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                Fail "Session '$Session' acknowledged stop but did not exit within $TimeoutSeconds seconds." @{ session = $Session; pid = $process.Id }
            }
            $exitCode = $process.ExitCode
        }
        $result = ConvertTo-Hashtable $response
        $result.Remove('protocol')
        $result.Remove('id')
        $result['session'] = $Session
        $result['exitCode'] = $exitCode
        if ($response.ok) { Write-Result $result 0 } else { Write-Result $result 1 }
    }
    'list' {
        $sessions = @()
        foreach ($pipe in [System.IO.Directory]::GetFiles('\\.\pipe\')) {
            $name = [System.IO.Path]::GetFileName($pipe)
            if ($name -like 'gglab-session-*') {
                $id = $name.Substring('gglab-session-'.Length)
                $entry = @{ session = $id; pid = $null; directory = $null }
                $recordPath = Join-Path (Get-SessionDirectory $id) 'session.json'
                if (Test-Path $recordPath) {
                    $record = Get-Content -Raw $recordPath | ConvertFrom-Json
                    $entry['pid'] = $record.pid
                    $entry['directory'] = $record.directory
                }
                $sessions += $entry
            }
        }
        Write-Result @{ ok = $true; sessions = $sessions } 0
    }
}
