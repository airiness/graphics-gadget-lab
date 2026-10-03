# Coastal Atrium Surface Materials

Static SDR presentation checks captured on October 3, 2026 (UTC). These images
inspect the combined refined paving, satin structure metal and woven upholstery
bundle. The [installed asset reference](../../../Models/GGLabCoastalAtriumResearchLounge/README.md)
records the source/export/texture file identities and import checks; the
[capture guide](CAPTURE_GUIDE.md) lists the reference poses and reproduction steps.

## Inputs

- Installed bundle: glTF JSON, adjacent binary and fifteen PNGs. All seventeen
  files match the independently regenerated export byte for byte.
- glTF SHA-256: `2bddb863ef030d7bf56822b301eb8d0dc584167260ad1208ebeef4b7a4a29863`.
- Binary SHA-256: `741fc6668d7ee5bbc55a1cc80dc61dd3b90de8b316bde5e0e88604dbd89f0f1b`.
- Authoring-source fingerprint, provenance only:
  `bd2f21b56a43075a5ac4a5c1902b3bccc48dbd3d7dc01bd561b75c8c4ee00678`.
- Shader source tree content ID: `1167f85ebf7fd2441e6e973415ed1dc502befea3`.
  Renderer and shader sources are unchanged in this import. Four supplemental
  Runtime reference views and material import checks are added in the working tree.
- Captures use WinApp Debug x64 and its matching development shader compiler,
  built with Visual Studio 2022 Community x64 MSBuild and
  `/p:PreferredToolArchitecture=x64`. Debug/Release import suites passed 220 checks each.
- GPU: NVIDIA GeForce RTX 5080; DX12 feature level 12_2, driver `32.0.16.1714`.

Input identity uses actual file hashes and shader content rather than branch names
or commit ancestry. Rebase or squash does not change these identities while the
referenced bytes remain unchanged. This capture does not replace historical
baseline, coastal-detail or concrete-surface images.

| Capture input | SHA-256 |
| --- | --- |
| Debug WinApp executable | `fc9abbead8ad6f8e96eb288ab7d2261e3272c66fa822d63d001d903dbe2a5417` |
| Debug development shader compiler | `d53ca7f3ed52bf1a940a1f999b6d42b45df6b2b51110659f45faa80be13f4256` |
| Runtime reference view header | `3d52c70cd8b22b9b773288f4763e492336388c3324ea93ce1dfa1bc614f5283b` |

## Settings and method

| Setting | Value |
| --- | --- |
| Scene / model transform | `Demo.Playground.CoastalAtrium` / identity |
| Swapchain and internal extent | 1920 x 1080 |
| Pipeline / lighting | Forward PBR / Forward+ |
| Material view / Specular AA | Lit / retained code defaults |
| World Sun ray direction | Normalized `(-1, -0.85, 0.35)` |
| Physical Sun illuminance / angular radius | 120000 lux / 0.2666 degrees |
| Atmosphere | Enabled; Earth defaults |
| Environment source / skybox | Physical Sky / enabled |
| Published World Lighting | Active/requested generation 3; zero retiring persistent textures |
| IBL intensity / yaw | Calibrated 1 / 0 degrees; world aligned |
| IBL environment | 512 x 512 x 6 FP32; ten mips |
| Exposure | Manual EV100 15; compensation 0, reapplied after each reference restoration |
| TAA / GTAO / bloom | Disabled by the Atrium Demo profile |
| Shadows and other rendering settings | Retained code defaults |

DX12 was launched through the desktop screenshot tool using the Debug executable,
then switched from Demo.Start to Coastal Atrium through the Demo selector.
It used the executable's default writable state location. Lighting and camera
settings were applied and inspected in the UI; the isolated-state command in
the guide remains the recommended command-line reproduction path.

The desktop capture tool returned **JPEG** window originals at 1922 x 1112.
Their original bytes and diagnostic screenshots are retained locally under
`Build/Verification/SurfaceImport/Raw/`. The pointer was moved onto the native
title bar before accepting each scene image. Floating panels and menus were closed.

Each archived PNG is a direct crop of the decoded JPEG: `(left, top, right,
bottom) = (1, 100, 1921, 1110)`, with exclusive right/bottom bounds. The result
is 1920 x 1010. The crop removes native chrome, the menu strip and an additional
top strip to exclude the cursor highlight completely, plus one bottom client row.
It does not preserve the full 16:9 viewport. Every retained pixel matches the
decoded source rectangle exactly; no resizing, color change, sharpening,
denoising or generative retouching was applied. PNG does not recover detail
lost by the original JPEG compression.

