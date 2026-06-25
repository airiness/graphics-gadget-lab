#pragma once
#include "Core/Hash/FNV1a.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <tuple>

namespace gglab
{
	template<typename Tag>
	class RHIHandle
	{
	public:
		using IndexType = uint32_t;
		using GenerationType = uint32_t;

		static constexpr IndexType InvalidIndex = std::numeric_limits<IndexType>::max();
		static constexpr GenerationType InvalidGeneration = 0;

		constexpr RHIHandle() noexcept = default;
		constexpr RHIHandle(IndexType index, GenerationType generation) noexcept :
			m_Index(index),
			m_Generation(generation)
		{}

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Index != InvalidIndex && m_Generation != InvalidGeneration;
		}

		constexpr void Reset() noexcept
		{
			m_Index = InvalidIndex;
			m_Generation = InvalidGeneration;
		}

		[[nodiscard]] constexpr IndexType Index() const noexcept { return m_Index; }
		[[nodiscard]] constexpr GenerationType Generation() const noexcept { return m_Generation; }

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::make_tuple(m_Index, m_Generation);
		}

		friend constexpr auto operator<=>(const RHIHandle&, const RHIHandle&) noexcept = default;

	private:
		IndexType m_Index = InvalidIndex;
		GenerationType m_Generation = InvalidGeneration;
	};

	struct RHITextureHandleTag {};
	struct RHIBufferHandleTag {};
	struct RHISamplerHandleTag {};
	struct RHIPipelineHandleTag {};
	struct RHIBindingLayoutHandleTag {};
	struct RHIRootSignatureHandleTag {};
	struct RHIShaderHandleTag {};
	struct RHITextureViewHandleTag {};
	struct RHIBufferViewHandleTag {};
	struct RHIFenceHandleTag {};
	struct RHICommandContextHandleTag {};

	using RHITextureHandle = RHIHandle<RHITextureHandleTag>;
	using RHIBufferHandle = RHIHandle<RHIBufferHandleTag>;
	using RHISamplerHandle = RHIHandle<RHISamplerHandleTag>;
	using RHIPipelineHandle = RHIHandle<RHIPipelineHandleTag>;
	using RHIBindingLayoutHandle = RHIHandle<RHIBindingLayoutHandleTag>;
	using RHIRootSignatureHandle = RHIHandle<RHIRootSignatureHandleTag>;
	using RHIShaderHandle = RHIHandle<RHIShaderHandleTag>;
	using RHITextureViewHandle = RHIHandle<RHITextureViewHandleTag>;
	using RHIBufferViewHandle = RHIHandle<RHIBufferViewHandleTag>;
	using RHIFenceHandle = RHIHandle<RHIFenceHandleTag>;
	using RHICommandContextHandle = RHIHandle<RHICommandContextHandleTag>;
}

namespace std
{
	template<typename Tag>
	struct hash<gglab::RHIHandle<Tag>>
	{
		size_t operator()(const gglab::RHIHandle<Tag>& handle) const noexcept
		{
			return gglab::KeyHash<gglab::RHIHandle<Tag>>{}(handle);
		}
	};
}
