# Coastal Retreat Runtime bundle

The Coastal Atrium Demo (`--demo atrium`, persisted id
`Demo.Playground.CoastalAtrium`) loads this self-contained original scene.
The landing update adds shielded dock and stair fixtures, a platform bench,
marine storage, a lantern and local damp timber patches.

## Installed assets

Keep `GGLabCoastalRetreat.gltf`, its adjacent `.bin`, `Textures/` and
[`bundle.json`](bundle.json) together. A normal checkout contains every Runtime
input. Building, running and testing require no authoring repository, Blender,
exporter or asset-generation step.

The bundle has 613 unique meshes, 801 placed mesh nodes, 835 total nodes,
217,956 unique / 1,315,914 placed triangles, 28 materials, 26 original PNGs,
12 exported cameras and seven punctual-light references. See the
[asset notices](../THIRD_PARTY_NOTICES.md#original-project-assets).

`bundle.json` is the installed-file identity authority. Schema 1 lists the
entry point and complete glTF dependency closure, sorted by relative path,
with SHA-256 for each file. The Demo validates these bytes before loading;
missing, altered or escaping dependencies fail preparation explicitly.

The bundle revision is SHA-256 of UTF-8 text:
`gglab.asset-bundle.v1\n`, the bundle id and newline, the entry point and newline,
then each sorted file's path, NUL, lowercase SHA-256 and newline. The manifest
contains no authoring paths or identities. Authoring provenance is retained
outside this repository.

## Rendering contract

- Glass retains the current core-glTF approximation: `BLEND`, alpha 0.18,
  roughness 0.075, metallic 0. Physical transmission is not implemented.
- Base Color is sRGB; Normal and packed G roughness / B metallic use linear
  data. Damp timber shares the original timber normal map.
- Physical Daylight uses the existing Physical Sun, Earth atmosphere,
  PhysicalSky/IBL, manual EV100 15, TAA and GTAO. Bloom remains disabled.
- Exported point/spot lights are references; they do not create Runtime lights.
  The Demo has zero local lights. Emitters retain their primary material
  appearance; area lighting, Blue Hour and local/emissive GI transport are
  separate future work.

## Views and verification

The fourteen existing views and their temporal paths retain their original
poses and projections. Four profile-version-1 observations are added:

| View | Purpose |
| --- | --- |
| `Retreat_SeaPassage` | Deep shade under the continuous roof, 16:10 |
| `GI_InteriorExterior` | Dedicated GI doorway view, 50.22967-degree vertical FOV and 150 m far plane |
| `Landing_Dock` | Fixture spacing and the stair approach |
| `Landing_Damp` | Damp timber material response |

The four GI observations use Sea Passage, Courtyard, GI Interior/Exterior and
Lounge. Restoring a view resets temporal history; use 1280 x 800 and 64 settled
frames for the recorded daylight comparison. `CAM_InteriorExterior` retains its
existing projection and remains the source of its versioned temporal paths.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/Verification/RunVerification.ps1 -Suite coastal-assets
Build/Output/x64/Develop/GraphicsGadgetLab.exe --self-test app-content-registration
```

Capture through the project's `gglab-frame-capture` skill. Sidecars include
`content.assetBundle` and schema-versioned `sceneInputs` from the built frame:
exposure, post processing, shadows, sun, atmosphere, environment/IBL settings
and local-light count. Publication serials describe session state. IBL readiness
must also hold before treating requested bake settings as the active profile.

`CompareCaptures.ps1 -RequireMatchingInputs` rejects incompatible recorded
inputs. Total elapsed time and IBL publication serials may differ between
static captures; moving comparisons still require aligned sequence frames.
The [Physical Daylight reference](../../Media/GGLabCoastalRetreat/PhysicalDaylight/CAPTURE.md)
pins the installed bundle, actual frame inputs and renderer snapshot. The retained
historical Atrium Baseline1 describes its original asset revision.
