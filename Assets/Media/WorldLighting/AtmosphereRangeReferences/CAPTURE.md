# Atmosphere Range capture record

These images are GGLab display captures of the C3 fixture, not Blender renders.
The Lab was ready, the model imported with no texture dependency, and Physical
Sky IBL had published before capture (DX12 generation 3; Vulkan generation 2).
Both runs used the Debug executable, a 1920 x 1080 window and the
`CAM_Range_01000m` reference view. Vulkan selected an NVIDIA GeForce RTX 5080.
Both used EV100 15 / compensation 0,
pre-exposure on, default Earth atmosphere, default 120,000 lux world sun,
Physical Sky with intensity 1 / yaw 0, GTAO/bloom/shadows off and TAA off.
The only intended on/off difference is the Lab's **Aerial Perspective** parameter.
The 25 m follow-up enables TAA and restores its named reference camera.

| Backend | 1 km transport on | Same view, transport off | Short path with TAA on |
| --- | --- | --- | --- |
| DX12 | [on](dx12-1000m-aerial-on.png) | [off](dx12-1000m-aerial-off.png) | — |
| Vulkan | [on](vulkan-1000m-aerial-on.png) | [off](vulkan-1000m-aerial-off.png) | [25 m](vulkan-25m-taa-on.png) |

Computer Use moved its pointer to the title bar before each capture. The saved
images crop the tool JPEG to the 1920 x 1060 content region below the menu bar,
without scaling or including the title-bar pointer. The Lab controls remain
visible to identify the mode and view. Do not treat JPEG-derived 8-bit screenshots
as linear HDR measurements.

At content pixel `(870,510)` inside the dark 1 km patch, both backends recorded
`(28,27,25)` with transport off and `(32,33,38)` with it on (8-bit display RGB).
The open-sky control `(1300,510)` remained `(88,133,174)` in both modes and both
backends. These spot values confirm a visible surface-only response for this
camera/exposure; they are not a transmittance or radiance error estimate. Vulkan
validation emitted one `WARNING-Shader-OutputNotConsumed` performance message on
a graphics pipeline after TAA was enabled, with no shader, assertion, lifetime,
synchronization or device-loss error in this run.

## Scene-linear GPU readback

The opt-in C3 Lab probe sampled the center surface pixel `(960,540)` and an
open-sky control pixel `(960,135)` before and after the aerial composite, plus
the corresponding atlas transmittance and in-scattering. The Debug executable
ran at 1920 x 1080 with EV100 15, TAA off, the same published Physical Sky
settings and a scene pre-exposure of `2.5431314e-5`. DX12 used active world
generation 3; Vulkan used generation 2. Each row uses the named range camera.
The measured center-ray distances were within 0.008 m of the fixture distances.

| Fixture distance | Transmittance RGB (both RHIs) | In-scattering blue, scene-linear (DX12) | Largest RGB composition residual (both RHIs) |
| ---: | --- | ---: | ---: |
| 25 m | (0.999512, 0.999432, 0.998944) | 12.127 | 0.388 |
| 50 m | (0.999376, 0.998888, 0.997911) | 24.634 | 2.321 |
| 100 m | (0.998544, 0.998047, 0.996102) | 49.585 | 1.725 |
| 250 m | (0.997329, 0.995376, 0.990523) | 123.697 | 1.938 |
| 500 m | (0.994745, 0.990897, 0.981306) | 242.356 | 1.828 |
| 1000 m | (0.990123, 0.982400, 0.963623) | 470.775 | 1.451 |

The residual is `max(abs(L_composite - (L_surface * T + L_in-scattering)))`
after converting the sampled FP16 scene colors back to scene-linear values.
At 25 m, the center blue channel changed from 4298.4 to 4305.6; at 1000 m,
from 4324.8 to 4636.8. All three transmittance channels decreased and all
three in-scattering channels increased across these fixed-atmosphere distances.
The sky control had zero measured change for all six views on both RHIs. DX12
and Vulkan surface/composite readbacks matched at the printed precision, with
in-scattering differences below 0.0005 scene-linear units. Neither run reported
a validation, synchronization or device-loss error from this probe.
After adding the per-sample frame-serial guard against unsubmitted-frame
readback, the 25 m measurement was repeated on both RHIs with identical values.

This confirms the implemented composition and distance trend at these six
samples; it is not an independent reference for the transport integration or a
bound on the 32-slice atlas interpolation error. Altitude changes and moving
TAA sequences still need separate quantitative checks.
