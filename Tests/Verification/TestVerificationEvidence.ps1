<# Exercises the real evidence producers with isolated inputs and a Python substitute, without a GPU. #>
[CmdletBinding()]
param([string]$OutputDirectory, [string]$RunDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/VerificationEvidence" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$fixture = Join-Path $output 'repository with spaces'
$checks = @()
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "Failed: $Name" }
    $script:checks += $Name
}
try {
    foreach ($path in @('Sources','Scripts/Verification','Tests/Verification','Build/Fixtures/GIVisibility',
        'Build/Output/x64/Develop','Build/Output/x64/Release')) {
        $null = New-Item -ItemType Directory -Path "$fixture/$path" -Force
    }
    $support = "$fixture/Scripts/Verification/VerificationSupport.psm1"
    Copy-Item -LiteralPath "$root/Scripts/Verification/VerificationSupport.psm1" -Destination $support
    $gitStub = @'
function git {
    $fixture = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $mode = [IO.File]::ReadAllText("$fixture/mode.txt")
    Write-VerificationJson "$fixture/git-arguments.json" @($args)
    $global:LASTEXITCODE = if ($mode -eq 'enumeration-failure') { 7 } else { 0 }
    foreach ($name in @('Sources/input.cpp','Sources/Z-input.cpp','Sources/added.cpp')) {
        if (Test-Path -LiteralPath "$fixture/$name") { $name }
    }
}
function Invoke-VerificationSession { throw 'Evidence-only fixtures must not start a native session.' }
Export-ModuleMember -Function git, Invoke-VerificationSession
'@
    Add-Content -LiteralPath $support -Value $gitStub -Encoding UTF8
    Write-VerificationJson "$fixture/Build/Fixtures/GIVisibility/manifest.json" @{fixture=$true}
    $python = "$fixture/evidence python.ps1"
    $pythonStub = @'
param([string]$Script)
$arguments = @($args)
$fixture = $PSScriptRoot
$mode = [IO.File]::ReadAllText("$fixture/mode.txt")
$producer = [IO.File]::ReadAllText("$fixture/producer.txt")
$name = [IO.Path]::GetFileName($Script)
$call = @{script=$name;arguments=$arguments} | ConvertTo-Json -Compress
[IO.File]::AppendAllText("$fixture/python-calls.jsonl", $call + [Environment]::NewLine)
$oracle = switch ($producer) {
    'receiver' { 'ValidateGIReceiverCache.py' }
    'visibility' { 'ValidateGISpecularVisibility.py' }
    'cost' { 'ValidateGIProbeCost.py' }
}
$isOracle = $name -eq $oracle -and $arguments -notcontains '--generate'
if ($isOracle) {
    switch ($mode) {
        'source-mutation' { [IO.File]::WriteAllText("$fixture/Sources/input.cpp", 'modified') }
        'source-addition' { [IO.File]::WriteAllText("$fixture/Sources/added.cpp", 'added') }
        'source-removal' { Remove-Item -LiteralPath "$fixture/Sources/Z-input.cpp" }
        'compiler-mutation' {
            $config = if ($producer -eq 'cost') { 'Release' } else { 'Develop' }
            [IO.File]::WriteAllText("$fixture/Build/Output/x64/$config/gglab-shaderc.exe", 'modified')
        }
        'parameter-count' { [IO.File]::WriteAllText("$($arguments[0])/unexpected.parameter-frames.json", '{}') }
    }
}
$global:LASTEXITCODE = if (($mode -eq 'oracle-failure' -and $isOracle) -or $mode -eq $name) { 7 } else { 0 }
$name
'@
    [IO.File]::WriteAllText($python, $pythonStub, (New-Object Text.UTF8Encoding($false)))
    $producers = @(
        @{name='receiver';file='TestGIReceiverCache.ps1';oracle='ValidateGIReceiverCache.py';
            scopes=@('Sources','Shaders','Projects','PropertySheets','Tests/Verification','Scripts/Verification');
            audits=@('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')},
        @{name='visibility';file='TestGISpecularVisibility.ps1';oracle='ValidateGISpecularVisibility.py';
            scopes=@('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets');
            audits=@('ValidateCaptureBundles.py','ValidateResourceExports.py','ValidateRenderGraphExports.py')},
        @{name='cost';file='MeasureGIProbeUpdate.ps1';oracle='ValidateGIProbeCost.py';
            scopes=@('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets');
            audits=@('ValidateCaptureBundles.py','ValidateParameterFrames.py','ValidateGpuTiming.py')}
    )
    foreach ($producer in $producers) {
        Copy-Item -LiteralPath "$PSScriptRoot/$($producer.file)" -Destination "$fixture/Tests/Verification"
        [IO.File]::WriteAllText("$fixture/producer.txt", $producer.name)
        $modes = @('passed','source-mutation','source-addition','source-removal','compiler-mutation',
            'enumeration-failure','oracle-failure') + $producer.audits
        if ($producer.name -eq 'cost') { $modes += 'parameter-count' }
        foreach ($mode in $modes) {
            $case = "$($producer.name)-$mode"
            $directory = "$output/$case"
            $auditDirectory = if ($producer.name -eq 'visibility') { "$directory/Develop" } else { $directory }
            [IO.File]::WriteAllText("$fixture/mode.txt", $mode)
            [IO.File]::WriteAllText("$fixture/python-calls.jsonl", '')
            foreach ($name in @('input.cpp','Z-input.cpp')) {
                [IO.File]::WriteAllText("$fixture/Sources/$name", 'original')
            }
            if (Test-Path "$fixture/Sources/added.cpp") { Remove-Item -LiteralPath "$fixture/Sources/added.cpp" }
            foreach ($config in @('Develop','Release')) {
                foreach ($name in @('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll')) {
                    [IO.File]::WriteAllText("$fixture/Build/Output/x64/$config/$name", 'binary')
                }
            }
            # An empty backend list exercises the complete evidence path without substituting its helpers.
            $parameters = @{RunDirectory=$directory;Python=$python;Rhi=@()}
            if ($producer.name -eq 'visibility') { $parameters.SkipCoastal=$true; $parameters.Smoke=$true }
            $execution = Invoke-VerificationScript "$fixture/Tests/Verification/$($producer.file)" $parameters
            $disk = Get-Content "$directory/result.json" -Raw | ConvertFrom-Json
            $passed = if ($producer.name -eq 'receiver') { $disk.ok } else { $disk.status -eq 'passed' }
            if ($mode -eq 'passed') {
                Check ($execution.exitCode -eq 0 -and $passed) "$case-result"
                $isCost = $producer.name -eq 'cost'
                $identityFile = if ($isCost) { 'identity.json' } else { 'source-identity.json' }
                $source = Get-Content "$directory/$identityFile" -Raw | ConvertFrom-Json
                $expectedPaths = @('Sources/Z-input.cpp','Sources/input.cpp')
                if ($isCost) {
                    $expectedPaths = @('Build/Output/x64/Release/GraphicsGadgetLab.exe',
                        'Build/Output/x64/Release/dxcompiler.dll','Build/Output/x64/Release/dxil.dll',
                        'Build/Output/x64/Release/gglab-shaderc.exe') + $expectedPaths
                }
                Assert-VerificationIdentity $expectedPaths @($source.path) "$case path order changed"
                $pattern = if ($producer.name -eq 'visibility') { '^[0-9a-f]{64}$' } else { '^[0-9A-F]{64}$' }
                foreach ($entry in $source) {
                    Check (($entry.PSObject.Properties.Name -join ',') -ceq 'path,sha256' -and
                        $entry.sha256 -cmatch $pattern) "$case-source-shape-and-case"
                }
                $gitArguments = Get-Content "$fixture/git-arguments.json" -Raw | ConvertFrom-Json
                $expected = @('-c',"safe.directory=$($fixture.Replace('\','/'))",'-C',$fixture,
                    'ls-files','--cached','--others','--exclude-standard') + $producer.scopes
                Assert-VerificationIdentity $expected $gitArguments "$case Git scope changed"
                Check $true "$case-source-scope"
                if (-not $isCost) {
                    $binaryFile = if ($producer.name -eq 'visibility') { 'Develop/binaries.json' } else { 'binaries.json' }
                    $binaries = Get-Content "$directory/$binaryFile" -Raw | ConvertFrom-Json
                    Assert-VerificationIdentity @('GraphicsGadgetLab.exe','gglab-shaderc.exe','dxcompiler.dll','dxil.dll') @($binaries.file) "$case binary order changed"
                    foreach ($entry in $binaries) {
                        Check (($entry.PSObject.Properties.Name -join ',') -ceq 'file,sha256' -and
                            $entry.sha256 -cmatch $pattern) "$case-binary-shape-and-case"
                    }
                } else {
                    Check ($execution.output.Trim() -ceq $producer.oracle -and -not $disk.productionFieldQualified -and
                        $disk.observations -eq 0) "$case-stdout-and-qualification-scope"
                }
                foreach ($validator in $producer.audits) {
                    if ($isCost) {
                        Check ((Get-Content "$directory/$validator.log" -Raw).Trim() -ceq $validator -and
                            -not (Test-Path "$directory/$validator.result.json")) "$case-$validator-log"
                    } else {
                        $receipt = Get-Content "$auditDirectory/$validator.result.json" -Raw | ConvertFrom-Json
                        Check ($receipt.exitCode -eq 0 -and $receipt.output -ceq $validator -and
                            @($receipt.PSObject.Properties).Count -eq 2) "$case-$validator-receipt"
                    }
                }
            } else {
                Check ($execution.exitCode -ne 0 -and -not $passed -and $disk.error) "$case-rejected"
                $pattern = switch ($mode) {
                    { $_ -like 'source-*' -or $_ -eq 'compiler-mutation' } { 'changed' }
                    'enumeration-failure' { 'Source enumeration failed' }
                    'parameter-count' { 'Missing parameter-frame audit inputs' }
                    default { 'verification failed|validation failed|qualification failed|artifact audit failed' }
                }
                Check ($disk.error -match $pattern) "$case-failure-reason"
                if ($mode -in $producer.audits) {
                    if ($producer.name -eq 'cost') {
                        Check ((Get-Content "$directory/$mode.log" -Raw).Trim() -ceq $mode) "$case-failure-log"
                    } else {
                        $receipt = Get-Content "$auditDirectory/$mode.result.json" -Raw | ConvertFrom-Json
                        Check ($receipt.exitCode -eq 7 -and $receipt.output -ceq $mode) "$case-failure-receipt"
                    }
                    $index = [Array]::IndexOf($producer.audits, $mode)
                    foreach ($later in @($producer.audits | Select-Object -Skip ($index + 1))) {
                        Check (-not (Test-Path "$auditDirectory/$later.log") -and
                            -not (Test-Path "$auditDirectory/$later.result.json")) "$case-stops-before-$later"
                    }
                }
                if ($mode -eq 'parameter-count') {
                    Check ($disk.error -match 'Missing parameter-frame audit inputs' -and
                        -not (Test-Path "$directory/ValidateParameterFrames.py.log")) "$case-preserves-file-count-guard"
                }
            }
        }
    }
    $result = @{ok=$true;directory=$output;checks=$checks}
} catch { $result = @{ok=$false;directory=$output;checks=$checks;error=$_.Exception.Message} }
Write-VerificationJson "$output/result.json" $result
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress -Depth 6))
if (-not $result.ok) { exit 1 }
