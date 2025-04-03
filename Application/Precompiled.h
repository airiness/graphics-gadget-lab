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
#include <comdef.h>

#include <wrl.h>
using namespace Microsoft::WRL;

// DirectX
#include <d3dx12.h>
#include <dxgi1_6.h>
#if defined(BUILD_DEBUG)
#include <dxgidebug.h>
#endif
#include <DirectXMath.h>
using namespace DirectX;

// C++
#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>
#include <cassert>
#include <format>