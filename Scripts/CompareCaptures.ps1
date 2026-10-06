<#
.SYNOPSIS
Compares GraphicsGadgetLab frame captures pixel by pixel.

.DESCRIPTION
Compares one reference image with one candidate image, or every capture of a
reference directory with the matching capture of a candidate directory.

Directory captures are matched through their metadata sidecars by content
(Lab id, else Demo id), reference view, source and label. When a directory holds
several captures with one key, the most recent one is compared. A typical use is
a cross-backend comparison of one batch captured by a DX12 session and one by a
Vulkan session.

Metrics use the 8-bit RGB channels: the mean and maximum absolute channel
difference, the percentage of pixels whose largest channel difference exceeds
-Threshold, and PSNR (null for identical images). -DiffDirectory writes one
amplified absolute-difference PNG per compared pair.

Sidecars must carry a supported schemaVersion; others are listed under
"rejected" and not compared. Every pair with sidecars lists the frame settings
that differ between them under "metadataDifferences", such as the camera, the
time step or the total simulated time. Backend, request id, capture time and
file names are expected to differ and are not listed. A difference does not
fail the comparison, but pixel differences of such a pair may come from it
rather than from rendering.

Prints one JSON object on stdout. The exit code is 0 when every pair was
compared and stays within the optional tolerances and no sidecar was rejected,
and 1 otherwise.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CompareCaptures.ps1 -Reference Build/Sessions/dx12/Captures -Candidate Build/Sessions/vulkan/Captures -MaxMeanError 1.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Reference,
    [Parameter(Mandatory = $true)]
    [string]$Candidate,
    [ValidateRange(0, 255)]
    [int]$Threshold = 8,
    [string]$DiffDirectory,
    [ValidateRange(1, 64)]
    [int]$DiffScale = 8,
    # Optional tolerances; a pair above either one fails.
    [double]$MaxMeanError = [double]::PositiveInfinity,
    [double]$MaxDifferingPercent = [double]::PositiveInfinity
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

function Write-Result([hashtable]$Result, [int]$ExitCode) {
    [Console]::Out.WriteLine(($Result | ConvertTo-Json -Compress -Depth 10))
    exit $ExitCode
}

function Fail([string]$Message) {
    Write-Result @{ ok = $false; error = $Message } 1
}

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class CaptureComparison
{
    public int Width;
    public int Height;
    public bool SizeMatches;
    public int MaxDifference;
    public double MeanAbsoluteError;
    public double DifferingPercent;
    public double Psnr;
}

public static class CapturePixelComparer
{
    static byte[] Read(string path, out int width, out int height, out int stride)
    {
        using (var bitmap = new Bitmap(path))
        {
            width = bitmap.Width;
            height = bitmap.Height;
            var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.ReadOnly,
                PixelFormat.Format24bppRgb);
            stride = data.Stride;
            var bytes = new byte[stride * height];
            Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
            bitmap.UnlockBits(data);
            return bytes;
        }
    }

    public static CaptureComparison Compare(string referencePath, string candidatePath, int threshold,
        string diffPath, int diffScale)
    {
        int width, height, stride, candidateWidth, candidateHeight, candidateStride;
        var reference = Read(referencePath, out width, out height, out stride);
        var candidate = Read(candidatePath, out candidateWidth, out candidateHeight, out candidateStride);
        var result = new CaptureComparison();
        result.Width = width;
        result.Height = height;
        result.SizeMatches = width == candidateWidth && height == candidateHeight;
        if (!result.SizeMatches)
        {
            return result;
        }

        // PowerShell passes a null path as an empty string.
        var diff = !string.IsNullOrEmpty(diffPath) ? new byte[stride * height] : null;
        long sum = 0;
        double squares = 0.0;
        long differing = 0;
        int max = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int index = y * stride + x * 3;
                int pixelMax = 0;
                for (int channel = 0; channel < 3; ++channel)
                {
                    int difference = Math.Abs(reference[index + channel] - candidate[index + channel]);
                    sum += difference;
                    squares += (double)difference * difference;
                    pixelMax = Math.Max(pixelMax, difference);
                    if (diff != null)
                    {
                        diff[index + channel] = (byte)Math.Min(255, difference * diffScale);
                    }
                }
                max = Math.Max(max, pixelMax);
                if (pixelMax > threshold)
                {
                    ++differing;
                }
            }
        }

        double samples = 3.0 * width * height;
        result.MaxDifference = max;
        result.MeanAbsoluteError = sum / samples;
        result.DifferingPercent = 100.0 * differing / ((double)width * height);
        double meanSquare = squares / samples;
        result.Psnr = meanSquare > 0.0 ? 10.0 * Math.Log10(255.0 * 255.0 / meanSquare)
            : double.PositiveInfinity;

        if (diff != null)
        {
            using (var bitmap = new Bitmap(width, height, PixelFormat.Format24bppRgb))
            {
                var data = bitmap.LockBits(new Rectangle(0, 0, width, height), ImageLockMode.WriteOnly,
                    PixelFormat.Format24bppRgb);
                for (int y = 0; y < height; ++y)
                {
                    Marshal.Copy(diff, y * stride, data.Scan0 + y * data.Stride, width * 3);
                }
                bitmap.UnlockBits(data);
                bitmap.Save(diffPath, ImageFormat.Png);
            }
        }
        return result;
    }
}
'@

