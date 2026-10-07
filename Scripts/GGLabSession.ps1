<#
.SYNOPSIS
Starts, queries, captures from and stops GraphicsGadgetLab control sessions.

.DESCRIPTION
A session is a GraphicsGadgetLab process started with --session <id>. It serves
the session control protocol (version 1) on \\.\pipe\gglab-session-<id>: one
JSON request line per connection, answered by one JSON response line.

Every command prints one JSON object on stdout and exits 0 when the command
succeeded, or 1 with an "error" field otherwise. A capture that ran but failed
still exits 0; inspect its "status".

Commands:
  start    Launch a session (hidden by default) and wait until it accepts requests.
  status   Report readiness gates, settled frames, camera and pending captures.
  capture  Capture a frame; waits for the PNG and metadata unless -NoWait.
           -View restores a camera reference view of the active content first.
  batch    Capture every reference view of the active content (or -Views) in
           order and wait for all of them.
  result   Report a capture submitted with -NoWait.
  sequence Play a camera path registered by the active content (-CameraPath),
           capturing -CaptureFrames ("0,10,20-23"); waits until the sequence
           and its captures finish unless -NoWait. Poll with 'status'.
           -Source diagnostic -DiagnosticTap <tap> records one diagnostic tap,
           such as temporal-history-weight, instead of the scene.
           -ReferenceSamples <n> renders every frame as a supersampled reference
           of n jittered samples with Temporal AA inactive and time held.
           -TemporalAA "name=value,..." evaluates Temporal AA overrides, such as
           neighborhoodClampExpansion=1 or historyFilter=bilinear, on every frame. -GpuTiming records
           per-scope GPU times of the sequence frames.
  sequence-cancel
           Cancel the active sequence.
  stop     Stop a session and wait for the process to exit.
  list     List running sessions.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 start -Session atrium -Rhi vulkan -Demo atrium
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 capture -Session atrium -SettleFrames 16 -Label overview
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 batch -Session atrium -SettleFrames 16
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 stop -Session atrium
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('start', 'status', 'capture', 'batch', 'result', 'sequence', 'sequence-cancel', 'stop', 'list')]
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
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [double]$IdleTimeout = 900,
    [double]$FixedDeltaTime,
    [switch]$Visible,
    [switch]$NoDevTools,
    [string]$StateRoot,
    [string]$SessionRoot,
    [int]$StartTimeoutSeconds = 300,

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

    # result
    [long]$RequestId,

    # sequence: camera path id and frames to capture, as numbers and ranges.
    [string]$CameraPath,
    [string]$CaptureFrames,
    [ValidateRange(0, 4096)]
    [int]$ReferenceSamples = 0,
    [string]$TemporalAA,
    [switch]$GpuTiming,

    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$ProtocolVersion = 1
$RepositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $SessionRoot) {
    $SessionRoot = Join-Path $RepositoryRoot 'Build\Sessions'
}

function Write-Result([hashtable]$Result, [int]$ExitCode = 0) {
    [Console]::Out.WriteLine(($Result | ConvertTo-Json -Compress -Depth 10))
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

# Sends one request and returns the parsed response object.
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
            Fail "Session '$Id' is not running or does not accept requests." @{ session = $Id }
        }
        $encoding = New-Object System.Text.UTF8Encoding($false)
        $writer = New-Object System.IO.StreamWriter($client, $encoding, 4096, $true)
        $writer.NewLine = "`n"
        $writer.WriteLine(($Request | ConvertTo-Json -Compress -Depth 10))
        $writer.Flush()
        $reader = New-Object System.IO.StreamReader($client, $encoding, $false, 4096, $true)
        $readTask = $reader.ReadLineAsync()
        if (-not $readTask.Wait([TimeSpan]::FromSeconds($ResponseTimeoutSeconds))) {
            Fail "Session '$Id' did not answer within $ResponseTimeoutSeconds seconds." @{ session = $Id }
        }
        $line = $readTask.Result
        if (-not $line) {
            Fail "Session '$Id' closed the connection without a response." @{ session = $Id }
        }
        return ($line | ConvertFrom-Json)
    }
    finally {
        $client.Dispose()
    }
}

function ConvertTo-Hashtable($Object) {
    $table = @{}
    foreach ($property in $Object.PSObject.Properties) { $table[$property.Name] = $property.Value }
    return $table
}

