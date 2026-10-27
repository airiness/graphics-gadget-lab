<#
.SYNOPSIS
Qualifies the guarded hidden TAA experiment on each selected configuration/backend.
#>
[CmdletBinding()]
param(
    [string[]]$Configuration=@('Develop'), [string[]]$Rhi=@('dx12','vulkan'),
    [string]$OutputDirectory, [string]$RunDirectory, [string]$Python='python'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3.0
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory=Join-Path $root 'Build/Validation/TaaComparisonTests' }
$output=New-VerificationRunDirectory $OutputDirectory $RunDirectory ([Guid]::NewGuid().ToString('N').Substring(0,8))
$cases=@()
try {
    foreach ($config in $Configuration) {
        foreach ($backend in $Rhi) {
            $execution=Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/Invoke-TaaComparison.ps1') @{
                Configuration=$config;Rhi=$backend;Python=$Python;RunDirectory=(Join-Path $output "$config-$backend") }
            $receipt=$execution.output | ConvertFrom-Json
            $cases+=@{configuration=$config;backend=$backend;passed=($execution.exitCode -eq 0 -and $receipt.ok);receipt=$receipt}
            if ($execution.exitCode -ne 0 -or -not $receipt.ok) { throw "TAA experiment failed on $config/$backend; inspect $($receipt.directory)." }
        }
    }
    $result=@{ok=$true;directory=$output;cases=$cases}
} catch { $result=@{ok=$false;directory=$output;cases=$cases;error=$_.Exception.Message} }
Write-VerificationJson (Join-Path $output 'result.json') $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 32))
if (-not $result.ok) { exit 1 }