function Get-Field($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

# Capture sidecar schema versions this script understands.
$SupportedSchemaVersions = @(1)

# Frame settings that change what a capture shows. Backend, request id, frame
# serial, capture time and file names are expected to differ between captures.
$ComparedMetadataFields = @(
    'source', 'timing.mode', 'timing.settleFrames', 'content.demoId', 'content.labId',
    'image.width', 'image.height', 'image.displayFormat', 'camera.referenceView',
    'camera.position', 'camera.forward', 'camera.up', 'camera.verticalFovDegrees',
    'camera.nearPlane', 'camera.farPlane', 'time.fixedDeltaTime', 'time.totalTime',
    'developmentTools'
)

function Get-MetadataValue($Metadata, [string]$Path) {
    $value = $Metadata
    foreach ($name in $Path.Split('.')) { $value = Get-Field $value $name }
    return $value
}

function Test-SameValue($Left, $Right) {
    $leftValues = @($Left)
    $rightValues = @($Right)
    if ($leftValues.Count -ne $rightValues.Count) { return $false }
    for ($index = 0; $index -lt $leftValues.Count; ++$index) {
        $a = $leftValues[$index]
        $b = $rightValues[$index]
        if ($a -is [ValueType] -and $b -is [ValueType] -and $a -isnot [bool] -and $b -isnot [bool]) {
            if ([Math]::Abs([double]$a - [double]$b) -gt 1e-4) { return $false }
        }
        elseif ("$a" -cne "$b") {
            return $false
        }
    }
    return $true
}

function Get-MetadataDifferences($ReferenceMetadata, $CandidateMetadata) {
    $differences = @()
    if ($null -eq $ReferenceMetadata -or $null -eq $CandidateMetadata) { return $differences }
    foreach ($field in $ComparedMetadataFields) {
        $referenceValue = Get-MetadataValue $ReferenceMetadata $field
        $candidateValue = Get-MetadataValue $CandidateMetadata $field
        if (-not (Test-SameValue $referenceValue $candidateValue)) {
            $differences += @{ field = $field; reference = $referenceValue; candidate = $candidateValue }
        }
    }
    return $differences
}

# Reads a capture sidecar; returns the metadata, or a rejection reason.
function Read-CaptureMetadata([string]$Path) {
    try {
        $metadata = Get-Content -Raw -Path $Path | ConvertFrom-Json
    }
    catch {
        return @{ rejected = 'The sidecar is not valid JSON.' }
    }
    $version = Get-Field $metadata 'schemaVersion'
    if ($null -eq $version) {
        return @{ rejected = 'The sidecar has no schemaVersion.' }
    }
    if ($SupportedSchemaVersions -notcontains $version) {
        return @{ rejected = "Sidecar schemaVersion $version is not supported (supported: $($SupportedSchemaVersions -join ', '))." }
    }
    return @{ metadata = $metadata }
}

$rejected = @()

# Most recent capture per comparison key: content|view|source|label.
function Get-CaptureIndex([string]$Directory) {
    $index = @{}
    foreach ($file in Get-ChildItem -Path $Directory -Filter '*.json' -File) {
        $read = Read-CaptureMetadata $file.FullName
        if ($read.ContainsKey('rejected')) {
            $script:rejected += @{ file = $file.FullName; reason = $read.rejected }
            continue
        }
        $metadata = $read.metadata
        $image = Get-Field (Get-Field $metadata 'image') 'file'
        if (-not $image) { continue }
        $imagePath = Join-Path $Directory $image
        if (-not (Test-Path $imagePath)) { continue }
        $content = Get-Field $metadata 'content'
        $contentId = Get-Field $content 'labId'
        if (-not $contentId) { $contentId = Get-Field $content 'demoId' }
        $view = Get-Field (Get-Field $metadata 'camera') 'referenceView'
        $key = '{0}|{1}|{2}|{3}' -f $contentId, $view, (Get-Field $metadata 'source'), (Get-Field $metadata 'label')
        $capturedAt = [string](Get-Field $metadata 'capturedAtUtc')
        if (-not $index.ContainsKey($key) -or $index[$key].capturedAt -lt $capturedAt) {
            $index[$key] = @{
                key = $key; image = $imagePath; capturedAt = $capturedAt; metadata = $metadata
            }
        }
    }
    return $index
}

# Sidecar next to a single image, when there is one.
function Get-ImageMetadata([string]$ImagePath) {
    $sidecar = [System.IO.Path]::ChangeExtension($ImagePath, '.json')
    if (-not (Test-Path $sidecar)) { return $null }
    $read = Read-CaptureMetadata $sidecar
    if ($read.ContainsKey('rejected')) {
        $script:rejected += @{ file = $sidecar; reason = $read.rejected }
        return $null
    }
    return $read.metadata
}

function Get-DiffPath([string]$Name) {
    if (-not $DiffDirectory) { return $null }
    $safe = ($Name -replace '[^A-Za-z0-9_.-]', '_').Trim('_')
    if (-not $safe) { $safe = 'capture' }
    return Join-Path $DiffDirectory "$safe-diff.png"
}

function Compare-Pair([string]$Key, [string]$ReferenceImage, [string]$CandidateImage,
    $ReferenceMetadata, $CandidateMetadata) {
    $diffPath = Get-DiffPath $Key
    $comparison = [CapturePixelComparer]::Compare($ReferenceImage, $CandidateImage, $Threshold,
        $diffPath, $DiffScale)
    $pair = @{
        key = $Key; reference = $ReferenceImage; candidate = $CandidateImage
        width = $comparison.Width; height = $comparison.Height; sizeMatches = $comparison.SizeMatches
        metadataDifferences = @(Get-MetadataDifferences $ReferenceMetadata $CandidateMetadata)
    }
    if (-not $comparison.SizeMatches) {
        $pair['pass'] = $false
        return $pair
    }
    $pair['maxDifference'] = $comparison.MaxDifference
    $pair['meanAbsoluteError'] = [Math]::Round($comparison.MeanAbsoluteError, 4)
    $pair['differingPercent'] = [Math]::Round($comparison.DifferingPercent, 4)
    $pair['psnr'] = if ([double]::IsInfinity($comparison.Psnr)) { $null } else { [Math]::Round($comparison.Psnr, 2) }
    $pair['pass'] = $comparison.MeanAbsoluteError -le $MaxMeanError -and
        $comparison.DifferingPercent -le $MaxDifferingPercent
    if ($diffPath) { $pair['diff'] = $diffPath }
    return $pair
}

$Reference = [System.IO.Path]::GetFullPath($Reference)
$Candidate = [System.IO.Path]::GetFullPath($Candidate)
if ($DiffDirectory) {
    $DiffDirectory = [System.IO.Path]::GetFullPath($DiffDirectory)
    New-Item -ItemType Directory -Force -Path $DiffDirectory | Out-Null
}

$pairs = @()
$unmatched = @()
if ((Test-Path $Reference -PathType Leaf) -and (Test-Path $Candidate -PathType Leaf)) {
    $referenceMetadata = Get-ImageMetadata $Reference
    $candidateMetadata = Get-ImageMetadata $Candidate
    if ($rejected.Count -eq 0) {
        $pairs += Compare-Pair ([System.IO.Path]::GetFileNameWithoutExtension($Candidate)) `
            $Reference $Candidate $referenceMetadata $candidateMetadata
    }
}
elseif ((Test-Path $Reference -PathType Container) -and (Test-Path $Candidate -PathType Container)) {
    $referenceIndex = Get-CaptureIndex $Reference
    $candidateIndex = Get-CaptureIndex $Candidate
    foreach ($key in ($referenceIndex.Keys | Sort-Object)) {
        if ($candidateIndex.ContainsKey($key)) {
            $pairs += Compare-Pair $key $referenceIndex[$key].image $candidateIndex[$key].image `
                $referenceIndex[$key].metadata $candidateIndex[$key].metadata
        }
        else {
            $unmatched += @{ key = $key; missingFrom = 'candidate' }
        }
    }
    foreach ($key in ($candidateIndex.Keys | Sort-Object)) {
        if (-not $referenceIndex.ContainsKey($key)) {
            $unmatched += @{ key = $key; missingFrom = 'reference' }
        }
    }
}
else {
    Fail '-Reference and -Candidate must both be existing image files or both be capture directories.'
}

if ($pairs.Count -eq 0) {
    Write-Result @{
        ok = $false; error = 'No captures could be paired.'; unmatched = $unmatched; rejected = $rejected
    } 1
}
$failed = @($pairs | Where-Object { -not $_.pass }).Count
$metadataMismatches = @($pairs | Where-Object { $_.metadataDifferences.Count -gt 0 }).Count
$ok = $failed -eq 0 -and $unmatched.Count -eq 0 -and $rejected.Count -eq 0
Write-Result @{
    ok = $ok; compared = $pairs.Count; failed = $failed; threshold = $Threshold
    metadataMismatches = $metadataMismatches; pairs = $pairs; unmatched = $unmatched
    rejected = $rejected
} $(if ($ok) { 0 } else { 1 })
