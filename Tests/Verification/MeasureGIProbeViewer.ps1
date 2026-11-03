<# Quiet Release viewer overhead. Images, validation and usability belong to TestGIProbeViewer.ps1. #>
[CmdletBinding()]
param(
    [string[]]$Rhi = @('dx12','vulkan'),
    [int[]]$Budgets = @(256,2048,4096,16384),
    [ValidateRange(2,10)][int]$RepeatCount = 2,
    [ValidateRange(60,3600)][int]$WarmupFrames = 120,
    [ValidateRange(0,120)][int]$DiscardFrames = 30,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
Import-Module "$PSScriptRoot/GIProbeViewerSupport.psm1"
$Rhi = @($Rhi | ForEach-Object {$_ -split ','})
if (-not $Rhi.Count -or @($Rhi | Where-Object {$_ -notin @('dx12','vulkan')}).Count -or
    -not $Budgets.Count -or @($Budgets | Where-Object {$_ -lt 1 -or $_ -gt 16384}).Count) {
    throw 'Viewer measurement requires explicit backends and budgets in [1,16384].'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbeViewerCost" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
$records = @()
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root 'Release'
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/settings.json" @{backends=$Rhi;budgets=$Budgets;repeats=$RepeatCount;
        warmupFrames=$WarmupFrames;discardFrames=$DiscardFrames;extent=@(1280,720);timedFrames=360}
    foreach ($backend in $Rhi) {
        foreach ($fixture in @(@{id='f4';value=4},@{id='coastal';value=15})) {
            $directory = "$output/$backend/$($fixture.id)"
            $null = New-Item -ItemType Directory -Path $directory -Force
            $sessionId = [Guid]::NewGuid().ToString('N').Substring(0,8)
            $session = 'viewer-cost-'+$sessionId
            $start = $null
            try {
                $start = Invoke-VerificationSession start $session @{Configuration='Release';Rhi=$backend;
                    Lab='gglab.lab.gi_reference';WindowSize='1280x720';NoDevTools=$true;Validation='off';
                    StateRoot="$root/Build/PVC-$sessionId";StartTimeoutSeconds=$TimeoutSeconds}
                Write-VerificationJson "$directory/start.json" $start
                if ($start.ownership -ne 'created') { throw 'Viewer timing requires an owned quiet session.' }
                $run = $start.runIdentity
                $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$directory/inputs.json" (Initialize-ProbeViewer $session $run $fixture.value $TimeoutSeconds)
                Write-VerificationJson "$directory/parameters.json" (Invoke-VerificationSession parameters $session @{ExpectedRunIdentity=$run})
                $fixtureStatus = Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run}
                # GI_CostStatic is authored from the fixture's first reference view.
                $warmupView = @($fixtureStatus.frame.referenceViews)[0]
                if (-not $warmupView) { throw 'Viewer measurement requires the authored static reference view.' }
                for ($repeat = 0; $repeat -lt $RepeatCount; ++$repeat) {
                    $cases = @(Get-ProbeViewerCases $Budgets)
                    if ($repeat % 2) {
                        $middle = @($cases[1..($cases.Count-2)])
                        [Array]::Reverse($middle)
                        $cases = @($cases[0]) + $middle + @($cases[-1])
                    }
                    foreach ($case in $cases) {
                        $relative = "$backend/$($fixture.id)/r$repeat/$($case.id)"
                        $path = "$output/$relative"
                        $null = New-Item -ItemType Directory -Path $path -Force
                        Set-ProbeViewerCase $session $run $case $TimeoutSeconds
                        # Allocation and completed-count evidence is deliberately outside the timed range.
                        # Reapply that view to start Ready temporal continuity after activation.
                        # An earlier unready epoch can otherwise leave the capture settle counter at zero.
                        $null = Invoke-ProbeViewerCapture $session $run "$path/allocation" ($case.kind -ne 'off') 32 $false $TimeoutSeconds $warmupView
                        $null = Wait-ProbeViewerWarmup -Session $session -Run $run -Frames $WarmupFrames `
                            -Directory "$path/warmup" -TimeoutSeconds $TimeoutSeconds
                        $sequence = Invoke-VerificationSession sequence $session @{ExpectedRunIdentity=$run;CameraPath='GI_CostStatic';
                            GpuTiming=$true;OutputDirectory="$path/samples";TimeoutSeconds=$TimeoutSeconds}
                        Write-VerificationJson "$path/sequence.json" $sequence
                        if ($sequence.sequence.state -ne 'completed' -or $sequence.sequence.timingPublication.outcome -ne 'Published' -or
                            $sequence.sequence.parameterPublication.outcome -ne 'Published') { throw 'Incomplete viewer timing range.' }
                        Write-VerificationJson "$path/status.json" (Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run})
                        $records += @{directory=$relative;rhi=$backend;fixture=$fixture.id;fixtureValue=$fixture.value;repeat=$repeat;case=$case}
                        Write-VerificationJson "$output/cases.json" @($records)
                        if ($records.Count -le 2) {
                            Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeViewer.py" @('--cost',$output,'--case-index',([string]($records.Count-1))) `
                                "$path/preflight.json" 'Viewer timing preflight failed'
                        }
                        [Console]::Error.WriteLine("$backend $($fixture.id) repeat $repeat $($case.id) completed")
                    }
                }
            } finally { Stop-ProbeViewer $session $start $directory }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbeViewer.py" @('--cost',$output) "$output/validator.json" 'Viewer cost validation failed'
    Invoke-VerificationPython $Python "$root/Scripts/Verification/ValidateCaptureBundles.py" @($output) "$output/bundle-audit.json" 'Viewer timing bundle audit failed'
    Invoke-VerificationPython $Python "$root/Scripts/Verification/ValidateRenderGraphExports.py" @($output) "$output/graph-audit.json" 'Viewer allocation audit failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Viewer measurement sources changed during the run.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root 'Release') 'Viewer measurement binaries changed during the run.'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output;cases=$records.Count;performanceAcceptance=$false}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message}
    throw
}
