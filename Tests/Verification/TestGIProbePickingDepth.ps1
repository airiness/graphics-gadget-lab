<# Shared crop transform, tiny attachments, exact D32/tie policy and clip endpoints. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [ValidateRange(4,16)]
    [int]$RasterSubpixelBits = 8,
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
Import-Module "$PSScriptRoot/GIProbeViewerSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Picking depth requires one configuration and explicit backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIProbePickingDepth" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity { Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects') }
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    Write-VerificationJson "$output/qualification-inputs.json" @{rasterSubpixelBits=$RasterSubpixelBits}
    foreach ($backend in $Rhi) {
        $session = 'pick-depth-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
        $start = $null
        $directory = "$output/$backend"
        $null = New-Item -ItemType Directory -Path $directory -Force
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.coordinate_conformance';WindowSize='640x400';NoDevTools=$true;Validation='standard';
                StateRoot="$root/Build/EPickState-$session";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            foreach ($convention in @('Standard','Reversed')) {
                foreach ($first in (0..10 | ForEach-Object { $_ * 3 })) {
                    # Two source arrays plus three ID/depth pairs fit eight payload slots.
                    $targets = @('Inputs','Scene') | ForEach-Object { @{resource="GI.Depth.$convention.$_"} }
                    if ($first -eq 30) {
                        $targets = @('ID','D32') | ForEach-Object { @{resource="GI.Pick.$convention.ClipReference.$_"} }
                    }
                    $indices = $first..($first+2)
                    foreach ($index in $indices) {
                        $targets += @{resource="GI.Pick.$convention.$index.ID"}
                        $targets += @{resource="GI.Pick.$convention.$index.D32"}
                    }
                    $case = "$directory/$convention/cases-$first"
                    $null = New-Item -ItemType Directory -Path $case -Force
                    $products = @(@{kind='graph'},@{kind='resource';targets=@($targets)})
                    $receipt = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$start.runIdentity;
                        Timing='next-frame';SettleFrames=0;Products=($products | ConvertTo-Json -Depth 8 -Compress);
                        OutputDirectory=$case;TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$case/receipt.json" $receipt
                    if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') { throw 'Picking depth did not publish.' }
                }
            }
            $targets = @('Standard','Reversed') | ForEach-Object {
                $convention = $_
                @('Inputs','Scene','Winners','D32') | ForEach-Object { @{resource="GI.Depth.$convention.$_"} }
            }
            $case = "$directory/controls"
            $null = New-Item -ItemType Directory -Path $case -Force
            $products = @(@{kind='graph'},@{kind='resource';targets=@($targets)})
            $receipt = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$start.runIdentity;
                Timing='next-frame';SettleFrames=0;Products=($products | ConvertTo-Json -Depth 8 -Compress);
                OutputDirectory=$case;TimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$case/receipt.json" $receipt
            if ($receipt.status -ne 'completed' -or $receipt.publication -ne 'Published') { throw 'Full-view D32 controls failed.' }
        } finally { Stop-ProbeViewer $session $start $directory }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIProbePicking.py" @('--depth',$output,'--subpixel-bits',"$RasterSubpixelBits") "$output/oracle-result.json" 'Picking D32/crop failed'
    Assert-VerificationArtifacts $root $output $Python 'Picking depth artifacts failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Picking depth sources changed.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Picking depth binaries changed.'
    Write-VerificationJson "$output/result.json" @{ok=$true;directory=$output}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;directory=$output;error=$_.Exception.Message}
    throw
}
