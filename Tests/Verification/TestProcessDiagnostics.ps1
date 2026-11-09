<# Out-of-process minidump contracts; no GPU or product process required. #>
[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory) { $OutputDirectory="$root/Build/Validation/ProcessDiagnostics" }
$output=Join-Path $OutputDirectory ([Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $output
$stopFile=Join-Path $output 'stop'
$readyFile=Join-Path $output 'ready'
$childFile=Join-Path $output 'child.ps1'
[IO.File]::WriteAllText($childFile, 'param([string]$StopFile,[string]$ReadyFile); [IO.File]::WriteAllText($ReadyFile,"ready"); while (-not [IO.File]::Exists($StopFile)) { Start-Sleep -Milliseconds 25 }')
$shell=(Get-Command powershell -CommandType Application | Select-Object -First 1).Source
$process=Start-Process -FilePath $shell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$childFile`"",'-StopFile',"`"$stopFile`"",'-ReadyFile',"`"$readyFile`"") `
    -WindowStyle Hidden -PassThru
$null=$process.Handle
$checks=@()
$dump=$null
function Check($Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks+=$Name
}
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $readyFile) -and [DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 25
    }
    Check (Test-Path -LiteralPath $readyFile) 'diagnostic-fixture-ready'
    $path=Join-Path $output 'process.dmp'
    $dump=Save-VerificationProcessDump $process $path
    Check ($dump.status -eq 'written' -and $dump.processId -eq $process.Id -and $dump.bytes -gt 32) 'owned-process-dump-written'
    $bytes=[IO.File]::ReadAllBytes($path)
    Check ([Text.Encoding]::ASCII.GetString($bytes,0,4) -ceq 'MDMP') 'native-minidump-signature'
    $hash=(Get-FileHash -LiteralPath $path).Hash
    $again=Save-VerificationProcessDump $process $path
    Check ($again.status -eq 'already-exists' -and (Get-FileHash -LiteralPath $path).Hash -ceq $hash) 'existing-evidence-not-overwritten'
    [IO.File]::WriteAllText($stopFile,'stop')
    Check ($process.WaitForExit(5000) -and $process.ExitCode -eq 0) 'diagnosis-preserves-normal-exit'
    $exitedPath=Join-Path $output 'exited.dmp'
    $exited=Save-VerificationProcessDump $process $exitedPath
    Check ($exited.status -eq 'process-exited' -and -not (Test-Path -LiteralPath $exitedPath)) 'exited-process-not-reopened-by-pid'
    & {
        # Execute the real startup cleanup block with an unresponsive owned host.
        $tokens=$null; $errors=$null
        $ast=[Management.Automation.Language.Parser]::ParseFile(
            "$PSScriptRoot/TestStartupReadiness.ps1",[ref]$tokens,[ref]$errors)
        $node=$ast.Find({param($n) $n -is [Management.Automation.Language.TryStatementAst] -and
            $n.Finally -and $n.Finally.Extent.Text.Contains('$cases+=$case')},$true)
        $text=$node.Finally.Extent.Text.Trim()
        $cleanup=[scriptblock]::Create($text.Substring(1,$text.Length-2))
        function Invoke-VerificationSession { return [pscustomobject]@{ok=$false} }
        function Save-VerificationProcessDump { return @{status='written'} }
        function Copy-Item {}
        foreach ($scenario in @('shutdown-failed','startup-and-shutdown-failed','forced-cleanup-incomplete')) {
            $directory=Join-Path $output $scenario
            $null=New-Item -ItemType Directory -Path $directory
            $session='fixture'; $start=@{ownership='created';runIdentity=('a'*32);outputLog='unused'}
            $case=@{normalExit=$false}; $cases=@()
            $failure=if ($scenario -eq 'startup-and-shutdown-failed') {'startup-failure'} else {$null}
            $process=[pscustomobject]@{HasExited=$false;disposed=$false;waitCompletes=($scenario -ne 'forced-cleanup-incomplete')}
            $process | Add-Member ScriptMethod Kill { $this.HasExited=$true }
            $process | Add-Member ScriptMethod WaitForExit { param($timeout) return $this.waitCompletes }
            $process | Add-Member ScriptMethod Dispose { $this.disposed=$true }
            $threw=$false
            try { . $cleanup } catch { $threw=$true }
            $saved=@(Get-Content "$output/cases.json" -Raw | ConvertFrom-Json)
            Check ($process.disposed -and $cases.Count -eq 1 -and $saved.Count -eq 1 -and
                $saved[0].forcedCleanup -and -not $saved[0].normalExit -and $saved[0].stopError -and
                $saved[0].cleanupCompleted -eq $process.waitCompletes -and $threw -eq (-not [bool]$failure)) `
                "startup-cleanup-retains-$scenario"
        }
    }
    $result=@{ok=$true;checks=$checks;dump=$dump}
} catch { $result=@{ok=$false;error=$_.Exception.Message;checks=$checks;dump=$dump} }
finally {
    [IO.File]::WriteAllText($stopFile,'stop')
    if (-not $process.WaitForExit(5000)) { $process.Kill(); $null=$process.WaitForExit(5000) }
    $process.Dispose()
}
Write-VerificationJson (Join-Path $output 'result.json') $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Depth 5 -Compress))
if (-not $result.ok) { exit 1 }