function Complete-SessionResponse($Response, [string]$Id) {
    $result = ConvertTo-Hashtable $Response
    $result.Remove('protocol')
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

function New-CaptureRequest([string]$ViewId, [bool]$Wait) {
    $request = @{
        command = 'capture'; source = $Source; timing = $Timing; settleFrames = $SettleFrames
        outputDirectory = (Get-CaptureOutputDirectory); wait = $Wait
    }
    if ($RequiredContentId) { $request['requiredContentId'] = $RequiredContentId }
    if ($DiagnosticTap) { $request['diagnosticTap'] = $DiagnosticTap }
    if ($ViewId) { $request['view'] = $ViewId }
    if ($Label) { $request['label'] = $Label }
    if ($Note) { $request['note'] = $Note }
    return $request
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

function ConvertTo-TemporalAAOverrides([string]$Text) {
    $overrides = @{}
    foreach ($part in @($Text -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        if ($part -notmatch '^([A-Za-z]+)=([-+.0-9A-Za-z]+)$') {
            Fail "Temporal AA override '$part' is not name=value."
        }
        $number = 0.0
        if ([double]::TryParse($Matches[2], [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$number)) {
            $overrides[$Matches[1]] = $number
        }
        else {
            $overrides[$Matches[1]] = $Matches[2]
        }
    }
    return $overrides
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
        $response = Invoke-SessionRequest $Session @{ command = 'status' } 30
        Complete-SessionResponse $response $Session
    }
    'capture' {
        Require-Session
        $response = Invoke-SessionRequest $Session (New-CaptureRequest $View (-not $NoWait)) $TimeoutSeconds
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
                $status = Invoke-SessionRequest $Session @{ command = 'status' } 30
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

        # Every view is queued first; the session applies them in this order.
        $queued = @()
        foreach ($viewId in $Views) {
            $response = Invoke-SessionRequest $Session (New-CaptureRequest $viewId $false) 30
            if (-not $response.ok) { Complete-SessionResponse $response $Session }
            $queued += @{ view = $viewId; requestId = $response.requestId }
        }

        $captures = @()
        $completed = 0
        foreach ($entry in $queued) {
            $response = $null
            while ($true) {
                $response = Invoke-SessionRequest $Session @{ command = 'result'; requestId = $entry.requestId } 30
                if (-not $response.ok) { Complete-SessionResponse $response $Session }
                if ($response.status -ne 'queued' -or (Get-Date) -ge $deadline) { break }
                Start-Sleep -Milliseconds 250
            }
            $status = if ($response.status -eq 'queued') { 'timeout' } else { $response.status }
            if ($status -eq 'completed') { ++$completed }
            $captures += @{
                view = $entry.view; requestId = $entry.requestId; status = $status
                image = (Get-Field $response 'image'); metadata = (Get-Field $response 'metadata')
                failure = (Get-Field $response 'failure')
            }
        }
        Write-Result @{
            ok = $true; session = $Session; outputDirectory = (Get-CaptureOutputDirectory)
            completed = $completed; failed = ($captures.Count - $completed); captures = $captures
        } 0
    }
    'result' {
        Require-Session
        if ($RequestId -le 0) {
            Fail "Command 'result' requires a positive -RequestId."
        }
        $response = Invoke-SessionRequest $Session @{ command = 'result'; requestId = $RequestId } 30
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
        if ($TemporalAA) { $request['temporalAA'] = (ConvertTo-TemporalAAOverrides $TemporalAA) }
        if ($GpuTiming) { $request['gpuTiming'] = $true }
        if ($Label) { $request['label'] = $Label }
        if ($Note) { $request['note'] = $Note }
        $response = Invoke-SessionRequest $Session $request 30
        if ($NoWait -or -not $response.ok) { Complete-SessionResponse $response $Session }

        $sequenceId = $response.sequence.id
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        while ($true) {
            $status = Invoke-SessionRequest $Session @{ command = 'status' } 30
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
    'sequence-cancel' {
        Require-Session
        $response = Invoke-SessionRequest $Session @{ command = 'sequence-cancel' } 30
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
        $response = Invoke-SessionRequest $Session @{ command = 'stop' } 30
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
