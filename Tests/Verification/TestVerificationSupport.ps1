<#
.SYNOPSIS
Checks verification transport, polling, cleanup and runner selection without a GPU.
#>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1')
$module = Get-Module VerificationSupport
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Build/Validation/VerificationSupport' }
$output = Join-Path ([IO.Path]::GetFullPath($OutputDirectory)) ([Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $output
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
function Reject([scriptblock]$Action, [string]$Pattern, [string]$Name) {
    try { $null = & $Action } catch {
        Check ($_.Exception.Message -match $Pattern) $Name
        return
    }
    throw "Expected rejection: $Name"
}
try {
    $standaloneRoot = Join-Path $output 'standalone'
    $firstRun = New-VerificationRunDirectory -OutputDirectory $standaloneRoot -RunId 'first'
    $secondRun = New-VerificationRunDirectory -OutputDirectory $standaloneRoot -RunId 'second'
    Check ($firstRun -ne $secondRun -and (Test-Path $firstRun) -and (Test-Path $secondRun)) 'standalone-runs-stay-isolated'
    $exactRun = Join-Path $output 'exact-run'
    Check ((New-VerificationRunDirectory -RunDirectory $exactRun -RunId 'unused') -eq $exactRun) 'exact-run-directory-without-another-id'
    Reject { New-VerificationRunDirectory -RunDirectory $exactRun } 'already exists' 'existing-run-directory-is-not-reused'
    Reject { New-VerificationRunDirectory -RunDirectory $exactRun -OutputDirectory $standaloneRoot } 'either' 'ambiguous-run-directory'
    $fixtureDirectory = Join-Path $output "fixture ' quoted"
    $null = New-Item -ItemType Directory -Path $fixtureDirectory
    $fixture = Join-Path $fixtureDirectory 'session.ps1'
    $source = @'
param([string]$Command, [string]$Session, [string[]]$Names, [switch]$Disabled,
    [bool]$Flag, [int]$Count, [string]$Text)
switch ($Command) {
    'invalid' { [Console]::Out.WriteLine('not-json'); exit 1 }
    'rejected' { [Console]::Out.WriteLine('{"ok":false,"error":"expected rejection"}'); exit 1 }
    'bad-exit' { [Console]::Out.WriteLine('{"ok":true}'); exit 7 }
    'failed-capture' { [Console]::Out.WriteLine('{"ok":true,"status":"failed"}'); exit 0 }
    'large' { [Console]::Out.Write('x' * 131072); [Console]::Error.Write('y' * 131072); exit 0 }
    'stop' {
        if ($Session -eq 'missing') { [Console]::Out.WriteLine('{"ok":false,"error":"missing"}'); exit 1 }
        $code = if ($Session -eq 'abnormal') { 7 } else { 0 }
        [Console]::Out.WriteLine((@{ ok=$true; exitCode=$code } | ConvertTo-Json -Compress)); exit 0
    }
    default {
        [Console]::Out.WriteLine((@{ ok=$true; names=@($Names); disabled=[bool]$Disabled;
            flag=$Flag; count=$Count; text=$Text; session=$Session } | ConvertTo-Json -Compress)); exit 0
    }
}
'@
    [IO.File]::WriteAllText($fixture, $source, (New-Object Text.UTF8Encoding($false)))
    & $module { param($path) $script:SessionScript = $path } $fixture
    $text = [char]0x6D4B + " ' literal `$value"
    $receipt = Invoke-VerificationSession echo 'quoted-session' @{ Names=@('boundary','production'); Disabled=$false; Flag=$false; Count=0; Text=$text }
    Check ($receipt.names.Count -eq 2 -and $receipt.names[1] -eq 'production' -and -not $receipt.disabled -and -not $receipt.flag -and $receipt.count -eq 0 -and $receipt.text -eq $text) 'typed-arguments-and-unicode'
    Check ((Invoke-VerificationSession failed-capture demo).status -eq 'failed') 'domain-failure-is-not-a-control-failure'
    Reject { Invoke-VerificationSession rejected demo } 'expected rejection' 'control-failure'
    Check (-not (Invoke-VerificationSession rejected demo -AllowFailure).ok) 'expected-control-rejection'
    Reject { Invoke-VerificationSession bad-exit demo } 'failed' 'process-exit-failure'
    Reject { Invoke-VerificationSession invalid demo -AllowFailure } 'invalid JSON' 'malformed-result-is-not-allowed-failure'
    Reject { Invoke-VerificationSession echo demo @{ Session='override' } } 'Reserved' 'reserved-parameters'
    $large = Invoke-VerificationScript $fixture @{ Command='large' }
    Check ($large.exitCode -eq 0 -and $large.output.Length -eq 131072 -and $large.error.Length -eq 131072) 'drain-both-process-streams'
    $state = [pscustomobject]@{ count=0 }
    $value = Wait-VerificationCondition -Poll { $state.count++; $state.count } -Until { param($n) $n -eq 2 } -TimeoutSeconds 1 -IntervalMilliseconds 1
    Check ($value -eq 2 -and $state.count -eq 2) 'poll-until-ready'
    $null = Wait-VerificationCondition -Poll { Check $true 'poll-can-call-script-helper'; 'ready' } `
        -Until { param($value) $value -eq 'ready' } -TimeoutSeconds 0
    Reject { Wait-VerificationCondition -Poll { 'last-evidence' } -Until { $false } -TimeoutSeconds 0 -OnTimeout { param($last) throw "Timeout: $last" } } 'last-evidence' 'timeout-keeps-last-evidence'
    Reject { Wait-VerificationCondition -Poll { throw 'poll-failure' } -Until { $true } -TimeoutSeconds 1 } 'poll-failure' 'poll-errors-propagate'
    Check ((Wait-VerificationCondition -Poll { 'pending' } -Until { $false } -TimeoutSeconds 0 -OnTimeout { param($last) $last }) -eq 'pending') 'report-only-timeout-policy'
    $path = Join-Path $output 'empty.json'
    Write-VerificationJson $path @()
    Check (([IO.File]::ReadAllText($path) -replace '\s', '') -eq '[]' -and [IO.File]::ReadAllBytes($path)[0] -eq 91) 'empty-array-without-bom'
    $stopPath = Join-Path $output 'stop.json'
    $null = Stop-VerificationSession normal -ReceiptPath $stopPath -RequireCleanExit
    Check ((Get-Content $stopPath -Raw | ConvertFrom-Json).exitCode -eq 0) 'clean-stop-receipt'
    Reject { Stop-VerificationSession abnormal -ReceiptPath $stopPath -RequireCleanExit } 'clean exit code' 'abnormal-process-exit'
    Check ((Get-Content $stopPath -Raw | ConvertFrom-Json).exitCode -eq 7) 'failed-stop-receipt-is-retained'
    $null = Stop-VerificationSession missing -AllowMissing
    $stop = Stop-VerificationSession missing -AllowFailure
    Check (-not $stop.ok) 'audit-stop-rejection-policy'
    Reject { Stop-VerificationSession missing -StartedSession @{pid=0} -AllowMissing } 'could not be stopped' 'owned-start-must-stop'
    foreach ($case in @(
        @{ name='exact'; process=@{Name='GraphicsGadgetLab.exe';CommandLine='app --session run-1 --hidden'}; expected=$true },
        @{ name='quoted'; process=@{Name='GraphicsGadgetLab.exe';CommandLine='app --session "run-1" --hidden'}; expected=$true },
        @{ name='prefix'; process=@{Name='GraphicsGadgetLab.exe';CommandLine='app --session run-10'}; expected=$false },
        @{ name='other-process'; process=@{Name='powershell.exe';CommandLine='app --session run-1'}; expected=$false }
    )) {
        $owned = & $module { param($process) Test-VerificationProcessOwner $process 'run-1' } $case.process
        Check ($owned -eq $case.expected) "cleanup-owner-$($case.name)"
    }
    $runner = Join-Path $PSScriptRoot 'RunVerification.ps1'
    # Exercise the real runner/leaf handoff without launching an application or compiling shaders.
    $runnerRepository = Join-Path $output 'runner'
    $runnerTests = Join-Path $runnerRepository 'Tests/Verification'
    $runnerTools = Join-Path $runnerRepository 'Scripts/Verification'
    $runnerBin = Join-Path $runnerRepository 'Build/Output/x64/Develop'
    $null = New-Item -ItemType Directory -Path $runnerTests, $runnerTools, $runnerBin
    Copy-Item -LiteralPath $runner -Destination $runnerTests
    Copy-Item -LiteralPath (Join-Path $root 'Scripts/Verification/VerificationSupport.psm1') -Destination $runnerTools
    [IO.File]::WriteAllText((Join-Path $runnerBin 'GraphicsGadgetLab.exe'), '')
    $leafSource = @'
param([string]$OutputDirectory, [string]$RunDirectory, [string[]]$Configuration, [string[]]$Rhi)
Import-Module (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'Scripts/Verification/VerificationSupport.psm1')
$directory = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId 'leaf-run-id'
[Console]::Out.WriteLine((@{ ok=$true; directory=$directory; configuration=$Configuration; rhi=$Rhi } | ConvertTo-Json -Compress))
'@
    foreach ($leaf in @('TestNonFiniteCoverage.ps1', 'TestNonFiniteGenerations.ps1', 'TestNonFiniteInjection.ps1', 'TestTemporalAACounters.ps1')) {
        [IO.File]::WriteAllText((Join-Path $runnerTests $leaf), $leafSource, (New-Object Text.UTF8Encoding($false)))
    }
    $execution = Invoke-VerificationScript (Join-Path $runnerTests 'RunVerification.ps1') @{ Group='gpu'; OutputDirectory=(Join-Path $output 'runner-results') }
    $gpuResult = $execution.output | ConvertFrom-Json
    Check ($execution.exitCode -eq 0 -and $gpuResult.ok -and $gpuResult.suites.Count -eq 4) 'runner-gpu-leaf-handoff'
    foreach ($suiteResult in $gpuResult.suites) {
        $leafResult = $suiteResult.steps[0].receipt
        Check ($leafResult.directory -eq (Join-Path $suiteResult.directory 'run') -and
            $leafResult.configuration[0] -eq 'Develop' -and $leafResult.rhi.Count -eq 2) "runner-compact-$($suiteResult.name)"
    }
    $unused = Join-Path $output 'must-not-create'
    $list = Invoke-VerificationScript $runner @{ List=$true; OutputDirectory=$unused }
    $items = $list.output | ConvertFrom-Json
    Check ($list.exitCode -eq 0 -and $items.suites.Count -eq 11 -and
        @($items.suites | Where-Object name -eq 'coastal-assets').Count -eq 1 -and
        -not (Test-Path $unused) -and @($items.suites | Where-Object { $_.group -eq 'contracts' })[0].configurations.Count -eq 0) 'list-without-launch-or-output'
    foreach ($case in @(
        @{ name='unknown-suite'; options=@{Suite='unknown'}; message='Unknown suites' },
        @{ name='release-injection'; options=@{Suite='nonfinite-injection';Configuration='Release'}; message='does not support Release' },
        @{ name='group-mismatch'; options=@{Suite='nonfinite-coverage';Group='contracts'}; message='do not belong' },
        @{ name='invalid-rhi'; options=@{Rhi='invalid'}; message='Rhi must' }
    )) {
        $case.options.OutputDirectory = $unused
        $execution = Invoke-VerificationScript $runner $case.options
        $result = $execution.output | ConvertFrom-Json
        Check ($execution.exitCode -ne 0 -and -not $result.ok -and $result.error -match $case.message -and -not (Test-Path $unused)) "runner-$($case.name)"
    }
    $firstPython = Join-Path $fixtureDirectory 'first-python'
    $secondPython = Join-Path $fixtureDirectory 'second-python'
    $null = New-Item -ItemType Directory -Path $firstPython, $secondPython
    [IO.File]::WriteAllText((Join-Path $firstPython 'verification-python.cmd'), "@echo first-python`r`n@exit /b 0`r`n")
    [IO.File]::WriteAllText((Join-Path $secondPython 'verification-python.cmd'), "@exit /b 9`r`n")
    $originalPath = $env:PATH
    try {
        $env:PATH = "$firstPython;$secondPython;$originalPath"
        $execution = Invoke-VerificationScript $runner @{ Suite='resource-export-schema'; Python='verification-python.cmd'; OutputDirectory=(Join-Path $output 'multiple-python') }
        $result = $execution.output | ConvertFrom-Json
        Check ($execution.exitCode -eq 0 -and $result.ok -and (Get-Content $result.suites[0].steps[0].log -Raw) -match 'first-python') 'runner-selects-first-application'
    } finally { $env:PATH = $originalPath }
    $badPython = Join-Path $fixtureDirectory 'fail.cmd'
    [IO.File]::WriteAllText($badPython, "@exit /b 7`r`n")
    $execution = Invoke-VerificationScript $runner @{ Suite='resource-export-schema'; Python=$badPython; OutputDirectory=(Join-Path $output 'failed-run') }
    $result = $execution.output | ConvertFrom-Json
    Check ($execution.exitCode -ne 0 -and -not $result.ok -and $result.suites[0].steps[0].exitCode -eq 7 -and (Test-Path (Join-Path $result.directory 'result.json'))) 'runner-retains-failure-and-log'
    $result = @{ ok=$true; checks=$checks; directory=$output }
} catch { $result = @{ ok=$false; checks=$checks; directory=$output; error=$_.Exception.Message } }
Write-VerificationJson (Join-Path $output 'result.json') $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
if (-not $result.ok) { exit 1 }
