<# Develop correctness/usability evidence. Timings from this runner are not performance evidence. #>
[CmdletBinding()]
param(
    [ValidateSet('Develop')][string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [switch]$DefaultsOnly,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
Import-Module "$PSScriptRoot/GIProbeViewerSupport.psm1"
$Rhi = @($Rhi | ForEach-Object {$_ -split ','})
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object {$_ -notin @('dx12','vulkan')}).Count) {
    throw 'Viewer correctness requires Develop and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeViewer" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))

# Resize only this runner's owned hidden process. No show/activation flags are used.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ProbeViewerWindow {
    delegate bool WindowCallback(IntPtr hwnd, IntPtr arg);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback callback, IntPtr arg);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll", SetLastError=true)] static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    public static void Resize(uint pid, int width, int height) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hwnd, arg) => {
            uint owner; Rect client;
            GetWindowThreadProcessId(hwnd, out owner);
            if (owner == pid && GetClientRect(hwnd, out client) && client.right > 0 && client.bottom > 0) {
                found = hwnd; return false;
            }
            return true;
        }, IntPtr.Zero);
        Rect c, w;
        if (found == IntPtr.Zero || !GetClientRect(found, out c) || !GetWindowRect(found, out w) ||
            !SetWindowPos(found, IntPtr.Zero, 0, 0, width+w.right-w.left-c.right, height+w.bottom-w.top-c.bottom, 0x16)) {
            throw new InvalidOperationException("Cannot resize the owned hidden viewer window.");
        }
    }
}
'@

function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
$records = @()
$receiverRecords = @()
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root 'Develop'
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/receiver-cases.json" @()
    foreach ($backend in $Rhi) {
        foreach ($fixture in @(@{id='f4';value=4},@{id='coastal';value=15})) {
            $directory = "$output/$backend/$($fixture.id)"
            $null = New-Item -ItemType Directory -Path $directory -Force
            $session = 'viewer-check-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
            $start = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration='Develop';Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize='1280x720';NoDevTools=$true;Validation='standard';
                    StateRoot="$directory/state";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Viewer correctness requires an owned session.' }
                $run = $start.runIdentity
                $null = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}} `
                    -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                $null = Initialize-ProbeViewer $session $run $fixture.value $TimeoutSeconds
                if ($DefaultsOnly) {
                    # Leave budget inherited: an explicit assignment would not test the default.
                    $null = Set-ProbeViewerValues $session $run @(
                        @{id='view.gi_probes.enabled';type='bool';value=$true},
                        @{id='view.gi_probes.mode';type='enum';value=0},
                        @{id='view.gi_probes.depth_mode';type='enum';value=1}) $TimeoutSeconds
                    $relative = "$backend/$($fixture.id)/default-budget"
                    $null = Invoke-ProbeViewerCapture $session $run "$output/$relative" $true 120 $true $TimeoutSeconds
                    $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;defaultBudget=$true;
                        case=@{id='default-budget';kind='classification';budget=4096;mode=0;depth=1;selected=$false}}
                    Write-VerificationJson "$output/cases.json" @($records)
                    continue
                }
                foreach ($case in @(Get-ProbeViewerCases)) {
                    Set-ProbeViewerCase $session $run $case $TimeoutSeconds
                    $relative = "$backend/$($fixture.id)/$($case.id)"
                    $null = Invoke-ProbeViewerCapture $session $run "$output/$relative" ($case.kind -ne 'off') 120 $true $TimeoutSeconds
                    $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;case=$case}
                    Write-VerificationJson "$output/cases.json" @($records)
                }
                $case = @{kind='selected';budget=2048;mode=0;depth=1;selected=$true}
                Set-ProbeViewerCase $session $run $case $TimeoutSeconds
                foreach ($extent in @(@{id='small';width=800;height=500},@{id='restored';width=1280;height=720})) {
                    [ProbeViewerWindow]::Resize([uint32]$start.pid,$extent.width,$extent.height)
                    $null = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}} `
                        -Until {param($r) $r.session.width -eq $extent.width -and $r.session.height -eq $extent.height} -TimeoutSeconds 30
                    $relative = "$backend/$($fixture.id)/resize-$($extent.id)"
                    $null = Invoke-ProbeViewerCapture $session $run "$output/$relative" $true 32 $true $TimeoutSeconds
                    $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;case=$case;extent=@($extent.width,$extent.height)}
                }
                $before = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                $generation = $before.frame.giField.summary.field.generation
                $null = Set-ProbeViewerValues $session $run @(@{id='gi_reference.seed';type='uint';value=2}) $TimeoutSeconds
                $relative = "$backend/$($fixture.id)/field-replaced"
                $null = Invoke-ProbeViewerCapture $session $run "$output/$relative" $true 32 $true $TimeoutSeconds
                $after = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                if ($after.frame.giField.summary.field.generation -eq $generation -or
                    $after.frame.giField.summary.probeVisualization.highlight.state -ne 'field-mismatch') {
                    throw 'Field replacement silently rebound the accepted selection.'
                }
                $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;case=$case;replacement=$true}
                Write-VerificationJson "$output/cases.json" @($records)
                # B has full-view receiver prerequisites. Record its allocation separately;
                # diagnostic capture/validation time never becomes a Release cost sample.
                $null = Set-ProbeViewerValues $session $run @(
                    @{id='gi_reference.receiver_diagnostics';type='bool';value=$true},
                    @{id='view.gi_probes.enabled';type='bool';value=$false}) $TimeoutSeconds
                $status = Wait-VerificationCondition -Poll {Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}} `
                    -Until {param($r) $r.frame.giField.summary.receiverView.supported} -TimeoutSeconds $TimeoutSeconds
                $summary = $status.frame.giField.summary
                $request = @{pixel=@(640,360);expectedGeneration=$summary.field.generation;
                    expectedViewRevision=$summary.receiverView.revision;expectedViewId=$summary.receiverView.viewId;
                    expectedRefreshEpoch=$summary.field.refreshEpoch;markers=$true}
                $relative = "$backend/$($fixture.id)/receiver-prerequisites"
                $path = "$output/$relative"
                $null = New-Item -ItemType Directory -Path $path -Force
                $products = @(@{kind='graph'},@{kind='image';source='composited'},
                    @{kind='resource';targets=@(@{resource='GI.Field.ReceiverInspection'})})
                $receipt = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';SettleFrames=32;
                    GIReceiverInspection=($request | ConvertTo-Json -Compress);
                    Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);OutputDirectory=$path;TimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$path/receipt.json" $receipt
                if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') { throw 'Receiver prerequisite evidence did not publish.' }
                $receiverRecords += @{directory=$relative;rhi=$backend;fixture=$fixture.id}
                Write-VerificationJson "$output/receiver-cases.json" @($receiverRecords)
            } finally { Stop-ProbeViewer $session $start $directory }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeViewer.py" @('--images',$output) "$output/validator.json" 'Viewer correctness failed'
    Assert-VerificationArtifacts $root $output $Python 'Viewer correctness failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Viewer correctness sources changed during the run.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root 'Develop') 'Viewer correctness binaries changed during the run.'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;cases=$records.Count;defaultsOnly=[bool]$DefaultsOnly;timingQualified=$false}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message}
    throw
}
