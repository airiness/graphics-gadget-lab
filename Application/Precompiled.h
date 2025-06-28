#pragma once

#if _DEBUG
#define BUILD_DEBUG
#endif

// Windows
#ifndef NOMINMIX
#define NOMINMAX
#endif // !NOMINMIX

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <Shlwapi.h>	// PathRemoveFileSpecW
#include <shlobj.h>
#include <comdef.h>

#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#if defined(BUILD_DEBUG)
#include <dxgidebug.h>
#endif

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

// Project definitions
#include "CoreDef.h"

// Graphics definitions
#include "GraphicsDef.h"

#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

#include <entt/entt.hpp>
