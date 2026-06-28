#pragma once
#include "Core/Hash/FNV1a.h"
#include "Core/TypedIndex.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class RHIDevice;
	class TransientResourcePool;
	struct TransientResourcePoolSnapshot;
	void BuildTransientResourcePoolSnapshot(
		const TransientResourcePool& pool,
		TransientResourcePoolSnapshot& outSnapshot) noexcept;

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
		std::optional<RHIClearValue> m_ClearValue = std::nullopt;

		bool operator==(const TransientTextureKey& rhs) const noexcept
		{
			return m_Dimension == rhs.m_Dimension &&
				m_Extent.m_Width == rhs.m_Extent.m_Width &&
				m_Extent.m_Height == rhs.m_Extent.m_Height &&
				m_Extent.m_Depth == rhs.m_Extent.m_Depth &&
				m_ArraySize == rhs.m_ArraySize &&
				m_MipLevels == rhs.m_MipLevels &&
				m_SampleCount == rhs.m_SampleCount &&
				m_Format == rhs.m_Format &&
				m_Usage == rhs.m_Usage &&
				ClearValuesEqual(m_ClearValue, rhs.m_ClearValue);
		}

		auto AsTuple() const noexcept
		{
			const RHIClearValue clearValue = m_ClearValue.value_or(RHIClearValue{});
			return std::make_tuple(m_Dimension,
				m_Extent.m_Width, m_Extent.m_Height, m_Extent.m_Depth,
				m_ArraySize, m_MipLevels, m_SampleCount, m_Format, m_Usage,
				static_cast<uint8_t>(m_ClearValue.has_value()), clearValue.m_Format,
				clearValue.m_Color[0], clearValue.m_Color[1],
				clearValue.m_Color[2], clearValue.m_Color[3],
				clearValue.m_Depth, clearValue.m_Stencil,
				static_cast<uint8_t>(clearValue.m_IsDepthStencil));
		}

	private:
		static bool ClearValuesEqual(
			const std::optional<RHIClearValue>& lhs,
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

			return lhs->m_Format == rhs->m_Format &&
				lhs->m_Color[0] == rhs->m_Color[0] &&
				lhs->m_Color[1] == rhs->m_Color[1] &&
				lhs->m_Color[2] == rhs->m_Color[2] &&
				lhs->m_Color[3] == rhs->m_Color[3] &&
				lhs->m_Depth == rhs->m_Depth &&
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

	struct TransientTextureAllocation
	{
		TransientTextureAllocation() noexcept = default;
		TransientTextureAllocation(
			RHITextureHandle texture,
			TransientResourcePoolSlot poolSlot,
			const TransientTextureKey& key) noexcept :
			m_Texture(texture),
			m_PoolSlot(poolSlot),
			m_Key(key)
		{}

		GGLAB_DELETE_COPYABLE(TransientTextureAllocation);

		TransientTextureAllocation(TransientTextureAllocation&& rhs) noexcept
		{
			*this = std::move(rhs);
		}

		TransientTextureAllocation& operator=(TransientTextureAllocation&& rhs) noexcept
		{
			if (this != &rhs)
			{
				m_Texture = rhs.m_Texture;
				m_PoolSlot = rhs.m_PoolSlot;
				m_Key = rhs.m_Key;
				rhs.Reset();
			}
			return *this;
		}

		RHITextureHandle m_Texture{};
		TransientResourcePoolSlot m_PoolSlot{};
		TransientTextureKey m_Key{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Texture.IsValid() && m_PoolSlot.IsValid();
		}

		void Reset() noexcept
		{
			m_Texture.Reset();
			m_PoolSlot.Reset();
			m_Key = {};
		}
	};

	struct TransientBufferAllocation
	{
		TransientBufferAllocation() noexcept = default;
		TransientBufferAllocation(
			RHIBufferHandle buffer,
			TransientResourcePoolSlot poolSlot,
			const TransientBufferKey& key) noexcept :
			m_Buffer(buffer),
			m_PoolSlot(poolSlot),
			m_Key(key)
		{}

		GGLAB_DELETE_COPYABLE(TransientBufferAllocation);

		TransientBufferAllocation(TransientBufferAllocation&& rhs) noexcept
		{
			*this = std::move(rhs);
		}

		TransientBufferAllocation& operator=(TransientBufferAllocation&& rhs) noexcept
		{
			if (this != &rhs)
			{
				m_Buffer = rhs.m_Buffer;
				m_PoolSlot = rhs.m_PoolSlot;
				m_Key = rhs.m_Key;
				rhs.Reset();
			}
			return *this;
		}

		RHIBufferHandle m_Buffer{};
		TransientResourcePoolSlot m_PoolSlot{};
		TransientBufferKey m_Key{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Buffer.IsValid() && m_PoolSlot.IsValid();
		}

		void Reset() noexcept
		{
			m_Buffer.Reset();
			m_PoolSlot.Reset();
			m_Key = {};
		}
	};

	class TransientResourcePool
	{
	private:
		friend void BuildTransientResourcePoolSnapshot(
			const TransientResourcePool& pool,
			TransientResourcePoolSnapshot& outSnapshot) noexcept;

		enum class ResourceType : uint8_t
		{
			Texture,
			Buffer
		};

		using TextureKeyHash = KeyHash<TransientTextureKey>;
		using BufferKeyHash = KeyHash<TransientBufferKey>;

		struct TextureRecord
		{
			RHITextureHandle m_Texture;
			TransientTextureKey m_Key{};
		};

		struct BufferRecord
		{
			RHIBufferHandle m_Buffer;
			TransientBufferKey m_Key{};
		};

		struct PendingRetirement
		{
			ResourceType m_Type = ResourceType::Texture;
			TransientResourcePoolSlot m_PoolSlot{};
			RHIFencePoint m_FencePoint{};
		};

	public:
		explicit TransientResourcePool(RHIDevice* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TransientResourcePool);
		~TransientResourcePool() noexcept;

		[[nodiscard]] TransientTextureAllocation AcquireTexture(const RHITextureDesc& desc) noexcept;
		[[nodiscard]] TransientBufferAllocation AcquireBuffer(const RHIBufferDesc& desc) noexcept;

		void RetireTexture(TransientTextureAllocation&& allocation, const RHIFencePoint& fencePoint) noexcept;
		void RetireBuffer(TransientBufferAllocation&& allocation, const RHIFencePoint& fencePoint) noexcept;

		void Tick() noexcept;

		void TrimPerKey(uint32_t maxCachedPerKey) noexcept;

		[[nodiscard]] bool IsCompatibleTexture(
			const TransientTextureAllocation& allocation,
			const RHITextureDesc& desc) const noexcept;

		[[nodiscard]] static TransientTextureKey MakeTextureKey(const RHITextureDesc& desc) noexcept;
		[[nodiscard]] static TransientBufferKey MakeBufferKey(const RHIBufferDesc& desc) noexcept;

	private:
		static std::optional<RHIClearValue> DefaultClearValue(const RHITextureDesc& desc) noexcept;
		void DestroyTexture(TransientResourcePoolSlot poolSlot) noexcept;
		void DestroyBuffer(TransientResourcePoolSlot poolSlot) noexcept;

		[[nodiscard]] TransientTextureAllocation CreateTexture(const RHITextureDesc& textureDesc) noexcept;
		[[nodiscard]] TransientBufferAllocation CreateBuffer(const RHIBufferDesc& bufferDesc) noexcept;

	private:
		RHIDevice* m_Device = nullptr;

		std::vector<TextureRecord> m_Textures;
		std::vector<BufferRecord> m_Buffers;

		std::unordered_map<TransientTextureKey, std::deque<TransientResourcePoolSlot>, TextureKeyHash> m_FreeTextures;
		std::unordered_map<TransientBufferKey, std::deque<TransientResourcePoolSlot>, BufferKeyHash> m_FreeBuffers;

		std::deque<PendingRetirement> m_PendingRetirements;

		uint32_t m_MaxCachedPerKey = 8;
	};
}
