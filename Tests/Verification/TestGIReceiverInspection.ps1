<# Same-observation receiver contributions and post-capture GPU markers. #>
[CmdletBinding()]
param(
    [string[]]$Configuration = @('Develop'),
    [string[]]$Rhi = @('dx12','vulkan'),
    [string]$OutputDirectory,
    [string]$RunDirectory,
    [string]$Python = 'python',
    [int]$TimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module "$root/Scripts/Verification/VerificationSupport.psm1"
$Configuration = @($Configuration | ForEach-Object { $_ -split ',' })
$Rhi = @($Rhi | ForEach-Object { $_ -split ',' })
if ($Configuration.Count -ne 1 -or -not $Rhi.Count -or @($Rhi | Where-Object { $_ -notin @('dx12','vulkan') }).Count) {
    throw 'Receiver inspection requires one configuration and explicit valid backends.'
}
if (-not $OutputDirectory -and -not $RunDirectory) { $OutputDirectory = "$root/Build/Validation/GIReceiverInspection" }
$output = New-VerificationRunDirectory -OutputDirectory $OutputDirectory -RunDirectory $RunDirectory -RunId ([Guid]::NewGuid().ToString('N').Substring(0,8))
function Source-Identity {
    Get-VerificationSourceIdentity $root @('Sources','Shaders','Tests/Verification','Scripts/Verification','Schemas','Projects')
}
function Read-ReceiverStatus($Session, $Run) {
    Wait-VerificationCondition -Poll { Invoke-VerificationSession status $Session @{ExpectedRunIdentity=$Run} } -Until {
        param($r) $r.frame -and $r.frame.giField -and $r.frame.giField.summary.inspectionAvailable -and
            $r.frame.giField.summary.receiverView.supported
    } -TimeoutSeconds $TimeoutSeconds
}
function Inspect-Receiver($Session, $Run, $Request, $Directory, [string]$View = '') {
    $products = @(@{kind='image';source='scene'},@{kind='image';source='composited'},@{kind='graph'},
        @{kind='resource';targets=@(@{resource='GI.Field.ReceiverIrradiance'},@{resource='GI.Field.ReceiverQuery'},
            @{resource='GI.Receiver.Position'},@{resource='GI.Receiver.VertexNormal'},@{resource='GI.Receiver.ShadingNormal'})})
    $options = @{ExpectedRunIdentity=$Run;Timing='next-frame';SettleFrames=0;
        GIReceiverInspection=(ConvertTo-Json -InputObject $Request -Compress);
        Products=(ConvertTo-Json -InputObject $products -Depth 8 -Compress);OutputDirectory=$Directory;TimeoutSeconds=$TimeoutSeconds}
    if ($View) { $options.View=$View }
    $result = Invoke-VerificationSession capture $Session $options
    if ($result.status -ne 'completed' -or $result.publication -ne 'Published') { throw 'Receiver inspection did not publish.' }
    Write-VerificationJson "$Directory/receipt.json" $result -Depth 32
    return $result
}
try {
    $source = Source-Identity
    $binaries = Get-VerificationBinaryIdentity $root $Configuration[0]
    Write-VerificationJson "$output/source-identity.json" $source
    Write-VerificationJson "$output/binaries.json" $binaries
    Write-VerificationJson "$output/backends.json" $Rhi
    $cases = @(
        @{id='plane';fixture=1;pixel=@(160,120);markers=$true;readiness=$true},
        @{id='markers-off';fixture=1;pixel=@(160,120);markers=$false;readiness=$true},
        @{id='background';fixture=1;pixel=@(0,0);markers=$true;readiness=$true},
        @{id='unsupported-cage';fixture=18;pixel=@(160,100);markers=$true;readiness=$true},
        @{id='outside';fixture=17;pixel=@(160,180);markers=$true;readiness=$true},
        @{id='fixed';fixture=1;pixel=@(160,120);markers=$true;readiness=$false},
        @{id='sunroom';fixture=4;pixel=@(160,140);markers=$true;readiness=$true},
        @{id='sunroom-floor';fixture=4;pixel=@(160,140);markers=$true;readiness=$true;view='F4_Floor'}
    )
    Write-VerificationJson "$output/cases.json" $cases
    foreach ($backend in $Rhi) {
        $session = 'gi-receiver-'+[Guid]::NewGuid().ToString('N').Substring(0,10)
        $start = $null
        try {
            $start = Invoke-VerificationSession start $session @{Configuration=$Configuration[0];Rhi=$backend;
                Lab='gglab.lab.gi_reference';WindowSize='320x200';NoDevTools=$true;Validation='standard';
                StateRoot="$output/$backend/state";StartTimeoutSeconds=$TimeoutSeconds}
            Write-VerificationJson "$output/$backend/start.json" $start
            if ($start.ownership -ne 'created') { throw 'Receiver verification requires an owned session.' }
            $run = $start.runIdentity
            $null = Wait-VerificationCondition -Poll { Invoke-VerificationSession status $session @{ExpectedRunIdentity=$run} } `
                -Until { param($r) $r.frame -and $r.frame.ready } -TimeoutSeconds $TimeoutSeconds
            $previousRequest = $null
            foreach ($case in $cases) {
                $directory = "$output/$backend/$($case.id)"
                New-Item -ItemType Directory -Path $directory -Force | Out-Null
                $changes = @(
                    @{id='gi_reference.fixture';type='enum';value=$case.fixture},
                    @{id='gi_reference.ev100';type='enum';value=0},
                    @{id='gi_reference.field_enabled';type='bool';value=$true},
                    @{id='gi_reference.field_sampling';type='bool';value=$true},
                    @{id='gi_reference.field_readiness';type='bool';value=$case.readiness},
                    @{id='gi_reference.probe_enabled';type='bool';value=$true},
                    @{id='gi_reference.seed';type='uint';value=61051}
                )
                $effect = Invoke-VerificationSession set $session @{ExpectedRunIdentity=$run;WaitConsumed=$true;
                    Changes=(ConvertTo-Json -InputObject $changes -Compress);TimeoutSeconds=$TimeoutSeconds}
                if (-not $effect.batch.succeeded) { throw 'Receiver fixture setup failed.' }
                $status = Read-ReceiverStatus $session $run
                Write-VerificationJson "$directory/status.json" $status
                $view = $status.frame.giField.summary.receiverView
                $request = @{pixel=$case.pixel;expectedGeneration=$status.frame.giField.generation;
                    expectedViewRevision=$view.revision;expectedViewId=$view.viewId;
                    expectedRefreshEpoch=$status.frame.giField.summary.field.refreshEpoch;markers=$case.markers}
                if ($previousRequest -and $previousRequest.expectedGeneration -ne $request.expectedGeneration) {
                    $replacement = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';
                        GIReceiverInspection=(ConvertTo-Json -InputObject $previousRequest -Compress);TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                    Write-VerificationJson "$directory/replacement-stale.json" $replacement
                    if ($replacement.status -ne 'failed' -or $replacement.failureCode -ne 'GIReceiverIdentityMismatch') {
                        throw 'A replacement field satisfied the earlier receiver request.'
                    }
                }
                if ($case.ContainsKey('view')) {
                    $moved = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';
                        View=$case.view;GIReceiverInspection=(ConvertTo-Json -InputObject $request -Compress);TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                    Write-VerificationJson "$directory/camera-stale.json" $moved
                    if ($moved.status -ne 'failed' -or $moved.failureCode -ne 'GIReceiverIdentityMismatch') {
                        throw 'A changed logical camera satisfied the earlier receiver pixel binding.'
                    }
                    $newStatus = Read-ReceiverStatus $session $run
                    if ($newStatus.frame.giField.generation -ne $request.expectedGeneration -or
                        $newStatus.frame.giField.summary.receiverView.revision -le $request.expectedViewRevision) {
                        throw 'Camera change replaced the field or failed to advance the logical view.'
                    }
                    $request.expectedViewRevision = $newStatus.frame.giField.summary.receiverView.revision
                    $request.expectedViewId = $newStatus.frame.giField.summary.receiverView.viewId
                    Write-VerificationJson "$directory/status.json" $newStatus
                }
                $null = Inspect-Receiver $session $run $request $directory
                $previousRequest = $request.Clone()
                $request.expectedViewRevision = [uint64]::MaxValue
                $stale = Invoke-VerificationSession capture $session @{ExpectedRunIdentity=$run;Timing='next-frame';
                    GIReceiverInspection=(ConvertTo-Json -InputObject $request -Compress);TimeoutSeconds=$TimeoutSeconds} -AllowFailure
                Write-VerificationJson "$directory/stale.json" $stale
                if ($stale.status -ne 'failed' -or $stale.failureCode -ne 'GIReceiverIdentityMismatch') { throw 'Stale receiver view did not fail.' }
            }
        } finally {
            if ($start -and $start.ownership -eq 'created') {
                $stop = Invoke-VerificationSession stop $session @{ExpectedRunIdentity=$start.runIdentity}
                Write-VerificationJson "$output/$backend/stop.json" $stop
                Copy-Item -LiteralPath $start.outputLog -Destination "$output/$backend/output.log"
                if (-not $stop.ok -or $stop.exitCode -ne 0) { throw 'Receiver session shutdown failed.' }
                $log = [IO.File]::ReadAllText($start.outputLog).Replace([string][char]0,'')
                if ($log -match '\[error\]|\[critical\]|Validation Error|VUID-|Assertion failed') { throw 'Receiver native/runtime validation failed.' }
            }
        }
    }
    Invoke-VerificationPython $Python "$PSScriptRoot/ValidateGIReceiverInspection.py" @('--root',$output) "$output/oracle-result.json" 'Receiver inspection failed'
    Assert-VerificationArtifacts $root $output $Python 'Receiver inspection failed'
    Assert-VerificationIdentity $source (Source-Identity) 'Receiver source changed during verification'
    Assert-VerificationIdentity $binaries (Get-VerificationBinaryIdentity $root $Configuration[0]) 'Receiver binaries changed during verification'
    Write-VerificationJson "$output/result.json" @{ok=$true;output=$output}
    [Console]::Out.WriteLine((@{ok=$true;output=$output} | ConvertTo-Json -Compress))
} catch {
    Write-VerificationJson "$output/result.json" @{ok=$false;error=$_.Exception.Message;output=$output}
    throw
}
