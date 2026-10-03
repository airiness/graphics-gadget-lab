# Coastal Atrium Surface Material Capture Guide

Manual SDR presentation checks for refined paving, satin structure metal and
woven upholstery. The [installed bundle](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
records the asset file identities and import checks. The [capture record](CAPTURE.md)
archives six DX12 views; matching Vulkan images and motion checks remain pending.

## Launch

Run from the code repository root. Use Debug WinApp and ShaderCompiler outputs
built together. Launch one backend at a time, closing it before starting the other:

```powershell
$surfaceStateRoot = Join-Path (Get-Location) 'Build/Verification/SurfaceImport'
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12 `
  --state-root "$surfaceStateRoot/DX12State"
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi vulkan `
  --state-root "$surfaceStateRoot/VulkanState"
```

## Shared settings

Keep the rendered client and internal resolution at 1920 x 1080, aspect 16:9.
The captured window extent may be larger because of its title and borders.

| Setting | Value |
| --- | --- |
| Scene / model transform | Coastal Atrium / identity |
| Pipeline / lighting | Forward PBR / Forward+ |
| Material view | Lit; retain default Specular AA settings |
| World Sun ray direction | Normalized `(-1, -0.85, 0.35)` |
| Sun TOA illuminance / angular radius | 120000 lux / 0.2666 degrees |
| Atmosphere | Enabled; Earth defaults |
| Environment source / skybox | Physical Sky / enabled |
| IBL intensity / yaw | 1 / 0 degrees |
| Exposure | Manual EV100 15; compensation 0 |
| TAA / GTAO / bloom | Disabled, as in the Atrium Demo profile |
| Shadows / other rendering settings | Current code defaults |

Use `Scene > World Lighting` to enable Physical Sun, Atmosphere, Physical Sky
and skybox. Wait for model/texture uploads and matching active/requested World
Lighting publication before capturing. Set IBL intensity to 1.

Select the main camera in `Scene > Camera`, choose a view under `Reference Views`
and restore it. Restoration applies the reference's legacy zero-EV policy;
set Manual EV100 15 and compensation 0 again after every restoration. Allow
the stationary view to settle, then close the panels for its scene image.

## Recommended views

Capture these six views on both DX12 and Vulkan, for twelve matching images.
The two established views retain profile version 1 and their original poses.
The four surface views are supplemental Runtime profiles, not additional glTF
camera nodes. All use perspective, near/far 0.1/150 m and roll-free Y-up poses.

| Reference view in the UI | Image stem | Inspect |
| --- | --- | --- |
| Courtyard | `courtyard` | Overall material balance, paving repeat and the lounge within its surroundings |
| Shadow Stairs | `shadow-stairs` | Paving/railings across distances, stair contacts and slat shadows |
| Paving Surface | `paving-surface` | Stone color variation, mineral detail, mortar depth and block edges |
| Railing Detail | `railing-detail` | Metal roughness, shallow finishing marks, rounded bars and base-plate contact |
| Upholstery Surface | `upholstery-surface` | Neutral fabric alongside the coated shell, seams and brushed frame |
| Upholstery Weave | `upholstery-weave` | Approximately 2 mm warp/weft relief, normal direction and absence of metallic response |

| Supplemental view | Position, meters | Target, meters | Vertical FOV, degrees |
| --- | --- | --- | --- |
| Paving Surface | `(1.8, 3.65, -4.5)` | `(-0.2, 2.4, -1.5)` | 25.360767 |
| Railing Detail | `(5.1, 2.15, -12.1)` | `(3.58, 1.55, -10.325)` | 32.268802 |
| Upholstery Surface | `(7.5, 3.95, -3.35)` | `(6.35, 3.1, -1.1)` | 20.861693 |
| Upholstery Weave | `(5.85, 3.5, -1.85)` | `(5.75, 3.065, -1.22)` | 13.585862 |

The established Interior / Exterior and Sky / Horizon views remain available
for additional checks. Optional evidence images may show the backend diagnostics,
World Lighting publication/settings and Camera exposure; identify those separately
from the six scene images. `Copy Camera Record` can retain each actual camera pose.

## Window images and processing

Prefer PNG window originals when the capture tool supports them. If it returns
JPEG, preserve those exact bytes as `.jpg` and record the lossy input format.
For example, use `upholstery-surface-dx12-window.jpg`. Keep originals under a
local directory such as
`Build/Verification/SurfaceImport/Raw/`, and provide them for review.

Close all floating panels and menus for scene captures. Move the pointer outside
the window or onto native chrome that will be excluded completely, including
its highlight. Title bars and window borders may remain in the originals.

Review each image's actual client bounds before cropping. Remove window chrome
and the application menu strip with a direct pixel crop; do not reuse an old
capture's crop rectangle blindly. Retain all remaining scene pixels without
resampling, color adjustment, sharpening, denoising or generative retouching.
If a pointer overlaps scene pixels, recapture the image. The final cropped extent
can differ from 1920 x 1080 when a menu overlay is excluded.

After review, archive the scene crops as `<stem>-dx12.png` and
`<stem>-vulkan.png` beside a capture record that states raw/cropped extents,
crop rectangles, image hashes, asset/binary identities, settings, backend evidence
and observed limitations. Existing captures retain their own inputs and identities.

These static SDR images support material and backend inspection. They do not
establish calibrated HDR accuracy or continuous/temporal stability. Later camera
motion should inspect distant paving and the fine weave for shimmer separately.
