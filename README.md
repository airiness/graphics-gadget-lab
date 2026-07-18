# Graphics Gadget Lab

Graphics Gadget Lab (gglab) is a personal graphics research playground built
with C++ and Direct3D 12.

It is used to experiment with rendering techniques, GPU resource management,
render graphs, shaders, diagnostics, and small self-contained graphics Labs.

## Start Demo

[▶ Watch the Start Demo](Assets/Media/StartDemo.mp4)

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

Open `GraphicsGadgetLab.sln`, then build the `Application` project for x64.

The main executable is written to:

    Build/Output/x64/Debug/GraphicsGadgetLab.exe

## Run a Lab

    GraphicsGadgetLab.exe --lab gglab.lab.mini_pbr_grid --absolute-mouse

Run with `--help` to see the available startup options.

The application starts in FPS mouse mode. Press `T` to release the cursor and
interact with the developer UI.

## Status

gglab is an evolving personal research project. Its architecture and
experiments may change as new graphics ideas are explored.
