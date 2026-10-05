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
  result   Report a capture submitted with -NoWait.
  stop     Stop a session and wait for the process to exit.
  list     List running sessions.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 start -Session atrium -Rhi vulkan -Demo atrium
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 capture -Session atrium -SettleFrames 16 -Label overview
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/GGLabSession.ps1 stop -Session atrium
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('start', 'status', 'capture', 'result', 'stop', 'list')]
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
    [ValidateSet('scene', 'composited')]
    [string]$Source = 'scene',
    [ValidateSet('after-ready', 'next-frame')]
    [string]$Timing = 'after-ready',
    [ValidateRange(0, 10000)]
    [int]$SettleFrames = 8,
    [string]$RequiredContentId,
    [string]$OutputDirectory,
    [string]$Label,
    [string]$Note,
    [switch]$NoWait,

    # result
    [long]$RequestId,

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
        if (-not $OutputDirectory) {
            $OutputDirectory = Join-Path (Get-SessionDirectory $Session) 'Captures'
        }
        $OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
        $request = @{
            command = 'capture'; source = $Source; timing = $Timing; settleFrames = $SettleFrames
            outputDirectory = $OutputDirectory; wait = (-not $NoWait)
        }
        if ($RequiredContentId) { $request['requiredContentId'] = $RequiredContentId }
        if ($Label) { $request['label'] = $Label }
        if ($Note) { $request['note'] = $Note }
        $response = Invoke-SessionRequest $Session $request $TimeoutSeconds
        Complete-SessionResponse $response $Session
    }
    'result' {
        Require-Session
        if ($RequestId -le 0) {
            Fail "Command 'result' requires a positive -RequestId."
        }
        $response = Invoke-SessionRequest $Session @{ command = 'result'; requestId = $RequestId } 30
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
