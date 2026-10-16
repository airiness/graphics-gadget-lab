Set-StrictMode -Version 3.0
$script:SessionScript = Join-Path $PSScriptRoot 'GGLabSession.ps1'

function Write-VerificationJson {
    param([string]$Path, [AllowNull()]$Value, [int]$Depth = 32)
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth $Depth),
        (New-Object Text.UTF8Encoding($false)))
}

function Invoke-VerificationScript {
    param([string]$Path, [hashtable]$Parameters = @{})
    # Contain script exit calls and preserve typed arrays/switches across the child shell.
    $json = ($Parameters | ConvertTo-Json -Compress -Depth 32).Replace("'", "''")
    $quotedPath = ([IO.Path]::GetFullPath($Path)).Replace("'", "''")
    $command = "`$ProgressPreference = 'SilentlyContinue'; [Console]::OutputEncoding = New-Object Text.UTF8Encoding(`$false); " +
        "`$p = @{}; `$data = '$json' | ConvertFrom-Json; " +
        "foreach (`$v in `$data.PSObject.Properties) { `$p[`$v.Name] = `$v.Value }; & '$quotedPath' @p"
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = (Get-Command powershell -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $info.Arguments = '-NoProfile -ExecutionPolicy Bypass -EncodedCommand ' +
        [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    try {
        $null = $process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        return [pscustomobject]@{ exitCode = $process.ExitCode; output = $stdout.Result; error = $stderr.Result }
    } finally { $process.Dispose() }
}

function Invoke-VerificationSession {
    param([string]$Command, [string]$Session, [hashtable]$Options = @{}, [switch]$AllowFailure)
    $parameters = @{ Command = $Command; Session = $Session }
    foreach ($key in $Options.Keys) {
        if ($parameters.ContainsKey($key)) { throw "Reserved session parameter: $key" }
        $parameters[$key] = $Options[$key]
    }
    $result = Invoke-VerificationScript $script:SessionScript $parameters
    try { $receipt = $result.output | ConvertFrom-Json -ErrorAction Stop }
    catch { throw "Session $Session $Command returned invalid JSON: $($result.output) $($result.error)" }
    if ($null -eq $receipt -or -not $receipt.PSObject.Properties['ok'] -or $receipt.ok -isnot [bool]) {
        throw "Session $Session $Command returned no control result: $($result.output) $($result.error)"
    }
    if (($result.exitCode -ne 0 -or -not $receipt.ok) -and -not $AllowFailure) {
        throw "Session $Session $Command failed: $($result.output) $($result.error)"
    }
    # Capture/sequence/watch claims remain the caller's responsibility.
    return $receipt
}

function Wait-VerificationCondition {
    param([scriptblock]$Poll, [scriptblock]$Until, [double]$TimeoutSeconds,
        [int]$IntervalMilliseconds = 100, [switch]$DelayFirstPoll,
        [scriptblock]$OnTimeout = { param($last) throw 'Verification condition timed out.' })
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    if ($DelayFirstPoll) { Start-Sleep -Milliseconds $IntervalMilliseconds }
    do {
        $value = & $Poll
        if (& $Until $value) { return $value }
        if ([DateTime]::UtcNow -ge $deadline) { return (& $OnTimeout $value) }
        Start-Sleep -Milliseconds $IntervalMilliseconds
    } while ($true)
}

function Test-VerificationProcessOwner {
    param($Process, [string]$Session)
    return $null -ne $Process -and $Process.Name -eq 'GraphicsGadgetLab.exe' -and
        $Process.CommandLine -match ('(?:^|\s)--session\s+"?' + [regex]::Escape($Session) + '"?(?=\s|$)')
}

function Stop-VerificationSession {
    param([string]$Session, $StartedSession, [string]$ReceiptPath, [int]$Depth = 32,
        [switch]$RequireCleanExit, [switch]$AllowFailure, [switch]$AllowMissing,
        [switch]$ForceOwnedProcess)
    $stop = Invoke-VerificationSession stop $Session -AllowFailure
    if (-not $stop.ok -and $AllowMissing -and $null -eq $StartedSession) { return }
    if ($ReceiptPath) { Write-VerificationJson $ReceiptPath $stop -Depth $Depth }
    if (-not $stop.ok -and -not $AllowFailure) {
        if ($ForceOwnedProcess -and $null -ne $StartedSession) {
            $process = Get-CimInstance Win32_Process -Filter "ProcessId = $($StartedSession.pid)"
            if (Test-VerificationProcessOwner $process $Session) { Stop-Process -Id $StartedSession.pid -Force }
        }
        throw "Session $Session could not be stopped: $($stop | ConvertTo-Json -Compress -Depth 12)"
    }
    if ($RequireCleanExit -and (-not $stop.PSObject.Properties['exitCode'] -or $stop.exitCode -ne 0)) {
        throw "Session $Session exited without a clean exit code: $($stop | ConvertTo-Json -Compress -Depth 12)"
    }
    return $stop
}

Export-ModuleMember -Function Write-VerificationJson, Invoke-VerificationScript, Invoke-VerificationSession,
    Wait-VerificationCondition, Stop-VerificationSession