## Scene images

Six DX12 views are archived. Matching Vulkan screenshots remain pending because
the command-line launch did not expose a targetable window to the screenshot
tool; the alternative Visual Studio launch required application access that
timed out. No Vulkan image or visual acceptance is claimed by this record.

| Reference view | DX12 |
| --- | --- |
| Courtyard | [Image](courtyard-dx12.png) |
| Shadow Stairs | [Image](shadow-stairs-dx12.png) |
| Paving Surface | [Image](paving-surface-dx12.png) |
| Railing Detail | [Image](railing-detail-dx12.png) |
| Upholstery Surface | [Image](upholstery-surface-dx12.png) |
| Upholstery Weave | [Image](upholstery-weave-dx12.png) |

### Image identities

| Archived image | SHA-256 |
| --- | --- |
| `courtyard-dx12.png` | `7992ce3aecf6122f32b9a6358e8eb91d2faae6eadab890a93e8da6e52d621484` |
| `shadow-stairs-dx12.png` | `282a3c6bc03f90ff5d20bfe9c167090e26ceb82acd867abb99bb7b50c04a5d42` |
| `paving-surface-dx12.png` | `a463517d690d8537df66d232f7f5ee6146c0aea53cec03f6d1fd07acd0d826b5` |
| `railing-detail-dx12.png` | `780c30148d29d964138df4508729206f6869de618bca15921f467b7bf2396b1f` |
| `upholstery-surface-dx12.png` | `33d537d0a4d317e3cbae12ba42d8aabf45351fdd3098fc9430c594099e0632be` |
| `upholstery-weave-dx12.png` | `d248176c2358775bfe75580208e77a76b099569c566eee6653681f1715cfcef5` |

| Original window image, local only | SHA-256 |
| --- | --- |
| `courtyard-dx12-window.jpg` | `4d885ef20855de6c7b90f7dcdda079b0a87e3e8102527c1d234808075ccf6b26` |
| `shadow-stairs-dx12-window.jpg` | `9361df77dc86e145d53fd746b5621a90f7fe95a23ceb19f3dc759d300ec2e314` |
| `paving-surface-dx12-window.jpg` | `6c463e7ad755d5a13835dd1538c229c1beab4e89203b53ddbcca3aa93f7f63e1` |
| `railing-detail-dx12-window.jpg` | `6b0872730b646284c1c23405077e8eb99df23ed241f8147bb2bd27f951a62d9e` |
| `upholstery-surface-dx12-window.jpg` | `b56fa69c911e831e6ac7ade7176936fc75cae72b37ecbeafd72f0e3c16532d14` |
| `upholstery-weave-dx12-window.jpg` | `015894d85cb61ec9685d41904493ae29b2fa69faf6f482597ccb8dc0e1b2083a` |

## Verification and observations

- DX12 RHI summary reported `Runtime health: healthy`, a 1920 x 1080
  `R8G8B8A8Unorm` presentation target, and zero resource creation/import,
  invalid-use, destroy, stale-handle and double-destroy failures.
  The application window closed normally through Alt+F4 after capture.
- The Physical Sky UI showed active/requested generation 3, no retiring
  persistent textures, the expected Sun direction and 120000 lux. Environment
  settings showed skybox enabled, calibrated intensity 1 and zero yaw.
- Paving retains a staggered block layout with readable joints and muted mineral
  variation in the close view and across the stair faces.
- The railing detail shows the dark satin-metal finish, shallow mottling,
  beveled edges, end cap and base-plate contacts. This static exposure does not
  establish the quality of moving specular highlights.
- Upholstery reads as neutral matte woven fabric alongside the teal coated shell.
  The close weave view resolves the warp/weft pattern without missing maps or
  an obvious inverted-normal seam. Geometry and seam placement remain unchanged.
- Existing directional-PCF shadow bands and the black region beyond the bounded
  ocean remain visible. They are retained observations, not material fixes.
- Tool launch constraints left Vulkan presentation/visual checks incomplete.
  Static JPEG-derived SDR images do not establish HDR calibration or camera-motion
  stability. Distant paving and fine weave still need a separate motion check
  for shimmer; continuous Specular AA acceptance remains open.
