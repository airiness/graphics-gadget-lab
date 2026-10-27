<#
.SYNOPSIS
Qualifies session lifetimes, suspended control and predicate selection deadlines.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory, [string]$RunDirectory,
    [string]$Python = 'python', [switch]$Visible,
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/SessionLifecycle' }
$runId = [Guid]::NewGuid().ToString('N').Substring(0, 8)
$output = New-VerificationRunDirectory $OutputDirectory $RunDirectory $runId
$checks = @(); $cases = @()
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GGLabSessionWindow {
    private delegate bool WindowCallback(IntPtr window, IntPtr context);
    [DllImport("user32.dll")] private static extern bool EnumWindows(WindowCallback callback, IntPtr context);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassName(IntPtr window, System.Text.StringBuilder name, int capacity);
    [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr Find(int process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, context) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            if (owner != process) return true;
            var name = new System.Text.StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            if (!name.ToString().EndsWith(".WindowClass", StringComparison.Ordinal)) return true;
            found = window; return false;
        }, IntPtr.Zero);
        return found;
    }
}
'@
function Check([bool]$Condition, [string]$Name) {
    $script:checks += @{ name=$Name; passed=$Condition }
    if (-not $Condition) { throw "Failed: $Name" }
}
function Control([string]$Command, [hashtable]$Options = @{}, [switch]$AllowFailure) {
    $Options.ExpectedRunIdentity = $script:run
    Invoke-VerificationSession $Command $script:session $Options -AllowFailure:$AllowFailure
}
function Suspend-Session($Process) {
    $window = [GGLabSessionWindow]::Find($Process.Id)
    Check ($window -ne [IntPtr]::Zero) "$script:case-window"
    if ($Visible) { $null = [GGLabSessionWindow]::ShowWindowAsync($window, 6) }
    else { $null = [GGLabSessionWindow]::PostMessage($window, 5, [IntPtr]1, [IntPtr]::Zero) }
    Wait-VerificationCondition -Poll { Control status } -Until {
        param($r) $r.session.runtimeState -eq 'suspended'
    } -TimeoutSeconds 10
}
function Resume-Session($Process) {
    $window = [GGLabSessionWindow]::Find($Process.Id)
    if ($Visible) { $null = [GGLabSessionWindow]::ShowWindowAsync($window, 9) }
    else { $null = [GGLabSessionWindow]::PostMessage($window, 5, [IntPtr]::Zero, [IntPtr](640 -bor (360 -shl 16))) }
}
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $script:case = "$config-$backend"
            $directory = Join-Path $output $case
            $null = New-Item -ItemType Directory -Path $directory
            $script:session = "lifetime-$runId-$case"
            $script:run = $null; $process = $null; $start = $null; $failure = $null
            try {
                $state = Join-Path $root "Build/C4S/$runId-$($config.Substring(0,1))-$backend"
                if ($Visible) {
                    $executable = Join-Path $root "Build/Output/x64/$config/GraphicsGadgetLab.exe"
                    $log = Join-Path $directory 'external.log'
                    $arguments = @('--rhi', $backend, '--session', $session, '--session-lifetime', 'developer',
                        '--lab', 'gglab.lab.lighting_contract', '--window-size', '640x360', '--no-devtools',
                        '--validation', 'standard', '--fixed-delta-time', '0.0166667',
                        '--state-root', "`"$state`"", '--output-log', "`"$log`"")
                    $process = Start-Process $executable -ArgumentList $arguments -PassThru
                    $null = $process.Handle
                    $null = Wait-VerificationCondition -Poll {
                        if ($process.HasExited) { throw 'The external developer session exited during startup.' }
                        if (Test-Path $log) { Get-Content -Raw $log }
                    } -Until { param($r) $r -match '(?m)^session-ready: ' } -TimeoutSeconds $TimeoutSeconds
                    $attached = Invoke-VerificationSession attach $session
                    $start = @{ pid=$process.Id;runIdentity=$attached.runIdentity;outputLog=$log;ownership='external' }
                } else {
                    $start = Invoke-VerificationSession start $session @{ Configuration=$config;Rhi=$backend
                        SessionLifetime='developer';Lab='gglab.lab.lighting_contract';WindowSize='640x360'
                        NoDevTools=$true;Validation='standard';FixedDeltaTime=0.0166667;StateRoot=$state
                        StartTimeoutSeconds=$TimeoutSeconds }
                    $process = Get-Process -Id $start.pid; $null = $process.Handle
                }
                $script:run = $start.runIdentity
                Write-VerificationJson "$directory/start.json" $start
                $attached = Invoke-VerificationSession attach $session @{ ExpectedRunIdentity=$run }
                Check ($attached.ownership -eq 'attached' -and $attached.runIdentity -ceq $run) "$case-attach-identity"
                $ready = Wait-VerificationCondition -Poll { Control status } -Until {
                    param($r) $r.frame -and $r.frame.ready
                } -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/environment.json" $ready.environment
                Check ($ready.session.lifetime -eq 'developer' -and $null -eq $ready.session.idleTimeoutSeconds -and
                    $ready.capabilities.sessionLifetime.suspendedControl) "$case-developer-policy"
                $suspended = Suspend-Session $process
                $serial = $suspended.frame.frameSerial
                $parameters = Control parameters
                $predicate = @{ runIdentity=$run;contentIdentity=$parameters.controlState.contentIdentity
                    minHostViewRevision=$parameters.controlState.hostViewRevision
                    values=@(@{id='view.taa.enabled';type='bool';value=$true}) } | ConvertTo-Json -Compress -Depth 8
                $waiting = Control capture @{ NoWait=$true;RequiredFrameState=$predicate;SettleFrames=10000
                    FrameSelectionTimeoutMs=500;OutputDirectory="$directory/expired" }
                $expiryMessage = "Frame capture job $($waiting.jobId) expired before a matching frame was submitted."
                $expiryLog = Wait-VerificationCondition -Poll { Get-Content -LiteralPath $start.outputLog -Raw } `
                    -Until { param($r) $r.Contains($expiryMessage) } -TimeoutSeconds 5
                Check ($expiryLog.Contains($expiryMessage)) "$case-expiry-before-query"
                $expired = Control result @{JobId=$waiting.jobId}
                $after = Control status
                Write-VerificationJson "$directory/expired.json" $expired
                Check ($expired.status -eq 'failed' -and $expired.failureCode -eq 'FrameSelectionExpired' -and
                    $expired.publication -eq 'NotPublished' -and $after.frame.frameSerial -eq $serial -and
                    $after.capture.activeJobs -eq 0 -and $after.capture.reservedReceipts -eq 1 -and
                    -not (Test-Path "$directory/expired")) "$case-silent-suspended-expiry"
                $null = Control result @{JobId=$waiting.jobId;Consume=$true}
                $cancelledJob = Control capture @{NoWait=$true;RequiredFrameState=$predicate;SettleFrames=10000
                    OutputDirectory="$directory/cancelled"}
                $cancelled = Control capture-cancel @{JobId=$cancelledJob.jobId}
                $null = Control sequence-cancel
                $cancelledReceipt = Control result @{JobId=$cancelledJob.jobId;Consume=$true}
                Check ($cancelled.cancellation -eq 'Cancelled' -and $cancelledReceipt.status -eq 'cancelled') "$case-suspended-cancel"
                $queued = Control set @{NoWait=$true;Changes='[{"id":"view.gtao.radius","type":"float","value":1.5}]'}
                Resume-Session $process
                $applied = Wait-VerificationCondition -Poll { Control parameter-result @{BatchId=$queued.batchId} } `
                    -Until {param($r) $r.batch.terminal} -TimeoutSeconds $TimeoutSeconds
                Check ($applied.batch.succeeded -and $applied.batch.items[0].firstConsumed -gt $serial) "$case-resume-admitted-work"
                $null = Control parameter-result @{BatchId=$queued.batchId;Consume=$true}
                $experiment = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/Invoke-TaaComparison.ps1') @{
                    Attach=$true;Session=$session;Configuration=$config;Rhi=$backend;Python=$Python
                    RunDirectory="$directory/taa" }
                $receipt = $experiment.output | ConvertFrom-Json
                Write-VerificationJson "$directory/attached-experiment.json" $receipt
                Check ($experiment.exitCode -eq 0 -and $receipt.ok -and $receipt.cleanup.intentRestored -and
                    $receipt.cleanup.renderingVerified -and -not $process.HasExited) "$case-attach-restores-without-stop"
                $staleStop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=('f'*32)} -AllowFailure
                Check (-not $staleStop.ok -and $staleStop.errorCode -eq 'ExpectedRunChanged') "$case-stale-stop"
                $null = Suspend-Session $process
                $stop = Control stop
                Check ($stop.ok -and $process.WaitForExit(30000) -and $process.ExitCode -eq 0) "$case-suspended-stop"
                Write-VerificationJson "$directory/stop.json" @{response=$stop;exitCode=$process.ExitCode}
                $cases += @{configuration=$config;backend=$backend;visible=[bool]$Visible;passed=$true;directory=$directory}
            } catch { $failure = $_; throw } finally {
                if ($process -and -not $process.HasExited) {
                    try {
                        $options = @{}
                        if ($run) { $options.ExpectedRunIdentity = $run }
                        $null = Invoke-VerificationSession stop $session $options -AllowFailure
                        if (-not $process.WaitForExit(10000)) { throw "Session $session did not stop; inspect its log." }
                    } catch {
                        $process.Kill(); $process.WaitForExit()
                        Write-VerificationJson "$directory/cleanup-error.json" @{error=$_.Exception.Message;forced=$true}
                        if (-not $failure) { throw }
                    }
                }
            }
            $script:session = "idle-$runId-$case"
            $idle = Invoke-VerificationSession start $session @{Configuration=$config;Rhi=$backend
                IdleTimeout=3;Lab='gglab.lab.lighting_contract';NoDevTools=$true;WindowSize='640x360'
                StateRoot=(Join-Path $root "Build/C4S/$runId-idle-$($config.Substring(0,1))-$backend")}
            $process = Get-Process -Id $idle.pid; $null = $process.Handle
            $script:run = $idle.runIdentity
            try {
                $null = Suspend-Session $process
                Check ($process.WaitForExit(6000) -and $process.ExitCode -eq 0) "$case-suspended-automation-idle"
            } finally {
                if (-not $process.HasExited) { $null = Control stop }
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks;cases=$cases}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 24))
if (-not $result.ok) { exit 1 }
