#pragma once
#include "Core/Hash/FNV1a.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <tuple>

namespace gglab
{
	enum class RHIDescriptorHeapType : uint8_t
	{
		CbvSrvUav,
		Sampler,
		RenderTarget,
		DepthStencil,
	};

	struct RHIDescriptorHandle
	{
		RHIDescriptorHeapType m_HeapType = RHIDescriptorHeapType::CbvSrvUav;
		uint32_t m_Index = RHITextureViewHandle::InvalidIndex;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Index != RHITextureViewHandle::InvalidIndex;
		}
	};

	struct RHITextureViewKey
	{
		RHITextureHandle m_Texture;
		RHITextureViewDesc m_Desc;

		bool operator==(const RHITextureViewKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Texture.Index(),
				m_Texture.Generation(),
				m_Desc.AsTuple());
		}
	};

	struct RHIBufferViewKey
	{
		RHIBufferHandle m_Buffer;
		RHIBufferViewDesc m_Desc;

		bool operator==(const RHIBufferViewKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::make_tuple(
				m_Buffer.Index(),
				m_Buffer.Generation(),
				m_Desc.AsTuple());
		}
	};

	using RHITextureViewKeyHash = KeyHash<RHITextureViewKey>;
	using RHIBufferViewKeyHash = KeyHash<RHIBufferViewKey>;
}
