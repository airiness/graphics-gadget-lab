<# Repeated hidden startup, readiness, capture and shutdown; retained failure diagnostics. #>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Develop','Release')][string]$Configuration='Develop',
    [string[]]$Rhi=@('dx12','vulkan'),
    [ValidateRange(1,16)][int]$Repeats=2,
    [string]$ShaderCacheDirectory,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python='python',
    [ValidateRange(30,600)][int]$TimeoutSeconds=120
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 3.0
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$Rhi=@($Rhi | ForEach-Object {$_ -split ','} | Select-Object -Unique)
if (-not $Rhi.Count -or @($Rhi | Where-Object {$_ -notin @('dx12','vulkan')}).Count) { throw 'Invalid RHI selection.' }
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory="$root/Build/Validation/StartupReadiness" }
$output=New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
}
$cases=@()
try {
    $Python=(Get-Command $Python -CommandType Application | Select-Object -First 1).Source
    $source=Source-Identity
    $binaries=Get-VerificationBinaryIdentity $root $Configuration
    Write-VerificationJson "$output/client.json" @{powerShellVersion=$PSVersionTable.PSVersion.ToString();edition=$PSVersionTable.PSEdition}
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        foreach ($attempt in 1..$Repeats) {
            $coldState=$null
            foreach ($warm in @($false,$true)) {
                $name="$backend-$(if($warm){'warm'}else{'cold'})-$attempt"
                $directory="$output/$name"
                $state="$directory/state"
                $null=New-Item -ItemType Directory -Path $state
                $seed=if ($warm) {$coldState} elseif ($ShaderCacheDirectory) {"$ShaderCacheDirectory/$backend"} else {$null}
                if ($seed) {
                    foreach ($kind in @('ShaderCache','ShaderArtifacts')) {
                        Copy-Item -LiteralPath "$seed/$kind" -Destination "$state/$kind" -Recurse
                    }
                    if ($warm) { Copy-Item -LiteralPath "$seed/DerivedDataCache" -Destination "$state/DerivedDataCache" -Recurse }
                }
                $session='startup-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
                $start=$null; $process=$null; $failure=$null
                $case=@{backend=$backend;name=$name;ready=$false;capturePublished=$false;normalExit=$false;
                    shaderSeeded=[bool]$seed;derivedCacheWarm=$warm}
                try {
                    $start=Invoke-VerificationSession start $session @{Configuration=$Configuration;Rhi=$backend;
                        Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                        StateRoot=$state;StartTimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/start.json" $start
                    if ($start.ownership -ne 'created') { throw 'Startup checks require an owned session.' }
                    $process=Get-Process -Id $start.pid
                    $null=$process.Handle
                    # PowerShell 7 decodes ISO timestamps as DateTime; 5.1 keeps strings.
                    if ($process.StartTime.ToUniversalTime().Ticks -ne ([DateTimeOffset]$start.processStartedAtUtc).UtcDateTime.Ticks -or
                        $process.Path -ine [IO.Path]::GetFullPath("$root/Build/Output/x64/$Configuration/GraphicsGadgetLab.exe")) {
                        $process.Dispose()
                        $process=$null
                        throw 'The created startup-check process identity changed.'
                    }
                    $status=Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$start.runIdentity} } `
                        -Until {param($r) $r.frame -and $r.frame.ready} -TimeoutSeconds $TimeoutSeconds
                    Write-VerificationJson "$directory/ready.json" $status
                    $case.ready=$true
                    $capture=Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$start.runIdentity;
                        SettleFrames=2;OutputDirectory="$directory/capture";TimeoutSeconds=$TimeoutSeconds}
                    Write-VerificationJson "$directory/capture.json" $capture
                    if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Startup capture was not published.' }
                    $case.capturePublished=$true
                } catch {
                    $failure=$_
                    $case.error=$_.Exception.Message
                    if ($process -and -not $process.HasExited) {
                        $case.diagnostic=Save-VerificationProcessDump $process "$directory/stalled-process.dmp"
                        Write-VerificationJson "$directory/diagnostic.json" $case.diagnostic
                    }
                    throw
                } finally {
                    try {
                        if ($start -and $start.ownership -eq 'created') {
                            try {
                                $stop=Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity} -AllowFailure
                                Write-VerificationJson "$directory/stop.json" $stop
                                $case.normalExit=$stop.ok -and $stop.PSObject.Properties['exitCode'] -and $stop.exitCode -eq 0
                                if (-not $case.normalExit) { throw 'The owned startup-check session did not stop cleanly.' }
                            } catch {
                                $case.stopError=$_.Exception.Message
                                if ($process -and -not $process.HasExited) {
                                    if (-not $case.ContainsKey('diagnostic')) {
                                        $case.diagnostic=Save-VerificationProcessDump $process "$directory/stalled-process.dmp"
                                        Write-VerificationJson "$directory/diagnostic.json" $case.diagnostic
                                    }
                                    $process.Kill()
                                    $case.forcedCleanup=$true
                                    $case.cleanupCompleted=$process.WaitForExit(10000)
                                    if (-not $case.cleanupCompleted) { throw 'The owned process did not exit after forced cleanup.' }
                                }
                                if (-not $failure) { throw }
                            } finally {
                                Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                            }
                        }
                    } finally {
                        # A shutdown/copy failure must still retain its failed case
                        # and release the original process handle before propagating.
                        if ($process) { $process.Dispose() }
                        $cases+=$case
                        Write-VerificationJson "$output/cases.json" $cases
                    }
                }
                $log=[IO.File]::ReadAllText("$directory/output.log").Replace([string][char]0,'')
                if ($log -match '\[(error|critical)\]|VUID-|GPU_BASED_VALIDATION_|assertion failed') { throw "Native failure: $name" }
                if (-not $case.normalExit) { throw "Unclean startup-check exit: $name" }
                $coldState=$state
            }
        }
    }
    # This suite requests images only; numerical/graph exports have separate suites.
    Invoke-VerificationPython $Python "$root/Scripts/Verification/ValidateCaptureBundles.py" @($output) `
        "$output/bundle-audit.json" 'Startup bundle audit failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Startup source inputs changed'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration) 'Startup binaries changed'
    $result=@{ok=$true;status='passed';backends=$Rhi;cases=$cases;observations=$cases.Count;
        shaderCachesPreseeded=[bool]$ShaderCacheDirectory;renderingQualityQualified=$false;startupRootCauseResolved=$false}
} catch {
    $result=@{ok=$false;status='failed';error=$_.Exception.Message;cases=$cases;startupRootCauseResolved=$false}
}
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 10 -Compress))
if ($result.status -ne 'passed') { exit 1 }
