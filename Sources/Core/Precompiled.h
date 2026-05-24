#pragma once

#include <Windows.h>
#include <Shlwapi.h>	// PathRemoveFileSpecW
#include <shlobj.h>
#include <comdef.h>

#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX
#include <d3dx12.h>
#include <dxgi1_6.h>
#if defined(BUILD_DEBUG)
#include <dxgidebug.h>

// PIX
#include <pix3.h>
#endif

// Shader Compiler
#include <dxcapi.h>

// D3D12MA
#include <D3D12MemAlloc.h>

// C++
#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>
#include <format>
#include <array>
#include <optional>
#include <span>
#include <queue>
#include <tuple>
#include <mutex>
#include <filesystem>
#include <unordered_map>
#include <limits>
#include <functional>
#include <utility>
#include <string_view>
#include <shared_mutex>
#include <bit>
#include <concepts>
#include <fstream>

// Project definitions
#include "Core/CoreMacros.h"
#include "Core/Log/LogMacros.h"

// SimpleMath
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

// GameInput
#include <GameInput.h>
#pragma comment(lib, "gameinput.lib")

// entt
#include <entt/entt.hpp>

// ImGui
#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>
