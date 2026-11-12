[CmdletBinding()]
param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
Import-Module "$PSScriptRoot/GIFieldExperimentRecipes.psm1" -Force
Import-Module "$PSScriptRoot/../../Scripts/Verification/VerificationSupport.psm1"
$catalog = Get-Content "$PSScriptRoot/GIFieldExperimentRecipes.json" -Raw | ConvertFrom-Json
if ($catalog.schemaVersion -ne 1 -or $catalog.parameter -cne 'gi_reference.field_experiment_recipe' -or
    ($catalog.recipes.value -join ',') -cne '0,1,2,3,4,5,6,7' -or
    @($catalog.recipes.id | Select-Object -Unique).Count -ne 8) { throw 'Invalid persisted recipe catalog.' }

function Get-Preflight([string]$Name) {
    $source = [IO.File]::ReadAllText("$PSScriptRoot/$Name.ps1")
    $end = $source.IndexOf('if (-not $OutputDirectory -and -not $RunDirectory)')
    if ($end -lt 0) { throw "Missing acquisition boundary in $Name" }
    # Execute the real parameter binding and admission code, stopping before directory
    # creation or process launch. Bind file-relative imports to the owning script folder.
    $preflightRoot = $PSScriptRoot
    $source = $source.Substring(0,$end).Replace('$PSScriptRoot','$preflightRoot')
    return [scriptblock]::Create($source + "`nreturn `$experimentRecipe").GetNewClosure()
}

$checks = 0
function Check-Admission($Preflight, [hashtable]$Arguments, [bool]$Expected) {
    $accepted = $true
    $reason = ''
    try { $null = & $Preflight @Arguments } catch { $accepted = $false; $reason = $_.Exception.Message }
    if ($accepted -ne $Expected) { throw "Unexpected recipe admission: $($Arguments | ConvertTo-Json -Compress); $reason" }
    $script:checks++
}

$light = Get-Preflight 'TestGIFieldLightStep'
$feedback = Get-Preflight 'TestGIFieldFeedback'
$readiness = Get-Preflight 'TestGIFieldReadinessRepeat'
$inspection = Get-Preflight 'TestGIProbeInspection'
foreach ($recipe in $catalog.recipes) {
    foreach ($mode in @('none','fixed','progressive')) {
        foreach ($feedbackMode in @('off','on')) {
            $allowed = if ($mode -eq 'fixed') { $feedbackMode -eq 'off' -and $recipe.value -in @(0,1) }
                elseif ($mode -eq 'progressive') { $feedbackMode -eq 'off' -and $recipe.value -in @(2,3,6,7) }
                else { $feedbackMode -eq 'on' -or $recipe.value -notin @(4,5) }
            Check-Admission $light @{FieldRecipe=$recipe.id;AttributionMode=$mode;Tap='gi';
                Feedback=$feedbackMode;Step='small-increase'} $allowed
        }
    }
    Check-Admission $feedback @{FieldRecipe=$recipe.id} ($recipe.value -in @(0,2,5,6))
    Check-Admission $readiness @{FieldRecipe=$recipe.id} ($recipe.value -in @(0,1))
    Check-Admission $inspection @{FieldRecipe=$recipe.id;IntegrationDiagnostics=$true} ($recipe.value -in @(0,4))
}
Check-Admission $light @{FieldRecipe='missing'} $false
Check-Admission $light @{FieldRecipe='default';TraceSeed=@(61052,71052)} $false
Check-Admission $light @{FieldRecipe='progressive-lighting';Tap='gi';Feedback='off';TraceSeed=@(61052,71052)} $true
Check-Admission $light @{FieldRecipe='progressive-sampling';Tap='final';Feedback='on'} $false
Check-Admission $light @{FieldRecipe='default';AttributionMode='progressive'} $false
Check-Admission $inspection @{FieldRecipe='progressive-sampling'} $false
foreach ($retired in @('ProgressiveRefresh','ProgressiveSampling','RapidFeedbackRefresh',
    'RapidHardReset','TransientAttribution','ProgressiveAttribution')) {
    Check-Admission $light @{$retired=$true} $false
}
$result = @{ok=$true;checks=$checks}
if ($OutputDirectory) {
    $null = New-Item -ItemType Directory -Path $OutputDirectory -Force
    Write-VerificationJson "$OutputDirectory/result.json" $result
}
[Console]::Out.WriteLine(($result | ConvertTo-Json -Compress))
