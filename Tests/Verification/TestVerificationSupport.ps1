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
    $binding = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') @{
        Command='result';Session='invalid-job-binding';JobId=0 }
    $bindingResult = $binding.output | ConvertFrom-Json
    Check ($binding.exitCode -eq 1 -and -not $bindingResult.ok -and $bindingResult.error -match 'positive -JobId') 'real-session-client-binds-job-parameters-without-gpu'
    # Transport failures remain one JSON control result, so audit cleanup can act on them.
    $missingSession = 'verification-missing-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
    $missingStop = Stop-VerificationSession $missingSession -AllowFailure
    Check (-not $missingStop.ok -and $missingStop.session -eq $missingSession -and
        $missingStop.error -match 'not running') 'real-session-client-reports-missing-session-as-json'
    # Exercise the real waiting client with deterministic transport outcomes.
    & {
        $tokens = $null; $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $root 'Scripts/Verification/GGLabSession.ps1'), [ref]$tokens, [ref]$parseErrors)
        foreach ($name in @('Wait-CaptureJob','ConvertTo-Hashtable')) {
            $definition = $ast.Find({ param($node)
                $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($definition.Extent.Text))
        }
        $Session = 'client-contract'; $ProtocolVersion = 4; $KeepResult = $false
        $state = @{ calls=0; timeout=0; fail=$true; status='completed' }
        function Invoke-SessionRequest([string]$Id, [hashtable]$Request, [int]$ResponseTimeoutSeconds) {
            $state.calls++; $state.timeout = $ResponseTimeoutSeconds
            if ($state.fail) { throw 'lost transport response' }
            return [pscustomobject]@{ protocol=4;ok=$true;jobId=$Request.jobId;status=$state.status
                consumed=($Request.consume -and $state.status -ne 'pending') }
        }
        $admission = [pscustomobject]@{ok=$true;jobId=17}
        $lost = Wait-CaptureJob $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check (-not $lost.ok -and $lost.jobId -eq 17 -and $lost.error -match 'lost transport' -and
            -not ($lost.PSObject.Properties.Name -contains 'status') -and $state.calls -eq 1 -and
            $state.timeout -gt 30) 'client-query-failure-retains-id-without-inventing-outcome-or-retrying'
        $state.fail = $false; $KeepResult = $true
        $borrowed = Wait-CaptureJob $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check ($borrowed.ok -and -not $borrowed.consumed) 'client-keep-result-borrows-terminal-receipt'
        $KeepResult = $false
        $consumed = Wait-CaptureJob $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check ($consumed.ok -and $consumed.consumed) 'client-default-explicitly-consumes-terminal-receipt'
        $state.status = 'pending'
        $pending = Wait-CaptureJob $admission ([DateTime]::UtcNow.AddSeconds(-1))
        Check (-not $pending.ok -and $pending.status -eq 'pending' -and $pending.jobId -eq 17 -and
            -not $pending.consumed -and $state.calls -eq 4) 'client-deadline-retains-pending-job-without-cancellation'
    }
    # Exercise the actual parameter wait loop across failures and lifecycle boundaries.
    & {
        $tokens = $null; $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $root 'Scripts/Verification/GGLabSession.ps1'), [ref]$tokens, [ref]$parseErrors)
        foreach ($name in @('Wait-ParameterBatch','ConvertTo-Hashtable')) {
            $definition = $ast.Find({ param($node)
                $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
            }, $true)
            . ([scriptblock]::Create($definition.Extent.Text))
        }
        $Session = 'parameter-contract'; $ProtocolVersion = 4; $KeepResult = $false; $WaitConsumed = $false
        $state = @{ calls=0; fail=$true; terminal=$false; revision=$null; items=@(); consumed=$false }
        function Invoke-SessionRequest([string]$Id, [hashtable]$Request, [int]$ResponseTimeoutSeconds) {
            $state.calls++
            if ($state.fail) { throw 'lost parameter query' }
            $state.consumed = $Request.consume -and $state.terminal
            return [pscustomobject]@{ protocol=4;ok=$true;batchId=$Request.batchId;consumed=$state.consumed
                batch=[pscustomobject]@{ terminal=$state.terminal;revision=$state.revision;items=$state.items } }
        }
        $admission = [pscustomobject]@{ok=$true;batchId=23}
        $lost = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check (-not $lost.ok -and $lost.batchId -eq 23 -and $state.calls -eq 1 -and
            $null -eq $lost.PSObject.Properties['batch']) 'parameter-query-loss-retains-identity-without-resubmission'
        $state.fail=$false
        $pending = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(-1))
        Check (-not $pending.ok -and $pending.batchId -eq 23 -and -not $pending.consumed -and
            $state.calls -eq 2) 'parameter-deadline-keeps-queued-ownership'
        $state.revision=8; $state.items=@([pscustomobject]@{applied=20;state='Published'})
        $applied = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check ($applied.ok -and -not $applied.batch.terminal -and -not $applied.consumed) 'parameter-default-waits-for-application-without-consuming-pending-effect'
        $WaitConsumed=$true
        $waiting = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(-1))
        Check (-not $waiting.ok -and -not $waiting.consumed) 'parameter-wait-consumed-does-not-stop-at-publication'
        $state.terminal=$true; $KeepResult=$true
        $kept = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check ($kept.ok -and -not $kept.consumed) 'parameter-keep-result-preserves-terminal-slot'
        $KeepResult=$false
        $taken = Wait-ParameterBatch $admission ([DateTime]::UtcNow.AddSeconds(90))
        Check ($taken.ok -and $taken.consumed -and $state.calls -eq 6) 'parameter-terminal-wait-explicitly-releases-slot'
    }
    # The real client binds a Host view operation's expected run to its whole wait. The fake
    # session admits the operation, then answers polling as a same-name restart whose new
    # process reuses the batch id, so an unguarded query would consume another run's result.
    & {
        $session = 'override-wait-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
        $run = '0123456789abcdef0123456789abcdef'
        $server = Start-Job -ArgumentList "gglab-session-$session" -ScriptBlock {
            param([string]$PipeName)
            $encoding = New-Object Text.UTF8Encoding($false)
            $requests = @()
            for ($index = 0; $index -lt 2; ++$index) {
                $pipe = New-Object IO.Pipes.NamedPipeServerStream($PipeName, [IO.Pipes.PipeDirection]::InOut)
                try {
                    if (-not $pipe.WaitForConnectionAsync().Wait(30000)) { break }
                    $reader = New-Object IO.StreamReader($pipe, $encoding, $false, 4096, $true)
                    $request = $reader.ReadLine() | ConvertFrom-Json
                    $requests += $request
                    $response = if ($request.command -eq 'host-view-overrides') {
                        @{ protocol=4; id=$request.id; ok=$true; admission='Accepted'; batchId=1; consumed=$false
                            batch=@{ batchId=1; terminal=$false; revision=$null; items=@() } }
                    } elseif ($request.PSObject.Properties['expectedRunIdentity']) {
                        @{ protocol=4; id=$request.id; ok=$false; errorCode='ExpectedRunChanged'
                            runIdentity='ffffffffffffffffffffffffffffffff' }
                    } else {
                        @{ protocol=4; id=$request.id; ok=$true; batchId=1; consumed=$request.consume
                            batch=@{ batchId=1; terminal=$true; revision=9; items=@() } }
                    }
                    $writer = New-Object IO.StreamWriter($pipe, $encoding, 4096, $true)
                    $writer.NewLine = "`n"
                    $writer.WriteLine(($response | ConvertTo-Json -Compress -Depth 8))
                    $writer.Flush()
                }
                finally { $pipe.Dispose() }
            }
            ConvertTo-Json -InputObject @($requests) -Compress -Depth 8
        }
        try {
            $listening = Wait-VerificationCondition -TimeoutSeconds 30 -Poll {
                [bool]([IO.Directory]::GetFiles('\\.\pipe\') | Where-Object { $_ -ieq "\\.\pipe\gglab-session-$session" })
            } -Until { param($ready) $ready }
            $client = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') @{
                Command='host-view-overrides'; Session=$session; TimeoutSeconds=30
                ExpectedState="{`"runIdentity`":`"$run`",`"contentIdentity`":4,`"hostViewRevision`":2}"
                Overrides='[{"id":"view.taa.enabled","type":"bool","present":false}]' }
            $result = $client.output | ConvertFrom-Json
            $requests = ConvertFrom-Json -InputObject ([string](Receive-Job $server -Wait -AutoRemoveJob))
            Check ($listening -and $client.exitCode -eq 1 -and -not $result.ok -and $result.errorCode -eq 'ExpectedRunChanged' -and
                $requests.Count -eq 2 -and -not $requests[0].PSObject.Properties['expectedRunIdentity'] -and
                $requests[0].expected.runIdentity -eq $run -and $requests[1].command -eq 'parameter-result' -and
                $requests[1].expectedRunIdentity -eq $run) 'host-view-override-polling-is-bound-to-the-expected-run'
        }
        finally { Get-Job -Id $server.Id -ErrorAction SilentlyContinue | Remove-Job -Force }
    }
    # The client adds an expected run only to commands the session checks.
    & {
        $tokens = $null; $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $root 'Scripts/Verification/GGLabSession.ps1'), [ref]$tokens, [ref]$parseErrors)
        $definition = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Add-ExpectedRunIdentity'
        }, $true)
        . ([scriptblock]::Create($definition.Extent.Text))
        $commands = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.AssignmentStatementAst] -and
                $node.Left.Extent.Text -eq '$RunCheckedCommands'
        }, $true)
        . ([scriptblock]::Create($commands.Extent.Text))
        $ExpectedRunIdentity = '0123456789abcdef0123456789abcdef'
        $set = @{ command='set' }; Add-ExpectedRunIdentity $set
        $status = @{ command='status' }; Add-ExpectedRunIdentity $status
        # The guarded override operation names its run in the expected control state.
        $guarded = @{ command='host-view-overrides' }; Add-ExpectedRunIdentity $guarded
        $ExpectedRunIdentity = ''
        $unchecked = @{ command='stop' }; Add-ExpectedRunIdentity $unchecked
        Check ($set.expectedRunIdentity -eq '0123456789abcdef0123456789abcdef' -and
            -not $status.ContainsKey('expectedRunIdentity') -and -not $guarded.ContainsKey('expectedRunIdentity') -and
            -not $unchecked.ContainsKey('expectedRunIdentity') -and
            $RunCheckedCommands.Count -eq 11) 'client-expected-run-identity-only-on-protected-commands'
    }
    $invalidParameter = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') @{
        Command='set';Session='parameter-invalid';Changes='[]';NoWait=$true;WaitConsumed=$true }
    $invalidResult = $invalidParameter.output | ConvertFrom-Json
    Check ($invalidParameter.exitCode -eq 1 -and -not $invalidResult.ok) 'parameter-incompatible-wait-flags-return-json'
    $missingChanges = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') @{
        Command='set';Session='parameter-invalid';ChangesFile=(Join-Path $output 'missing-changes.json') }
    $missingChangesResult = $missingChanges.output | ConvertFrom-Json
    Check ($missingChanges.exitCode -eq 1 -and -not $missingChangesResult.ok) 'parameter-missing-input-file-returns-json'
    $missingState = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') @{
        Command='host-view-overrides';Session='parameter-invalid'
        Overrides='[{"id":"view.taa.enabled","type":"bool","present":false}]' }
    $missingStateResult = $missingState.output | ConvertFrom-Json
    Check ($missingState.exitCode -eq 1 -and -not $missingStateResult.ok -and
        $missingStateResult.error -match 'ExpectedState') 'host-view-overrides-require-expected-state'
    $stamp = '{"runIdentity":"0123456789abcdef0123456789abcdef","contentIdentity":4,"hostViewRevision":2}'
    $release = '[{"id":"view.taa.enabled","type":"bool","present":false}]'
    foreach ($case in @(
        @{ name='host-view-overrides-missing-overrides-file-returns-json'; pattern='missing-overrides'
            options=@{ OverridesFile=(Join-Path $output 'missing-overrides.json'); ExpectedState=$stamp } },
        @{ name='host-view-overrides-missing-expected-state-file-returns-json'; pattern='missing-state'
            options=@{ Overrides=$release; ExpectedStateFile=(Join-Path $output 'missing-state.json') } },
        @{ name='host-view-overrides-reject-malformed-expected-run'; pattern='runIdentity'
            options=@{ Overrides=$release; ExpectedState=$stamp.ToUpperInvariant() } },
        @{ name='host-view-overrides-reject-a-conflicting-expected-run'; pattern='differs'
            options=@{ Overrides=$release; ExpectedState=$stamp; ExpectedRunIdentity='ffffffffffffffffffffffffffffffff' } })) {
        $arguments = @{ Command='host-view-overrides'; Session='parameter-invalid' }
        foreach ($key in $case.options.Keys) { $arguments[$key] = $case.options[$key] }
        $invalid = Invoke-VerificationScript (Join-Path $root 'Scripts/Verification/GGLabSession.ps1') $arguments
        # An uncaught script error leaves stdout empty, which parses as null.
        $invalidResult = $invalid.output | ConvertFrom-Json
        Check ($invalid.exitCode -eq 1 -and $null -ne $invalidResult -and -not $invalidResult.ok -and
            $invalidResult.error -match $case.pattern) $case.name
    }
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
    $capture = [pscustomobject]@{status='completed';publication='Published';manifest='bundle.json';products=@(
        [pscustomobject]@{index=0;files=@('a.png','a.json')}, [pscustomobject]@{index=1;files=@('b.graph.json')})}
    Check ((Get-VerificationCaptureFile $capture '.png') -eq 'a.png' -and
        (Get-VerificationCaptureFile $capture '.json' 1) -eq 'b.graph.json') 'capture-files-are-selected-by-product'
    Reject { Get-VerificationCaptureFile $capture '.npy' } 'Expected one' 'missing-capture-file'
    $capture.publication = 'Indeterminate'
    Reject { Get-VerificationCaptureFile $capture '.png' } 'published bundle' 'partial-files-are-not-completed-capture'
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
    # Keep catalog expectations and leaf fixtures together so their counts cannot drift.
    $gpuLeaves = [ordered]@{
        'lighting-profiles' = 'TestLightingProfiles.ps1'
        'sequence-parameter-events' = 'TestSequenceParameterEvents.ps1'
        'sequence-deferred-events' = 'TestDeferredSequenceEvents.ps1'
        'lab-sun-parameters' = 'TestLabSunParameters.ps1'
        'session-rebuild-parameters' = 'TestRebuildParameters.ps1'
        'session-host-parameters' = 'TestHostParameters.ps1'
        'session-parameters' = 'TestSessionParameters.ps1'
        'capture-jobs' = 'TestCaptureJobs.ps1'
        'capture-on-ready' = 'TestCaptureOnReady.ps1'
        'pass-profile-identity' = 'TestPassProfileIdentity.ps1'
        'graph-allocation-facts' = 'TestGraphAllocationFacts.ps1'
        'nonfinite-coverage' = 'TestNonFiniteCoverage.ps1'
        'nonfinite-generations' = 'TestNonFiniteGenerations.ps1'
        'nonfinite-injection' = 'TestNonFiniteInjection.ps1'
        'temporal-aa-counters' = 'TestTemporalAACounters.ps1'
    }
    $contractSuites = @(
        'coastal-assets', 'verification-support', 'nonfinite-sites', 'render-graph-schema',
        'resource-export-schema', 'nonfinite-coverage-schema', 'capture-comparison',
        'capture-bundle-schema', 'gpu-timing-schema', 'parameter-frames-schema'
    )
    foreach ($leaf in $gpuLeaves.Values) {
        [IO.File]::WriteAllText((Join-Path $runnerTests $leaf), $leafSource, (New-Object Text.UTF8Encoding($false)))
    }
    $execution = Invoke-VerificationScript (Join-Path $runnerTests 'RunVerification.ps1') @{ Group='gpu'; OutputDirectory=(Join-Path $output 'runner-results') }
    $gpuResult = $execution.output | ConvertFrom-Json
    Check ($execution.exitCode -eq 0 -and $gpuResult.ok -and
        $gpuResult.suites.Count -eq $gpuLeaves.Count -and
        @(Compare-Object @($gpuLeaves.Keys) @($gpuResult.suites.name)).Count -eq 0) 'runner-gpu-leaf-handoff'
    foreach ($suiteResult in $gpuResult.suites) {
        $leafResult = $suiteResult.steps[0].receipt
        Check ($leafResult.directory -eq (Join-Path $suiteResult.directory 'run') -and
            $leafResult.configuration[0] -eq 'Develop' -and $leafResult.rhi.Count -eq 2) "runner-compact-$($suiteResult.name)"
    }
    $unused = Join-Path $output 'must-not-create'
    $list = Invoke-VerificationScript $runner @{ List=$true; OutputDirectory=$unused }
    $items = $list.output | ConvertFrom-Json
    $listedGpu = @($items.suites | Where-Object { $_.group -eq 'gpu' })
    $listedContracts = @($items.suites | Where-Object { $_.group -eq 'contracts' })
    Check ($list.exitCode -eq 0 -and $items.ok -and
        $items.suites.Count -eq ($gpuLeaves.Count + $contractSuites.Count) -and
        $listedGpu.Count -eq $gpuLeaves.Count -and $listedContracts.Count -eq $contractSuites.Count -and
        @(Compare-Object @($gpuLeaves.Keys) @($listedGpu.name)).Count -eq 0 -and
        @(Compare-Object $contractSuites @($listedContracts.name)).Count -eq 0 -and
        -not (Test-Path $unused) -and
        @($listedContracts | Where-Object { $_.configurations.Count -ne 0 }).Count -eq 0) 'list-without-launch-or-output'
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
