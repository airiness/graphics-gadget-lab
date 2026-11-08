Set-StrictMode -Version 3.0
Import-Module "$PSScriptRoot/../../Scripts/Verification/VerificationSupport.psm1"

# Each refresh reason gates with its own refresh and steady change RMS. Steady RMS is
# measured after the timeout of the calibration field, or after a calibrated handoff in a
# second field at the calibration seed whose refresh adopts the measured refresh RMS, so
# its steady window follows the gated runs' integration history. ValidateGIField*.py
# check this evidence through GIFieldReadinessCalibration.py.
$script:SigmaInputs = @{
    hardReset = @('field_refresh_noise','field_steady_noise')
    lighting = @('field_lighting_refresh_noise','field_lighting_steady_noise')
}

function Test-GIFieldSigmaInput([string]$Name) {
    return $Name -in ($script:SigmaInputs.Values | ForEach-Object { $_ })
}

function Write-GIFieldCalibrationJson([string]$Path, $Value) {
    # Status records may carry a complete readiness trace; indentation would multiply their size.
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 32 -Compress),
        (New-Object Text.UTF8Encoding($false)))
}

function Write-GIFieldCalibrationRecipe([string]$OutputDirectory, [string]$SteadyCalibration) {
    if ($SteadyCalibration -notin @('after-timeout','after-handoff')) { throw 'Unknown steady calibration method.' }
    Write-VerificationJson "$OutputDirectory/readiness-calibration.json" @{schemaVersion=1;steadyCalibration=$SteadyCalibration}
}

function Get-GIFieldUncalibratedInputs([ValidateSet('hardReset','lighting')][string]$Reason = 'hardReset') {
    $names = $script:SigmaInputs[$Reason]
    return @{ $names[0] = 0.0; $names[1] = 0.0 }
}

function Get-GIFieldAdoptedInputs([ValidateSet('hardReset','lighting')][string]$Reason, $Calibration, $Steady) {
    $names = $script:SigmaInputs[$Reason]
    $steadySigma = if ($Steady) { $Steady.steadyNoiseEstimate } else { $Calibration.steadyNoiseEstimate }
    return @{ $names[0] = [float]$Calibration.refreshNoiseEstimate; $names[1] = [float]$steadySigma }
}

<#
Measures one reason's steady RMS after a calibrated handoff and returns that field's
giField status. SetInputs takes a hashtable of unprefixed inputs and returns the consumed
effect; Status returns the current status. The sigma change restarts the field at the
same seed. For a lighting refresh, Prepare receives the replaced generation, brings the
new field to the lighting refresh being calibrated and returns that refresh epoch.
#>
function Measure-GIFieldSteadyAfterHandoff {
    param(
        [Parameter(Mandatory)][scriptblock]$SetInputs,
        [Parameter(Mandatory)][scriptblock]$Status,
        [Parameter(Mandatory)]$Calibration,
        [Parameter(Mandatory)][string]$Directory,
        [ValidateSet('hardReset','lighting')][string]$Reason = 'hardReset',
        [scriptblock]$Prepare,
        [int]$TimeoutSeconds = 600
    )
    if (($Reason -eq 'lighting') -ne [bool]$Prepare) { throw 'Only a lighting calibration prepares its refresh.' }
    $names = $script:SigmaInputs[$Reason]
    $prefix = if ($Reason -eq 'lighting') { 'lighting-' } else { '' }
    if ([float]$Calibration.refreshNoiseEstimate -eq 0) {
        # The first pass already gated its handoff with the adopted refresh sigma, the
        # uncalibrated minimum. A second field would repeat it, and unchanged inputs would
        # not restart one, so the first pass is its own steady calibration.
        Write-GIFieldCalibrationJson "$Directory/${prefix}steady-calibration.json" @{
            reusedCalibrationPass=$true;frame=@{giField=$Calibration}}
        return $Calibration
    }
    $replaced = $Calibration.generation
    $effect = & $SetInputs @{ $names[0] = [float]$Calibration.refreshNoiseEstimate; $names[1] = 0.0 }
    Write-VerificationJson "$Directory/${prefix}steady-calibration-effect.json" $effect
    $epoch = if ($Prepare) { & $Prepare $replaced } else { 1 }
    $until = {
        param($r)
        $f = $r.frame.giField
        $f -and $f.generation -ne $replaced -and $f.summary -and $f.summary.field.refreshEpoch -eq $epoch -and
            $f.steadyNoiseSamples -eq 64
    }.GetNewClosure()
    $steady = Wait-VerificationCondition -Poll $Status -Until $until -TimeoutSeconds $TimeoutSeconds
    Write-GIFieldCalibrationJson "$Directory/${prefix}steady-calibration.json" $steady
    return $steady.frame.giField
}

Export-ModuleMember -Function Test-GIFieldSigmaInput,Write-GIFieldCalibrationJson,Write-GIFieldCalibrationRecipe,
    Get-GIFieldUncalibratedInputs,Get-GIFieldAdoptedInputs,Measure-GIFieldSteadyAfterHandoff
