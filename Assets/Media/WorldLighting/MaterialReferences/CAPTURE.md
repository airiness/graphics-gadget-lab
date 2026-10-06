# Material Shading Reference Captures

Background runtime captures for the material implementation at code revision
`eca5cc1a`, captured on 2026-10-02 and archived unchanged on 2026-10-06.
No application was launched and no new capture was taken during archival.
Owner visual review and the merge visual gate remain pending.
These are production DX12/Vulkan renderings,
not Content previews or headless shader-test output. Historical media remains
unchanged. The previous [Physical Sky references](../PhysicalSkyReferences/CAPTURE.md)
provide the lighting/exposure convention; this archive uses the current material
reference and Research Lounge assets.

## Images

All PNGs are lossless 1920 x 1080 client-area captures, without Windows chrome,
mouse pointers, menus or DevTools panels. TAA is OFF in every image.

| Reference | DX12 | Vulkan |
| --- | --- | --- |
| `CAM_Clearcoat`, Lit | [PNG](clearcoat-dx12-lit.png) | [PNG](clearcoat-vulkan-lit.png) |
| `CAM_Clearcoat`, UnfilteredLit | [PNG](clearcoat-dx12-unfiltered.png) | [PNG](clearcoat-vulkan-unfiltered.png) |
| `CAM_Clearcoat`, Specular AA contribution | [PNG](clearcoat-dx12-specular-aa.png) | [PNG](clearcoat-vulkan-specular-aa.png) |
| `CAM_Anisotropy`, Lit | [PNG](anisotropy-dx12-lit.png) | [PNG](anisotropy-vulkan-lit.png) |
| `CAM_Anisotropy`, UnfilteredLit | [PNG](anisotropy-dx12-unfiltered.png) | [PNG](anisotropy-vulkan-unfiltered.png) |
| `CAM_Anisotropy`, Specular AA contribution | [PNG](anisotropy-dx12-specular-aa.png) | [PNG](anisotropy-vulkan-specular-aa.png) |
| Research Lounge, `CAM_Courtyard` | [PNG](lounge-courtyard-dx12-lit.png) | [PNG](lounge-courtyard-vulkan-lit.png) |
| Research Lounge, front inspection | [PNG](lounge-close-dx12-lit.png) | [PNG](lounge-close-vulkan-lit.png) |

Clearcoat, left to right: off, smooth coat, rough coat, independent coat normal
at scale 0.45. Anisotropy, near row left to right: off, strength 0.85 at 0 degrees,
90 degrees; far row: 45 degrees, mirrored U at 45 degrees, base normal at scale
0.4 with the 45-degree frame. See the
[asset contract](../../../Models/GGLabLightingContractMaterialReferences/README.md).

## Capture Settings

| Setting | Value |
| --- | --- |
| Build | x64 Debug, WinApp and ShaderCompiler outputs from capture revision `eca5cc1a` |
| Pipeline | Production Forward PBR; runtime validation requested |
| Material Lab | `gglab.lab.lighting_contract`, Extended Material References, Physical Sun preset |
| Atrium content | `Demo.Playground.CoastalAtrium`, Research Lounge bundle |
| Sun | 120000 lux, white chromaticity, angular radius 0.2666 degrees |
| Photon direction | Material Lab: normalized `(0, -1, 1)`; Lounge: normalized `(-1, -0.85, 0.35)` |
| Shadows | Material Lab: disabled; Lounge: production directional shadows |
| Exposure | EV100 15, compensation 0, scene pre-exposure enabled |
| Environment | Physical Sky, intensity 1, yaw 0; default atmosphere and Medium IBL quality |
| Presentation | SDR 8-bit BGRA acquisition; production ACES-fitted lit output |
| Other settings | TAA OFF, GTAO OFF, Bloom OFF; aerial perspective enabled |
| Readiness | Scene preparation complete and Physical Sky IBL published atomically before capture |

| View | Runtime position | Runtime target | Vertical FOV | Near / far |
| --- | --- | --- | --- | --- |
| `CAM_Clearcoat` | `(67, 3.2, -10)` | `(67, 0.8, 0)` | 37.299076 degrees | 0.1 / 100 m |
| `CAM_Anisotropy` | `(82.2, 4.4, -10)` | `(82.2, 0.8, 1.3)` | 37.299076 degrees | 0.1 / 100 m |
| `CAM_Courtyard` | `(23, 19, -28)` | `(-1, 1.8, -2)` | 37.299076 degrees | 0.1 / 150 m |
| Lounge front inspection | `(6.35, 4.2, -6.8)` | `(6.35, 3.15, -1)` | 37.299076 degrees | 0.1 / 150 m |

The lounge inspection view is a documented custom camera, not a persisted Lab
reference ID. The two material views use Physical Sun reference profile version 2.

## Acquisition and Verification

An isolated, temporary entry point under `Build/ReviewMaterial/` linked the capture revision's
WinApp objects (excluding `Main.obj`) and production libraries. It selects startup
parameters before asynchronous Lab preparation, freezes camera input, and omits
optional tooling. Lit uses the imported asset directly. The other material modes
clone authored material properties and model bindings through public asset APIs,
changing only `MaterialDebugView`; geometry, transforms and texture bindings remain
unchanged. Source assets stay owned by the Lab until normal GPU-fenced shutdown.
This is verification tooling, not a shipped screenshot feature or CLI option.

Windows Graphics Capture targets only the test process's HWND. The window is shown
at `HWND_BOTTOM` with `SWP_NOACTIVATE`; no foreground activation, keyboard or mouse
input is requested. Cursor capture and DWM rounded-corner masking are disabled.
Client coordinates are checked against the acquired window extent before copying
only the client rectangle to a D3D11 staging texture. Blocking GPU readback retains
the frame, texture and mapping through WIC PNG encoding. There is no resampling,
JPEG conversion, retouching or post-capture cropping.

[capture.json](capture.json) preserves the historical source revision, PNG SHA-256
hashes, per-image settings, RHI adapter identities, IBL publication and capture
records, process exit codes, known warnings, and the original RGB code-value
comparisons. A capture-time executable hash manifest was not saved; current build
outputs are not substituted for historical provenance. Full logs and
the temporary scripts remain local under `Build/ReviewMaterial/ArchiveCaptures/`.

Lit versus UnfilteredLit isolates production Specular AA with TAA disabled.
Contribution diagnostics display the base kernel in red, coat kernel in green,
and base perceptual roughness increase in blue. Compare the same camera and backend;
PNG differences are display-space code values, not linear-radiance error bounds.

## Limits

These stills are evidence for owner review of static presentation and
material/Specular AA response on the tested hardware, not a completed visual gate.
They do not establish motion stability, temporal shimmer
reduction, Release presentation, other GPUs, or native UI interaction. Cross-backend
visual agreement is not a pixel-identical-rendering guarantee. Anisotropic IBL
still uses the documented bent reflection into an isotropic prefiltered environment.
The black region below the Physical Sky horizon is existing behavior. Directional
shadow filtering is not a new acceptance target in this material archive.

A startup HDR asset may report the existing finite-channel FP16 clamp warning;
the final environment is Physical Sky, and any such warning is retained in the
manifest. Unexpected rendering, assertion, shader or validation warnings/errors
fail capture acceptance rather than being suppressed.
