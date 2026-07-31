#pragma once
#include "Graphics/RHI/RHITexture.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace gglab
{
	[[nodiscard]] constexpr uint32_t ResolveSubresourceCount(
		uint32_t base, uint32_t count, uint32_t total) noexcept
	{
		if (base >= total)
		{
			return 0;
		}

		const uint32_t remaining = total - base;
		return count == RHISubresourceRange::Remaining ? remaining : std::min(count, remaining);
	}

	[[nodiscard]] constexpr uint32_t GetRHITextureMipLevelCount(const RHITextureDesc& desc) noexcept
	{
		return std::max<uint32_t>(desc.m_MipLevels, 1u);
	}

	[[nodiscard]] constexpr uint32_t GetRHITextureArraySize(const RHITextureDesc& desc) noexcept
	{
		return desc.m_Dimension == RHITextureDimension::Texture3D
			? 1u
			: std::max<uint32_t>(desc.m_ArraySize, 1u);
	}

	[[nodiscard]] constexpr RHISubresourceRange NormalizeTextureSubresourceRange(
		const RHITextureDesc& desc,
		const std::optional<RHISubresourceRange>& requested = std::nullopt) noexcept
	{
		RHISubresourceRange range = requested.value_or(RHISubresourceRange{});
		range.m_MipCount = ResolveSubresourceCount(
			range.m_BaseMip, range.m_MipCount, GetRHITextureMipLevelCount(desc));
		range.m_ArraySliceCount = ResolveSubresourceCount(
			range.m_BaseArraySlice, range.m_ArraySliceCount, GetRHITextureArraySize(desc));
		range.m_Aspects &= GetRHITextureAspects(desc);
		return range;
	}
}
