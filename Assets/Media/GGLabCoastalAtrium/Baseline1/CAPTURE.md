# Coastal Atrium — Rendering Baseline 1

Directional-shadow references captured on September 20, 2026 (Asia/Tokyo).
[baseline.json](baseline.json) retains the frozen asset/renderer inputs,
camera poses and full render settings for historical comparisons.

## Images

| View | DirectX 12 | Vulkan |
| --- | --- | --- |
| Courtyard | [Image](dx12-courtyard.png) | [Image](vulkan-courtyard.png) |
| Shadow Stairs | [Image](dx12-shadow-stairs.png) | [Image](vulkan-shadow-stairs.png) |
| Interior / Exterior | [Image](dx12-interior-exterior.png) | [Image](vulkan-interior-exterior.png) |
| Courtyard, repeat | — | [Image](vulkan-courtyard-repeat.png) |
| Interior / Exterior, repeat | — | [Image](vulkan-interior-exterior-repeat.png) |
| Backend panel | [Image](dx12-backend.png) | [Image](vulkan-backend.png) |
| GPU profiling panel | [Image](dx12-interior-timing.png) | [Image](vulkan-interior-timing.png) |

## Render settings

| Setting | Value |
| --- | --- |
| Render / image dimensions | 1920 x 1080 / 1922 x 1112 including window chrome |
| Pipeline / lighting | Forward PBR / Forward+ |
| Sun ray direction / color / intensity | `normalize(-1, -0.85, 0.35)` / linear white / legacy intensity 3 |
| Environment intensity / skybox | 0 / disabled |
| Exposure / tone mapping | 0 EV / ACES fitted, then linear-to-sRGB |
| TAA / GTAO / bloom | Disabled |
| Directional shadow | Enabled; 2048 x 2048 map; 3 x 3 PCF |
| Shadow range / caster extrusion | 30 / 300 m |
| Shadow ortho / depth padding | 1 / 200 m |
| Receiver / rasterizer / slope-scaled bias | 0 / 360 / 0.6 |

Images are compressed SDR references with window chrome; cursor highlights may remain.

Machine-readable [capture evidence](capture-evidence.json) retains file identities,
method/crop metadata and recorded diagnostics.

## Observations

- The original PCF self-shadowing waves are retained.

## Known limitations

- Environment lighting is disabled in this historical preset.
- Static SDR frames and isolated Debug timings are not pixel goldens or performance benchmarks.
