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

#ifndef WINRT_LEAN_AND_MEAN
#define WINRT_LEAN_AND_MEAN
#endif // ! WINRT_LEAN_AND_MEAN

#define WINRT_NO_MODULE_LOCK
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

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
#include <limits>

// Project definitions
#include "CoreMacros.h"
#include "LogMacros.h"

#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

#include <GameInput.h>
#pragma comment(lib, "gameinput.lib")

// Graphics definitions
#include "GraphicsConstants.h"

#include <entt/entt.hpp>