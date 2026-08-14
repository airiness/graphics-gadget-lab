#pragma once
#include "Graphics/RHI/RHITexture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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

	// Canonical subresource interpretation shared by RenderGraph content validity,
	// barrier planning and any other texture subresource consumer. Aspect order is
	// fixed as Color, Depth, Stencil; a subresource index is
	// mip + mipCount * (arraySlice + arraySize * aspectIndex).
	inline constexpr std::array<RHITextureAspect, 3> RHITextureAspectOrder = {
		RHITextureAspect::Color,
		RHITextureAspect::Depth,
		RHITextureAspect::Stencil,
	};

	[[nodiscard]] constexpr uint32_t GetRHITextureAspectCount(const RHITextureDesc& desc) noexcept
	{
		uint32_t count = 0;
		const RHITextureAspect aspects = GetRHITextureAspects(desc);
		for (const RHITextureAspect aspect : RHITextureAspectOrder)
		{
			count += Test(aspects, aspect) ? 1u : 0u;
		}
		return count;
	}

	[[nodiscard]] constexpr uint32_t GetRHITextureAspectIndex(
		const RHITextureDesc& desc, RHITextureAspect target) noexcept
	{
		uint32_t index = 0;
		const RHITextureAspect aspects = GetRHITextureAspects(desc);
		for (const RHITextureAspect aspect : RHITextureAspectOrder)
		{
			if (!Test(aspects, aspect))
			{
				continue;
			}
			if (aspect == target)
			{
				return index;
			}
			++index;
		}
		return std::numeric_limits<uint32_t>::max();
	}

	[[nodiscard]] constexpr RHITextureAspect GetRHITextureAspectAt(
		const RHITextureDesc& desc, uint32_t targetIndex) noexcept
	{
		uint32_t index = 0;
		const RHITextureAspect aspects = GetRHITextureAspects(desc);
		for (const RHITextureAspect aspect : RHITextureAspectOrder)
		{
			if (!Test(aspects, aspect))
			{
				continue;
			}
			if (index == targetIndex)
			{
				return aspect;
			}
			++index;
		}
		return RHITextureAspect::None;
	}

	[[nodiscard]] constexpr uint32_t GetRHITextureSubresourceIndex(const RHITextureDesc& desc,
		uint32_t mip, uint32_t arraySlice, RHITextureAspect aspect) noexcept
	{
		const uint32_t mipLevels = GetRHITextureMipLevelCount(desc);
		const uint32_t arraySize = GetRHITextureArraySize(desc);
		return mip + mipLevels * (arraySlice + arraySize * GetRHITextureAspectIndex(desc, aspect));
	}

	[[nodiscard]] constexpr uint32_t GetRHITextureSubresourceCount(const RHITextureDesc& desc) noexcept
	{
		return GetRHITextureMipLevelCount(desc) * GetRHITextureArraySize(desc) *
			GetRHITextureAspectCount(desc);
	}

	template <typename Func>
	constexpr void ForEachRHITextureSubresource(const RHITextureDesc& desc,
		const RHISubresourceRange& normalizedRange, Func&& function) noexcept
	{
		for (const RHITextureAspect aspect : RHITextureAspectOrder)
		{
			if (!Test(normalizedRange.m_Aspects, aspect))
			{
				continue;
			}
			for (uint32_t arraySlice = normalizedRange.m_BaseArraySlice;
				arraySlice < normalizedRange.m_BaseArraySlice + normalizedRange.m_ArraySliceCount;
				++arraySlice)
			{
				for (uint32_t mip = normalizedRange.m_BaseMip;
					mip < normalizedRange.m_BaseMip + normalizedRange.m_MipCount; ++mip)
				{
					function(mip, arraySlice, aspect);
				}
			}
		}
	}
}
