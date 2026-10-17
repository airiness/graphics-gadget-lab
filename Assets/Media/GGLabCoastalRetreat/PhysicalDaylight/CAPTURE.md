# Physical Daylight reference

This is the pre-GI Coastal Atrium Demo reference for the installed Coastal
Retreat bundle. It retains eight views on each backend and a second capture
of the four GI observations. Original PNGs and sidecars are paired by filename.

`baseline.json` pins installed assets, captures, supporting evidence and renderer
commit `c8abb1bc865f9c82d377dd883cb0a2ab3a4b50e9`. The committed source matches
the exact renderer inputs used for these captures. Check out that revision in
a separate checkout to reconstruct the captured renderer and installed bundle.
Binary fingerprints describe the original build, not bit-reproducible binaries.

Run from the code repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/ValidateRenderingBaseline.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/ValidateRenderingBaseline.ps1 -CheckRenderer
```

The first command checks file integrity; the second also requires the recorded
source/build inputs. Neither command launches a renderer. No authoring checkout,
Blender or export step is needed. Historical Atrium Baseline1 remains a separate
reference for its original revision.

For a new comparison, build Develop and use the `gglab-frame-capture` skill:
hidden `--demo atrium`, scene source, 1280 x 800, isolated state, fixed 1/60 s,
64 settled frames per view, on both DX12 and Vulkan. Use the view list in the
manifest. Require all readiness gates and verify each sidecar's effective inputs.
TAA and GTAO are enabled; manual EV100 is 15, sun illuminance is 120,000 lux,
PhysicalSky/IBL is active, bloom is disabled, and local-light count is zero.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Verification/CompareCaptures.ps1 -Reference Assets/Media/GGLabCoastalRetreat/PhysicalDaylight/dx12/reference -Candidate Build/MyCapture -RequireMatchingInputs -DiffDirectory Build/MyDiff
```

`comparison.json` records retained cross-backend and repeated-image metrics.
All four GI repeats were pixel-identical within each backend. Cross-backend
errors are observations, not an acceptance threshold. Inspect images and
amplified differences before choosing a tolerance.

`nonfinite-audit.json` records all nine existing camera paths per backend with
fail-mode boundary and production checks. No boundary anomalies or NaN/Inf
replacements occurred. TAA's declared expected negative policy clamped 81 values
across 23 frames; no failed frames or FP16 clamps occurred. The loading shell's
IBL applicability follows the active bank during cold startup.

Captured on RTX 3060 / driver 577.19. These images establish the existing
Physical Daylight behavior before diffuse GI. They do not validate Blue Hour,
new local lighting, physical glass transmission, performance, other hardware,
or equality with an offline path tracer.
