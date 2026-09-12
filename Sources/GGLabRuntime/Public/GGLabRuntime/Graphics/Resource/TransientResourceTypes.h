#pragma once
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <optional>
#include <tuple>

namespace gglab
{
	GGLAB_DEFINE_TYPED_INDEX(TransientResourcePoolSlot, uint32_t);

	struct TransientTextureKey
	{
		RHITextureDimension m_Dimension = RHITextureDimension::Texture2D;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		RHIFormat m_Format = RHIFormat::Unknown;
		RHITextureUsage m_Usage = RHITextureUsage::None;
		RHITextureCreateFlags m_CreateFlags = RHITextureCreateFlags::None;
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;

		bool operator==(const TransientTextureKey& rhs) const noexcept
		{
			return m_Dimension == rhs.m_Dimension && m_Extent.m_Width == rhs.m_Extent.m_Width &&
				m_Extent.m_Height == rhs.m_Extent.m_Height &&
				m_Extent.m_Depth == rhs.m_Extent.m_Depth && m_ArraySize == rhs.m_ArraySize &&
				m_MipLevels == rhs.m_MipLevels && m_SampleCount == rhs.m_SampleCount &&
				m_Format == rhs.m_Format && m_Usage == rhs.m_Usage &&
				m_CreateFlags == rhs.m_CreateFlags &&
				ClearValuesEqual(m_ClearValue, rhs.m_ClearValue);
		}

		auto AsTuple() const noexcept
		{
			const RHIClearValue clearValue = m_ClearValue.value_or(RHIClearValue{});
			return std::make_tuple(m_Dimension, m_Extent.m_Width, m_Extent.m_Height,
				m_Extent.m_Depth, m_ArraySize, m_MipLevels, m_SampleCount, m_Format, m_Usage,
				m_CreateFlags,
				static_cast<uint8_t>(m_ClearValue.has_value()), clearValue.m_Format,
				clearValue.m_Color[0], clearValue.m_Color[1], clearValue.m_Color[2],
				clearValue.m_Color[3], clearValue.m_Depth, clearValue.m_Stencil,
				static_cast<uint8_t>(clearValue.m_IsDepthStencil));
		}

	private:
		static bool ClearValuesEqual(const std::optional<RHIClearValue>& lhs,
			const std::optional<RHIClearValue>& rhs) noexcept
		{
			if (lhs.has_value() != rhs.has_value())
			{
				return false;
			}
			if (!lhs)
			{
				return true;
			}

			return lhs->m_Format == rhs->m_Format && lhs->m_Color[0] == rhs->m_Color[0] &&
				lhs->m_Color[1] == rhs->m_Color[1] && lhs->m_Color[2] == rhs->m_Color[2] &&
				lhs->m_Color[3] == rhs->m_Color[3] && lhs->m_Depth == rhs->m_Depth &&
				lhs->m_Stencil == rhs->m_Stencil &&
				lhs->m_IsDepthStencil == rhs->m_IsDepthStencil;
		}
	};

	struct TransientBufferKey
	{
		uint64_t m_SizeInBytes = 0;
		uint32_t m_StrideInBytes = 0;
		RHIBufferUsage m_Usage = RHIBufferUsage::None;
		RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::GpuOnly;

		bool operator==(const TransientBufferKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::tie(m_SizeInBytes, m_StrideInBytes, m_Usage, m_MemoryUsage);
		}
	};
}
