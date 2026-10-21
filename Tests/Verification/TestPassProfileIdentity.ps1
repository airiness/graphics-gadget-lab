<#
.SYNOPSIS
Checks that hidden GPU sequence scopes join graph declarations by pass identity.
.DESCRIPTION
Runs the same Temporal AA Lab path on the selected backends. Reads graph products
through completed bundle manifests, checks timing scope identities and sample
counts, and compares the identity sets across backends. This checks identity and
coverage, not timing accuracy or performance. Every started session is stopped.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Develop', 'Release')]
    [string[]]$Configuration = @('Release'),
    [ValidateSet('dx12', 'vulkan')]
    [string[]]$Rhi = @('dx12', 'vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 6)
if (-not $OutputDirectory -and -not $RunDirectory) {
    $OutputDirectory = Join-Path $root 'Build/Validation/PassProfileIdentity'
}
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId $runId
$cases = @()
try {
    foreach ($config in $Configuration) {
        $previousKeys = $null
        foreach ($backend in $Rhi) {
            $directory = Join-Path $output "$config-$backend"
            $null = New-Item -ItemType Directory -Path $directory
            $sessionId = "profile-$runId-$config-$backend"
            $start = $null
            try {
                $start = Invoke-VerificationSession start $sessionId @{
                    Configuration = $config; Rhi = $backend; Lab = 'gglab.lab.temporal_aa'
                    NoDevTools = $true; WindowSize = '320x180'
                    StateRoot = (Join-Path $directory 'State'); StartTimeoutSeconds = $TimeoutSeconds
                }
                Write-VerificationJson "$directory/start.json" $start
                $warmup = Invoke-VerificationSession capture $sessionId @{
                    SettleFrames = 8; OutputDirectory = $directory; Label = 'startup'; TimeoutSeconds = $TimeoutSeconds
                }
                $null = Get-VerificationCaptureFile $warmup '.png'
                Write-VerificationJson "$directory/startup.json" $warmup
                $exports = @(0, 179 | ForEach-Object {
                    @{ frame = $_; targets = @(@{ resource = 'TAA.ResolvedSceneColor' }); graph = $true }
                }) | ConvertTo-Json -Compress -Depth 8
                $sequenceDirectory = Join-Path $directory 'Sequence'
                $response = Invoke-VerificationSession sequence $sessionId @{
                    CameraPath = 'SEQ_TemporalAALab_MovingObject'; GpuTiming = $true
                    Exports = $exports; OutputDirectory = $sequenceDirectory; Label = 'identity'
                    TimeoutSeconds = $TimeoutSeconds
                }
                Write-VerificationJson "$directory/sequence.json" $response
                if ($response.sequence.state -ne 'completed' -or $response.sequence.gpuTiming.frames -ne 180) {
                    throw 'The sequence did not complete all requested GPU timing samples.'
                }
                $graphKeys = @()
                $graphs = @()
                foreach ($manifestFile in Get-ChildItem -LiteralPath $sequenceDirectory -Filter '*.capture-bundle.json') {
                    $manifest = Get-Content -LiteralPath $manifestFile.FullName -Raw | ConvertFrom-Json
                    foreach ($product in @($manifest.products | Where-Object schema -eq 'gglab.render-graph')) {
                        foreach ($file in $product.files) {
                            $graphPath = if ([IO.Path]::IsPathRooted($file)) { $file } else {
                                Join-Path $manifestFile.DirectoryName $file
                            }
                            $graph = Get-Content -LiteralPath $graphPath -Raw | ConvertFrom-Json
                            $graphs += $graphPath
                            foreach ($pass in @($graph.compile.passes | Where-Object { -not $_.culled })) {
                                $graphKeys += (@($pass.name, $pass.occurrence) | ConvertTo-Json -Compress)
                            }
                        }
                    }
                }
                if ($graphs.Count -ne 2) { throw 'Expected both sequence graph products in published manifests.' }
                $keys = @()
                $passKeys = @()
                foreach ($scope in $response.sequence.gpuTiming.scopes) {
                    if ($scope.kind -eq 'pass') {
                        $key = @($scope.name, $scope.occurrence) | ConvertTo-Json -Compress
                        if ($scope.occurrence -lt 1 -or $key -notin $graphKeys) {
                            throw "GPU pass identity has no live graph declaration: $key"
                        }
                        $passKeys += $key
                    } elseif ($scope.kind -ne 'other' -or $null -ne $scope.occurrence) {
                        throw 'Non-pass GPU scopes must have kind other and null occurrence.'
                    }
                    if ($scope.count -lt 1 -or $scope.count -gt 180) { throw 'A scope counted multiple occurrences as samples of one frame.' }
                    $keys += (@($scope.kind, $scope.name, $scope.occurrence) | ConvertTo-Json -Compress)
                }
                $keys = @($keys | Sort-Object)
                if (@($graphKeys | Where-Object { $_ -notin $passKeys }).Count) {
                    throw 'A live graph declaration has no GPU timing series.'
                }
                if (-not $keys.Count -or @($keys | Select-Object -Unique).Count -ne $keys.Count) {
                    throw 'GPU scope identities are empty or duplicated.'
                }
                if ($null -ne $previousKeys -and ($keys -join "`n") -cne ($previousKeys -join "`n")) {
                    throw 'The same graph produced different scope identities across backends.'
                }
                $previousKeys = $keys
                Write-VerificationJson "$directory/identities.json" $keys
                $cases += @{ configuration = $config; backend = $backend; passed = $true
                    frames = 180; scopes = $keys.Count; graphs = $graphs; directory = $directory }
            } finally {
                if ($start) {
                    $null = Stop-VerificationSession $sessionId -StartedSession $start `
                        -ReceiptPath "$directory/stop.json" -RequireCleanExit
                }
            }
            $log = Get-Content -LiteralPath $start.outputLog -Raw
            if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') {
                throw "Native validation errors: $($start.outputLog)"
            }
        }
    }
    $result = @{ ok = $true; directory = $output; cases = $cases }
} catch { $result = @{ ok = $false; directory = $output; cases = $cases; error = $_.Exception.Message } }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 12))
if (-not $result.ok) { exit 1 }
