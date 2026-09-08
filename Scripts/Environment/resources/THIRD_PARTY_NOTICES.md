# Environment dependency notices

This is a local authoring deployment, not a general redistribution installer.
Windows, a compatible GPU driver, and the system Vulkan loader remain platform
requirements. The publisher includes application-local Microsoft CRT dependencies;
Debug CRT deployment remains subject to the owner's Visual Studio license and is
not a grant of redistribution rights.

Khronos Vulkan Validation Layers: Khronos Group and contributors, Apache-2.0.
Source: https://github.com/KhronosGroup/Vulkan-ValidationLayers/tree/vulkan-sdk-1.3.296.0
The layer binary and its unmodified relative-path JSON are supplied by the installed
Vulkan SDK. `Apache-2.0.txt` contains the upstream license.

DXC, Direct3D Agility SDK, PIX runtime and Assimp retain their upstream licenses
and are copied from the selected GGLab deployment. The Environment does not grant
additional rights to those dependencies. HDR asset source and attribution are
preserved in `payload/Assets/Textures/Skybox/THIRD_PARTY_NOTICES.md`.
