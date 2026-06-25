#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

namespace gglab
{
	DXGI_FORMAT ToDXGIFormat(RHIFormat format) noexcept
	{
		switch (format)
		{
		case RHIFormat::Unknown:
			return DXGI_FORMAT_UNKNOWN;
		case RHIFormat::R8G8B8A8Typeless:
			return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case RHIFormat::R8G8B8A8Unorm:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case RHIFormat::R8G8B8A8UnormSrgb:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case RHIFormat::R16G16Float:
			return DXGI_FORMAT_R16G16_FLOAT;
		case RHIFormat::R16G16B16A16Float:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case RHIFormat::R32G32Float:
			return DXGI_FORMAT_R32G32_FLOAT;
		case RHIFormat::R32G32B32Float:
			return DXGI_FORMAT_R32G32B32_FLOAT;
		case RHIFormat::R32G32B32A32Float:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case RHIFormat::R32Typeless:
			return DXGI_FORMAT_R32_TYPELESS;
		case RHIFormat::R32Float:
			return DXGI_FORMAT_R32_FLOAT;
		case RHIFormat::D24UnormS8Uint:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case RHIFormat::D32Float:
			return DXGI_FORMAT_D32_FLOAT;
		}

		GGLAB_UNREACHABLE("Unhandled RHIFormat.");
	}

	RHIFormat ToRHIFormat(DXGI_FORMAT format) noexcept
	{
		switch (format)
		{
		case DXGI_FORMAT_UNKNOWN:
			return RHIFormat::Unknown;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return RHIFormat::R8G8B8A8Typeless;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return RHIFormat::R8G8B8A8Unorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return RHIFormat::R8G8B8A8UnormSrgb;
		case DXGI_FORMAT_R16G16_FLOAT:
			return RHIFormat::R16G16Float;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return RHIFormat::R16G16B16A16Float;
		case DXGI_FORMAT_R32G32_FLOAT:
			return RHIFormat::R32G32Float;
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return RHIFormat::R32G32B32Float;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return RHIFormat::R32G32B32A32Float;
		case DXGI_FORMAT_R32_TYPELESS:
			return RHIFormat::R32Typeless;
		case DXGI_FORMAT_R32_FLOAT:
			return RHIFormat::R32Float;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return RHIFormat::D24UnormS8Uint;
		case DXGI_FORMAT_D32_FLOAT:
			return RHIFormat::D32Float;
		default:
			GGLAB_ASSERT_MSG(false, "Unsupported DXGI_FORMAT for RHIFormat conversion.");
			return RHIFormat::Unknown;
		}
	}
}
