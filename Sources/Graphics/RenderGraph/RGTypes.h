#pragma once
#include "Core/EnumFlags.h"
#include "Core/TypedIndex.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/RHITypes.h"

#include <cstdint>

namespace gglab
{
	GGLAB_DEFINE_TYPED_INDEX(RGPassNodeIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGResourceNodeIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGVirtualResourceIndex, uint32_t);
	GGLAB_DEFINE_TYPED_INDEX(RGTextureViewId, uint32_t);

	[[nodiscard]] constexpr inline RHITextureViewDesc MakeRHITexture2DArrayViewDesc(
		RHIFormat format,
		uint32_t mip,
		uint32_t firstSlice,
		uint32_t arraySize,
		RHITextureAspect aspect = RHITextureAspect::Color) noexcept
	{
		return RHITextureViewDesc{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::Texture2DArray,
			.m_Format = format,
			.m_Subresources =
			{
				.m_BaseMip = mip,
				.m_MipCount = 1,
				.m_BaseArraySlice = firstSlice,
				.m_ArraySliceCount = arraySize,
				.m_Aspects = aspect,
			},
		};
	}

	[[nodiscard]] constexpr inline RHITextureViewDesc MakeRHITexture2DViewDesc(
		RHIFormat format,
		uint32_t mip = 0,
		uint32_t mipLevels = 1,
		RHITextureAspect aspect = RHITextureAspect::Color) noexcept
	{
		return RHITextureViewDesc{
			.m_Type = RHITextureViewType::ShaderResource,
			.m_Dimension = RHITextureViewDimension::Texture2D,
			.m_Format = format,
			.m_Subresources =
			{
				.m_BaseMip = mip,
				.m_MipCount = mipLevels,
				.m_Aspects = aspect,
			},
		};
	}
}
