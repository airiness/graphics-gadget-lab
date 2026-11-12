Set-StrictMode -Version 3.0
$script:Catalog = Get-Content "$PSScriptRoot/GIFieldExperimentRecipes.json" -Raw | ConvertFrom-Json

function Get-GIFieldExperimentRecipe {
    param(
        [string]$Name = 'default',
        [ValidateSet('light-step','feedback','readiness')][string]$Use = 'light-step'
    )
    $entries = @($script:Catalog.recipes | Where-Object id -CEQ $Name)
    if ($entries.Count -ne 1) { throw "Unknown GI field experiment recipe: $Name" }
    $entry = $entries[0]
    # Preserve each runner's existing admission scope, independently of Lab availability.
    if (($Use -eq 'feedback' -and $entry.value -notin @(0,2,5,6)) -or
        ($Use -eq 'readiness' -and $entry.value -notin @(0,1))) {
        throw "Recipe '$Name' is outside the $Use experiment scope."
    }
    return @{
        id=$entry.id;value=[int]$entry.value;name=$entry.name;
        rapidHardReset=([int]$entry.value -band 1) -ne 0;
        progressiveRefresh=([int]$entry.value -band 2) -ne 0;
        progressiveSampling=([int]$entry.value -band 4) -ne 0
    }
}

Export-ModuleMember -Function Get-GIFieldExperimentRecipe
