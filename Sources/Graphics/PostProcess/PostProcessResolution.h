#pragma once
#include "Graphics/RenderGraph/RenderGraph.h"

#include <cstdint>

namespace gglab
{
	enum class PostProcessResolutionScale : uint32_t
	{
		Full = 1,
		Half = 2,
		Quarter = 4,
		Eighth = 8,
		Sixteenth = 16,
		ThirtySecond = 32,
	};

	[[nodiscard]] inline RHITextureDesc MakeRelativeTextureDesc(
		const RenderGraph::RGBuilder& builder,
		RGTextureId source,
		PostProcessResolutionScale scale,
		RHIFormat format = RHIFormat::Unknown) noexcept
	{
		const RHITextureDesc& sourceDesc = builder.GetTextureDesc(source);
		const uint32_t divisor = static_cast<uint32_t>(scale);
		GGLAB_ASSERT_MSG(divisor > 0, "Post-process resolution divisor must be non-zero.");

		RHITextureDesc result{};
		result.m_Dimension = RHITextureDimension::Texture2D;
		result.m_Format = format == RHIFormat::Unknown ? sourceDesc.m_Format : format;
		result.m_Extent = {
			std::max(1u, (sourceDesc.m_Extent.m_Width + divisor - 1u) / divisor),
			std::max(1u, (sourceDesc.m_Extent.m_Height + divisor - 1u) / divisor),
			1u,
		};
		return result;
	}
}
