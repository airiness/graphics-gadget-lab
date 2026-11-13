<# Lab recipe admission, safe rebuilds and frozen profile parity; no GI quality verdict. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$ShaderCacheDirectory,
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
Import-Module "$PSScriptRoot/GIFieldExperimentRecipes.psm1"
$catalog = Get-Content "$PSScriptRoot/GIFieldExperimentRecipes.json" -Raw | ConvertFrom-Json
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or $Configuration[0] -ne 'Develop' -or -not $Rhi.Count -or
    @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) { throw 'Recipe GPU contracts require Develop and valid backends.' }
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIFieldExperimentRecipes" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory `
    -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
$sourcePaths = @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects','PropertySheets')
$cases = @()
try {
    $source = Get-VerificationSourceIdentity $root $sourcePaths
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    foreach ($backend in $Rhi) {
        $directory = "$output/$backend"
        $state = "$directory/state"
        $null = New-Item -ItemType Directory -Path $state -Force
        if ($ShaderCacheDirectory) {
            foreach ($name in @('ShaderCache','ShaderArtifacts')) {
                Copy-Item -LiteralPath "$ShaderCacheDirectory/$backend/$name" -Destination "$state/$name" -Recurse
            }
        }
        $session = 'gi-recipe-' + [Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot=$state;StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$directory/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Recipe validation requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            $parameters = Invoke-VerificationSession parameters $session @{ExpectedRunIdentity=$run}
            Write-VerificationJson "$directory/parameters.json" $parameters
            $descriptor = @($parameters.parameters | Where-Object id -CEQ $catalog.parameter)
            if ($descriptor.Count -ne 1 -or $descriptor[0].type -cne 'enum' -or $descriptor[0].default -ne 0 -or
                $descriptor[0].impact -cne 'RestartSession' -or $descriptor[0].enumItems.Count -ne 8 -or
                @($parameters.parameters | Where-Object source -CEQ 'lab').Count -ne 44) { throw 'Unexpected Lab recipe descriptor.' }
            foreach ($recipe in $catalog.recipes) {
                $item = @($descriptor[0].enumItems | Where-Object value -EQ $recipe.value)
                if ($item.Count -ne 1 -or $item[0].name -cne $recipe.name) { throw 'Lab and script recipe catalogs disagree.' }
            }
            $invalid = @(@{id=$catalog.parameter;type='enum';value=8})
            $rejected = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;
                Changes=(ConvertTo-Json -InputObject $invalid -Compress)} -AllowFailure
            if ($rejected.ok) { throw 'An unknown recipe value was admitted.' }
            foreach ($retired in @('field_progressive_refresh','field_rapid_hard_reset','field_progressive_sampling')) {
                if ("gi_reference.$retired" -in $parameters.parameters.id) { throw 'A retired Lab switch is still registered.' }
                $invalid = @(@{id=$catalog.parameter;type='enum';value=1},
                    @{id="gi_reference.$retired";type='bool';value=$true})
                $rejected = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;
                    Changes=(ConvertTo-Json -InputObject $invalid -Compress)} -AllowFailure
                $after = Invoke-VerificationSession parameters $session @{ExpectedRunIdentity=$run}
                if ($rejected.ok -or $after.revision -ne $parameters.revision -or
                    @($after.parameters | Where-Object id -CEQ $catalog.parameter)[0].current -ne 0) {
                    throw 'Retired inputs did not reject atomically.'
                }
            }
            foreach ($recipe in $catalog.recipes) {
                # Leave room for atomic-publication staging names on Windows.
                $caseDirectory = "$directory/recipe-$($recipe.value)"
                $null = New-Item -ItemType Directory -Path $caseDirectory -Force
                $changes = @(@{id=$catalog.parameter;type='enum';value=[int]$recipe.value},
                    @{id='gi_reference.fixture';type='enum';value=4},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$true},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.probe_enabled';type='bool';value=$true})
                $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
                if (-not $effect.batch.succeeded) { throw 'Recipe effect was not consumed.' }
                Write-VerificationJson "$caseDirectory/effect.json" $effect
                $status = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                    -Until { param($r) $f=$r.frame.giField; $f -and $f.completedUpdates -ge 4 -and $f.summary.profile } `
                    -TimeoutSeconds $TimeoutSeconds
                Write-VerificationJson "$caseDirectory/status.json" $status
                $selected = Get-GIFieldExperimentRecipe $recipe.id
                $profile = $status.frame.giField.summary.profile
                $hard = if ($selected.rapidHardReset) { 'rapid' } else { 'progressive' }
                $lighting = if ($selected.progressiveRefresh) { 'progressive' } else { 'rapid' }
                if ($profile.readiness.hardReset.integration -cne $hard -or
                    $profile.readiness.lighting.integration -cne $lighting -or
                    $profile.progressiveSampling -ne $selected.progressiveSampling -or
                    $status.frame.giField.activeBlocks -le 0 -or $status.frame.giField.failed) {
                    throw 'Recipe changed its historical integration or sampling policy.'
                }
                $products = @(@{kind='image';source='scene'},@{kind='graph'},
                    @{kind='resource';targets=@(@{resource='GI.Field.Readiness'},@{resource='GI.Field.Classification'})})
                # Observe active probes without claiming a calibrated readiness or quality gate.
                $capture = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;
                    View='F4_Floor';Timing='next-frame';SettleFrames=4;
                    Products=(ConvertTo-Json -InputObject $products -Depth 6 -Compress);
                    OutputDirectory="$caseDirectory/capture";TimeoutSeconds=$TimeoutSeconds;Label=$recipe.id}
                Write-VerificationJson "$caseDirectory/capture.json" $capture
                if ($capture.status -ne 'completed' -or $capture.publication -ne 'Published') { throw 'Recipe capture did not publish.' }
                $frozen = @($capture.products[0].metadata.parameters.values | Where-Object id -CEQ $catalog.parameter)
                # Frozen parameter snapshots omit default-valued assignments.
                $frozenValue = if ($frozen.Count) { $frozen[0].value } else { $descriptor[0].default }
                if ($frozen.Count -gt 1 -or $frozenValue -ne $recipe.value) { throw 'Capture lost its applied recipe identity.' }
                $cases += @{backend=$backend;recipe=$recipe.id;value=$recipe.value;status='passed'}
                [Console]::Error.WriteLine("$backend $($recipe.id) passed")
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$directory/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$directory/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Recipe session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Recipe native validation failed.' }
            }
        }
    }
    Assert-VerificationArtifacts $root $output $Python 'Recipe artifacts failed'
    Assert-VerificationIdentity $source (Get-VerificationSourceIdentity $root $sourcePaths) 'Recipe sources changed during acquisition.'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Recipe binaries changed during acquisition.' -Depth 2
    Write-VerificationJson "$output/result.json" @{ok=$true;cases=$cases;wholeG3Qualified=$false;directory=$output}
    [Console]::Out.WriteLine((@{ok=$true;directory=$output;cases=$cases.Count} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;cases=$cases;error=$_.Exception.Message}
    throw
}
