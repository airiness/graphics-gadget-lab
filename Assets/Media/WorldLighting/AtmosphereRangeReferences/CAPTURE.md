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
camera/exposure; they are not a transmittance or radiance error estimate. Other
targets, altitude changes, 32-slice interpolation and moving/TAA sequences still
need linear GPU readback to establish quantitative transport accuracy. Vulkan
validation emitted one `WARNING-Shader-OutputNotConsumed` performance message on
a graphics pipeline after TAA was enabled, with no shader, assertion, lifetime,
synchronization or device-loss error in this run.
