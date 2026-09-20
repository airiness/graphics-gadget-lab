# Coastal Atrium — Rendering Baseline 1

Static reference for directional-shadow comparisons, captured on September 20,
2026 (Asia/Tokyo). [baseline.json](baseline.json) records the frozen inputs,
SHA-256 fingerprints, camera profiles, capture settings and observations.
The existing PCF self-shadowing waves are part of this baseline.

## Recoverable inputs

| Input | Immutable reference |
| --- | --- |
| Runtime, shaders and exported scene | [graphics-gadget-lab at a9a4cb4](https://github.com/airiness/graphics-gadget-lab/tree/a9a4cb4aab5c17fec563b86f4d0ee224cf79d56e) |
| Blender source, authoring scripts and textures | [graphics-gadget-lab-content at 0fde6e1](https://github.com/airiness/graphics-gadget-lab-content/tree/0fde6e137eb1505715fe41492b70e27d11a4360d) |
| Shader source tree | `51e0be693dd13de836bdf8dc3b6ef4478155ff7c` |
| Build | Debug, x64, v143, MSVC 14.44.35207; Vulkan SDK 1.3.296.0 |
| Exporter | Blender 5.1.1, glTF I/O 5.1.19, separate glTF + binary + PNGs |

The manifest covers the saved `.blend`, four authoring/export scripts, nine
source textures, exported glTF/binary/textures, and the startup HDR environment.
The environment is loaded by application startup but contributes no lighting
or skybox in this Demo. Its FP16 sanitization warning is recorded below.
The [asset reference](../../../Models/GGLabCoastalAtrium/README.md) contains the
export command; the manifest records the export options. Reuse the frozen
export for rendering comparisons. Re-exporting is a separate content check.

Obtain these exact commits in separate checkouts to recover historical inputs.
Keep this capture directory and the validation script available separately:
they were added after the recorded renderer revision. Do not replace the old
asset or capture files when preparing a new comparison.

Executable and DXC DLL hashes identify the binaries used for capture; rebuilt
executables are not required to have identical bytes. Backend shader registry
IDs and registry file hashes identify the active compiled program sets. Generated
binaries, shader caches and local state directories are not committed.

## Capture profiles

Each camera uses profile version 1 and the shared capture profile version 1 in
the manifest. Camera profile numbering is independent of baseline numbering.
Coordinates are left-handed, Y-up, in meters; the model transform is identity.
All cameras are perspective, roll-free, at 16:9 with near/far 0.1/150 m.

| Camera | Position | Target | Vertical FOV, radians |
| --- | --- | --- | --- |
| `CAM_Courtyard` | `(23, 19, -28)` | `(-1, 1.8, -2)` | 0.6509917105 |
| `CAM_ShadowStairs` | `(5.5, 3.4, -14)` | `(0, 2.8, 1)` | 0.6939552104 |
| `CAM_InteriorExterior` | `(-12, 4, -2.9)` | `(2, 2.5, -5)` | 0.7984415392 |

| Shared setting | Value |
| --- | --- |
| Output / internal resolution | 1920 × 1080 / 1920 × 1080 |
| Pipeline / lighting | Forward PBR / Forward+ |
| Sun ray direction / color / intensity | Normalized `(-1, -0.85, 0.35)` / linear white / 3 |
| Environment intensity / skybox | 0 / disabled |
| Exposure / tone mapping | 0 EV / ACES fitted, then linear-to-sRGB |
| TAA / GTAO / bloom / HDR diff validation | All disabled |
| Directional shadow | Enabled, 2048² map, 3×3 PCF |
| Shadow range / caster extrusion | 30 / 300 m |
| Shadow ortho / depth padding | 1 / 200 m |
| Receiver / rasterizer / slope-scaled bias | 0 / 360 / 0.6 |
| Presentation | VSync off; DX12 two buffers; Vulkan three images, Mailbox |

## Reproduction

From the restored code checkout, build with the recorded MSVC/SDK toolchain:

```powershell
msbuild GraphicsGadgetLab.sln /m /p:Configuration=Debug /p:Platform=x64
```

From the checkout containing this baseline, verify the assets and screenshots:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateRenderingBaseline.ps1 `
  -RootDir . -ContentRoot ../GraphicsGadgetLabContent -CheckRenderer
```

For a separate historical checkout, set `-RootDir` to that checkout and
`-ManifestPath` to the absolute path of this `baseline.json`. `-CheckRenderer`
checks tracked renderer/build inputs against the recorded commit and rejects
untracked inputs in those directories. Omit it only for an intentional renderer
A/B change; keep the content hashes fixed and record the new renderer revision.
The script does not read live camera or rendering settings.

Launch each backend separately with a fresh, absolute state path outside the
executable directory. This isolates saved DevTools layout and derived caches:

```powershell
$captureStateRoot = Join-Path (Get-Location) 'Build/Verification/AtriumCapture-dx12'
./Build/Output/x64/Debug/GraphicsGadgetLab.exe --demo atrium --rhi dx12 `
  --absolute-mouse --state-root $captureStateRoot
```

Use `vulkan` and a different fresh path for Vulkan. Keep the initial 1920×1080
client size. Allow model/texture uploads, shader preparation and startup IBL
publication to finish before capture. The recorded sessions waited beyond
these startup operations; exact successful submission counts were not recorded.
Objects are static, simulation uses real time, and there is no animated water.
TAA is disabled, so jitter phase and temporal convergence warmup do not apply.

Use `Scene > Camera > Reference Views` to select each camera. Restoration sets
the pose, projection and exposure, resets movement velocity and requests the
existing camera-cut temporal reset. Wait for the restored view to settle, then
close panels. Restoration preserves viewport size and other rendering edits;
a fresh process supplies the settings above. Capture the presented window as
an original PNG. For timing, open `Diagnostics > Profiling`, leave CPU/GPU
profiling enabled, then pause the display and save the visible GPU pass table.

Optional shader registry verification after a run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/ValidateRenderingBaseline.ps1 `
  -ShaderArtifactRoot "$captureStateRoot/ShaderArtifacts" -Rhi dx12
```

## Reference images and repeated capture

| View | DirectX 12 | Vulkan |
| --- | --- | --- |
| Courtyard | [Reference](dx12-courtyard.png) | [Reference](vulkan-courtyard.png) |
| Shadow Stairs | [Reference](dx12-shadow-stairs.png) | [Reference](vulkan-shadow-stairs.png) |
| Interior / Exterior | [Reference](dx12-interior-exterior.png) | [Reference](vulkan-interior-exterior.png) |

All PNGs retain original Windows.Graphics.Capture bytes, at 1922×1112 including
window chrome and the menu bar. The scene is captured after final-color tone
mapping and DevTools composition. Panels are closed in scene images; cursor
highlights can remain. Desktop color management/HDR calibration was not
verified. These are SDR visual references, not raw GPU readbacks or full-window
pixel-comparison goldens. `savedAtUtc` is file archival time, not a GPU timestamp.

A second Vulkan process with a fresh state root reproduced the initial
[Courtyard](vulkan-courtyard-repeat.png) and restored
[Interior / Exterior](vulkan-interior-exterior-repeat.png) profiles. A read-only
RGB comparison used window rectangle `(1, 180, 1920, 920)`, excluding chrome,
menu and the upper cursor area: 1,766,400 pixels per pair.

| Repeated view | Pixels with any changed RGB channel | Mean absolute channel difference, 0–255 | Maximum channel difference |
| --- | --- | --- | --- |
| Courtyard | 11,836 (0.67%) | 0.01510 | 32 |
| Interior / Exterior | 11,106 (0.63%) | 0.00878 | 28 |

The compositions and visible shadow defect repeat; the captures are not bit
identical. This is static reproduction evidence on the recorded machine, with
no claim of deterministic frame replay or temporal stability.

## GPU observations

Hardware: NVIDIA GeForce RTX 5080, driver 610.88 (Windows 32.0.16.1088), Windows
11 Pro for Workstations build 26200. DX12 feature level 12_2; Vulkan adapter API
1.4.341 with application baseline 1.3. Both sessions used Debug with validation
requested and CPU/GPU profiling enabled. See the [DX12](dx12-backend.png) and
[Vulkan](vulkan-backend.png) backend records.

Each column below is one paused, completed GPU timestamp snapshot at
`CAM_InteriorExterior`, rounded to the precision displayed in the UI. Sampling
interval and variance were not measured. The panel's displayed frame number
belongs to the CPU snapshot; its GPU frame index is not exposed there.

| GPU scope, milliseconds | [DX12 sample](dx12-interior-timing.png) | [Vulkan sample](vulkan-interior-timing.png) |
| --- | --- | --- |
| Whole GPU frame | 2.62 | 0.18 |
| Shadow.Directional | 0.036 | 0.010 |
| Geometry.DepthPrepass | 0.037 | 0.010 |
| Lighting.ForwardPlus.Cull | 0.064 | 0.044 |
| Geometry.ForwardOpaque | 2.394 | 0.079 |
| PostProcess.FinalColor | 0.026 | 0.013 |
| UI.DevelopGui | 0.023 | 0.009 |

These isolated Debug observations establish timestamp availability. They do
not establish release performance, a backend ranking, or an optimization gain.
Future cost comparisons need repeated samples and variance on the same RHI,
hardware, build, content and capture profile, with the tested change identified.

## Verification and known limitations

- All three reference views presented on DX12 and Vulkan. The saved backend
  panels show healthy runtime state and zero resource-operation failures;
  Vulkan reports an enabled validation messenger with zero errors and warnings.
- Capture-session logs contained no error or critical entries, and application
  shutdown reported resource teardown. Vulkan startup logged the existing HDR
  sanitization warning: 16 channels clamped from a maximum of 97536 to 65000
  for FP16 storage. The environment has zero contribution in this preset.
- Paving and other receiving surfaces show PCF self-shadowing waves. Prior
  diagnostic toggles removed or reduced them when shadows/PCF were disabled
  or receiver bias was raised. Baseline captures retain the original settings.
- Dark interiors and metal surfaces reflect the disabled environment lighting.
  No GI, sky, shadow correction or material redesign is part of this baseline.
- No standalone hardware qualification, Release timing, continuous camera path,
  TAA convergence or temporal shadow-stability validation is claimed.
- Earlier screenshots outside this directory remain supplementary material and
  greybox references; they do not inherit this baseline's capture metadata.
