#pragma once
#include "Core/Hash/FNV1a.h"
#include "Core/TypedIndex.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class RHIDevice;

	GGLAB_DEFINE_TYPED_INDEX(RGTransientResourcePoolSlot, uint32_t);

	struct RGTransientTextureKey
	{
		RHITextureDimension m_Dimension = RHITextureDimension::Texture2D;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		uint16_t m_SampleCount = 1;
		RHIFormat m_Format = RHIFormat::Unknown;
		RHITextureUsage m_Usage = RHITextureUsage::None;

		bool operator==(const RGTransientTextureKey& rhs) const noexcept
		{
			return m_Dimension == rhs.m_Dimension &&
				m_Extent.m_Width == rhs.m_Extent.m_Width &&
				m_Extent.m_Height == rhs.m_Extent.m_Height &&
				m_Extent.m_Depth == rhs.m_Extent.m_Depth &&
				m_ArraySize == rhs.m_ArraySize &&
				m_MipLevels == rhs.m_MipLevels &&
				m_SampleCount == rhs.m_SampleCount &&
				m_Format == rhs.m_Format &&
				m_Usage == rhs.m_Usage;
		}

		auto AsTuple() const noexcept
		{
			return std::tie(m_Dimension,
				m_Extent.m_Width, m_Extent.m_Height, m_Extent.m_Depth,
				m_ArraySize, m_MipLevels, m_SampleCount, m_Format, m_Usage);
		}
	};

	struct RGTransientBufferKey
	{
		uint64_t m_SizeInBytes = 0;
		uint32_t m_StrideInBytes = 0;
		RHIBufferUsage m_Usage = RHIBufferUsage::None;

		bool operator==(const RGTransientBufferKey&) const noexcept = default;

		auto AsTuple() const noexcept
		{
			return std::tie(m_SizeInBytes, m_StrideInBytes, m_Usage);
		}
	};

	struct RGPhysicalTextureAllocation
	{
		RGPhysicalTextureAllocation() noexcept = default;
		RGPhysicalTextureAllocation(
			RHITextureHandle texture,
			RGTransientResourcePoolSlot poolSlot,
			const RGTransientTextureKey& key) noexcept :
			m_Texture(texture),
			m_PoolSlot(poolSlot),
			m_Key(key)
		{}

		GGLAB_DELETE_COPYABLE(RGPhysicalTextureAllocation);

		RGPhysicalTextureAllocation(RGPhysicalTextureAllocation&& rhs) noexcept
		{
			*this = std::move(rhs);
		}

		RGPhysicalTextureAllocation& operator=(RGPhysicalTextureAllocation&& rhs) noexcept
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
		RGTransientResourcePoolSlot m_PoolSlot{};
		RGTransientTextureKey m_Key{};

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

	struct RGPhysicalBufferAllocation
	{
		RGPhysicalBufferAllocation() noexcept = default;
		RGPhysicalBufferAllocation(
			RHIBufferHandle buffer,
			RGTransientResourcePoolSlot poolSlot,
			const RGTransientBufferKey& key) noexcept :
			m_Buffer(buffer),
			m_PoolSlot(poolSlot),
			m_Key(key)
		{}

		GGLAB_DELETE_COPYABLE(RGPhysicalBufferAllocation);

		RGPhysicalBufferAllocation(RGPhysicalBufferAllocation&& rhs) noexcept
		{
			*this = std::move(rhs);
		}

		RGPhysicalBufferAllocation& operator=(RGPhysicalBufferAllocation&& rhs) noexcept
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
		RGTransientResourcePoolSlot m_PoolSlot{};
		RGTransientBufferKey m_Key{};

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

	class RGTransientResourcePool
	{
	private:
		enum class ResourceType : uint8_t
		{
			Texture,
			Buffer
		};

		using TextureKeyHash = KeyHash<RGTransientTextureKey>;
		using BufferKeyHash = KeyHash<RGTransientBufferKey>;

		struct TextureRecord
		{
			RHITextureHandle m_Texture;
			RGTransientTextureKey m_Key{};
		};

		struct BufferRecord
		{
			RHIBufferHandle m_Buffer;
			RGTransientBufferKey m_Key{};
		};

		struct PendingRetirement
		{
			ResourceType m_Type = ResourceType::Texture;
			RGTransientResourcePoolSlot m_PoolSlot{};
			RHIFencePoint m_FencePoint{};
		};

	public:
		explicit RGTransientResourcePool(RHIDevice* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RGTransientResourcePool);
		~RGTransientResourcePool() noexcept;

		[[nodiscard]] RGPhysicalTextureAllocation AcquireTexture(const RHITextureDesc& desc) noexcept;
		[[nodiscard]] RGPhysicalBufferAllocation AcquireBuffer(const RHIBufferDesc& desc) noexcept;

		void RetireTexture(RGPhysicalTextureAllocation&& allocation, const RHIFencePoint& fencePoint) noexcept;
		void RetireBuffer(RGPhysicalBufferAllocation&& allocation, const RHIFencePoint& fencePoint) noexcept;

		void Tick() noexcept;

		void TrimPerKey(uint32_t maxCachedPerKey) noexcept;

		[[nodiscard]] bool IsCompatibleTexture(
			const RGPhysicalTextureAllocation& allocation,
			const RHITextureDesc& desc) const noexcept;

		[[nodiscard]] static RGTransientTextureKey MakeTextureKey(const RHITextureDesc& desc) noexcept;
		[[nodiscard]] static RGTransientBufferKey MakeBufferKey(const RHIBufferDesc& desc) noexcept;

	private:
		static std::optional<RHIClearValue> DefaultClearValue(const RHITextureDesc& desc) noexcept;
		void DestroyTexture(RGTransientResourcePoolSlot poolSlot) noexcept;
		void DestroyBuffer(RGTransientResourcePoolSlot poolSlot) noexcept;

		[[nodiscard]] RGPhysicalTextureAllocation CreateTexture(const RHITextureDesc& textureDesc) noexcept;
		[[nodiscard]] RGPhysicalBufferAllocation CreateBuffer(const RHIBufferDesc& bufferDesc) noexcept;

	private:
		RHIDevice* m_Device = nullptr;

		std::vector<TextureRecord> m_Textures;
		std::vector<BufferRecord> m_Buffers;

		std::unordered_map<RGTransientTextureKey, std::deque<RGTransientResourcePoolSlot>, TextureKeyHash> m_FreeTextures;
		std::unordered_map<RGTransientBufferKey, std::deque<RGTransientResourcePoolSlot>, BufferKeyHash> m_FreeBuffers;

		std::deque<PendingRetirement> m_PendingRetirements;

		uint32_t m_MaxCachedPerKey = 8;
	};
}
