# Graphics Gadget Lab

Graphics Gadget Lab (gglab) is a personal graphics research playground built
with C++ and Direct3D 12.

It is used to experiment with rendering techniques, GPU resource management,
render graphs, shaders, diagnostics, and small self-contained graphics Labs.

## Start Demo

[![Start Demo](Assets/Media/StartDemo.gif)](Assets/Media/StartDemo.mp4)

## Highlights

- Direct3D 12 renderer with DXC and HLSL
- Render graph and reusable render-pass pipeline
- Runtime-selectable Labs for focused experiments
- ImGui-based diagnostics and developer tools
- GPU resource, asset, and shader management experiments

## Build

Requirements:

- Windows 10 or later
- Visual Studio 18 Insiders with the v143 toolset
- Windows SDK
- Git submodules initialized
- Vulkan SDK 1.3.296 (optional; required for the Vulkan backend)

Open `GraphicsGadgetLab.sln`, then build the `Application` project for x64.

The main executable is written to:

    Build/Output/x64/Debug/GraphicsGadgetLab.exe

The Vulkan backend is enabled by default (`GGLAB_ENABLE_VULKAN=1`). It uses
the Vulkan SDK discovered through the `VULKAN_SDK` environment variable and
links `vulkan-1.lib`; the runtime uses the system Vulkan loader. Set
`GGLAB_ENABLE_VULKAN=0` for a DX12-only build that does not require the
Vulkan SDK.

## Run a Lab

    GraphicsGadgetLab.exe --lab gglab.lab.mini_pbr_grid --absolute-mouse

Run with `--help` to see the available startup options.

The application starts in FPS mouse mode. Press `T` to release the cursor and
interact with the developer UI.

## RHI backend selection

    GraphicsGadgetLab.exe --rhi vulkan
    GraphicsGadgetLab.exe --rhi dx12
    GraphicsGadgetLab.exe --list-adapters
    GraphicsGadgetLab.exe --rhi vulkan --adapter <index|identity-prefix>

The default backend is DX12. An explicit `--rhi vulkan` creates a Vulkan
instance, Win32 surface, enumerates and evaluates every physical device
against the GGLab Vulkan device profile, and creates a logical device and
graphics queue; it never falls back to DX12. `--list-adapters` prints the
profile evaluation for every adapter. Swapchain and rendering on Vulkan are
not implemented yet.

## Self-tests

First-party headless self-tests run as one executable per domain:

| Executable | Command | Coverage |
|---|---|---|
| `GraphicsGadgetLab.exe` | `--self-test all` | Application-owned in-app suites (`app-launch-options`, `app-devtools-view-profile`, `napa-voxel`) |
| `GGLabRuntimeTests.exe` | `--suite all` | Runtime/Foundation contract suites (`artifact-cache`, `asset-data`, `asset-upload-scheduler`, `publication-accounting`, `shader-compile-contracts`, `rendering-contracts`, `vulkan-contracts`) |
| `NapaVoxelCoreTests.exe` | `--suite all` | Pure NapaVoxelCore suites compiled as C++20 (`build-contract`, `coordinate`, `damage`, `edit`, `hash`, `mesher`, `multi-chunk`, `mutation`, `primitive`, `restore`, `storage`, `publication-data-only`) |
| `GGLabFoundationTests.exe` | (no arguments) | Foundation public-header link probe |

Use `--suite <id>` to select one suite in the test executables. CI runs all
four executables on every build leg.

## Status

gglab is an evolving personal research project. Its architecture and
experiments may change as new graphics ideas are explored.
