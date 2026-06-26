#pragma once
#include "Core/EnumFlags.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>

namespace gglab
{
	[[nodiscard]] constexpr inline RHITextureViewDesc MakeRHITexture2DArrayViewDesc(
		RHIFormat format,
		uint32_t mip,
		uint32_t firstSlice,
		uint32_t arraySize,
		uint32_t planeSlice = 0) noexcept
	{
		return RHITextureViewDesc{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::Texture2DArray,
			.m_Format = format,
			.m_MipSlice = mip,
			.m_FirstArraySlice = firstSlice,
			.m_ArraySize = arraySize,
			.m_PlaneSlice = planeSlice,
		};
	}

	[[nodiscard]] constexpr inline RHITextureViewDesc MakeRHITexture2DViewDesc(
		RHIFormat format,
		uint32_t mip = 0,
		uint32_t mipLevels = 1,
		uint32_t planeSlice = 0) noexcept
	{
		return RHITextureViewDesc{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::Texture2D,
			.m_Format = format,
			.m_MostDetailedMip = mip,
			.m_MipLevels = mipLevels,
			.m_MipSlice = mip,
			.m_PlaneSlice = planeSlice,
		};
	}
}
