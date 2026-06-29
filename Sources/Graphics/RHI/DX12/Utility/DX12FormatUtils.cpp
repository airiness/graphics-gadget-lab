#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12FormatUtils.h"

#include <array>

namespace gglab
{
	namespace
	{
		constexpr std::array<DXGI_FORMAT, static_cast<size_t>(RHIFormat::Count)> DXGIFormats =
		{
			DXGI_FORMAT_UNKNOWN,
			DXGI_FORMAT_R8G8B8A8_TYPELESS,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_R16G16_FLOAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R32G32_FLOAT,
			DXGI_FORMAT_R32G32B32_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R32_TYPELESS,
			DXGI_FORMAT_R32_FLOAT,
			DXGI_FORMAT_R32_UINT,
			DXGI_FORMAT_D24_UNORM_S8_UINT,
			DXGI_FORMAT_D32_FLOAT,
		};

		static_assert([]
			{
				for (size_t index = 1; index < DXGIFormats.size(); ++index)
				{
					if (DXGIFormats[index] == DXGI_FORMAT_UNKNOWN)
					{
						return false;
					}
				}
				return DXGIFormats[0] == DXGI_FORMAT_UNKNOWN;
			}(), "Every RHIFormat must have a DXGI_FORMAT mapping.");
	}

	DXGI_FORMAT ToDXGIFormat(RHIFormat format) noexcept
	{
		const size_t index = static_cast<size_t>(format);
		if (index < DXGIFormats.size())
		{
			return DXGIFormats[index];
		}

		GGLAB_ASSERT_MSG(false, "Unhandled RHIFormat.");
		return DXGI_FORMAT_UNKNOWN;
	}

	RHIFormat ToRHIFormat(DXGI_FORMAT format) noexcept
	{
		for (size_t index = 0; index < DXGIFormats.size(); ++index)
		{
			if (DXGIFormats[index] == format)
			{
				return static_cast<RHIFormat>(index);
			}
		}

		GGLAB_ASSERT_MSG(false, "Unsupported DXGI_FORMAT for RHIFormat conversion.");
		return RHIFormat::Unknown;
	}
}
