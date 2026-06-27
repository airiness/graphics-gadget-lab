#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/GraphicsTypes.h"
#include "Core/Hash/FNV1a.h"

namespace gglab
{
	class DX12Device;
	class DX12Texture;
	class DX12Buffer;
	class RHIDevice;
	class RGGpuResourceAllocator
	{
	private:
		enum class Type : uint8_t
		{
			Texture,
			Buffer
		};

		struct TextureKey
		{
			RHITextureDimension m_Dimension = RHITextureDimension::Texture2D;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_Depth = 0;
			uint16_t m_ArraySize = 1;
			uint16_t m_MipLevels = 1;
			uint16_t m_SampleCount = 1;
			RHIFormat m_Format = RHIFormat::Unknown;
			RHITextureUsage m_Usage = RHITextureUsage::None;

			bool operator==(const TextureKey&) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Dimension, m_Width, m_Height, m_Depth, m_ArraySize,
					m_MipLevels, m_SampleCount, m_Format, m_Usage);
			}
		};
		using TextureKeyHash = KeyHash<TextureKey>;

		struct BufferKey
		{
			uint64_t m_SizeInBytes = 0;
			uint32_t m_StrideInBytes = 0;
			RHIBufferUsage m_Usage = RHIBufferUsage::None;

			bool operator==(const BufferKey&) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_SizeInBytes, m_StrideInBytes, m_Usage);
			}
		};
		using BufferKeyHash = KeyHash<BufferKey>;

		static TextureKey MakeKey(const RHITextureDesc& desc) noexcept
		{
			return TextureKey
			{
				.m_Dimension = desc.m_Dimension,
				.m_Width = desc.m_Extent.m_Width,
				.m_Height = desc.m_Extent.m_Height,
				.m_Depth = desc.m_Extent.m_Depth,
				.m_ArraySize = desc.m_ArraySize,
				.m_MipLevels = desc.m_MipLevels,
				.m_SampleCount = desc.m_SampleCount,
				.m_Format = desc.m_Format,
				.m_Usage = desc.m_Usage
			};
		}

		static BufferKey MakeKey(const RHIBufferDesc& desc) noexcept
		{
			return BufferKey
			{
				.m_SizeInBytes = desc.m_SizeInBytes,
				.m_StrideInBytes = desc.m_StrideInBytes,
				.m_Usage = desc.m_Usage
			};
		}

		struct TextureRecord
		{
			RHITextureHandle m_Texture;
			TextureKey m_Key{};
		};

		struct BufferRecord
		{
			RHIBufferHandle m_Buffer;
			BufferKey m_Key{};
		};

		struct Pending
		{
			Type m_Type;
			ResourceIndex m_Index;
			DX12FencePoint m_FencePoint;
		};

	public:
		explicit RGGpuResourceAllocator(RHIDevice* device) noexcept;
		GGLAB_DEFAULT_COPYABLE_MOVABLE(RGGpuResourceAllocator);
		~RGGpuResourceAllocator() noexcept;

		template<typename ResourceDesc>
		ResourceIndex Acquire(const ResourceDesc& rgResourceDesc) noexcept = delete;

		void ReleaseTexture(ResourceIndex texIndex, const DX12FencePoint& fencePoint) noexcept;
		void ReleaseBuffer(ResourceIndex bufIndex, const DX12FencePoint& fencePoint) noexcept;

		DX12Texture* GetTexture(ResourceIndex texIndex) const noexcept;
		DX12Buffer* GetBuffer(ResourceIndex bufIndex) const noexcept;
		RHITextureHandle GetTextureHandle(ResourceIndex texIndex) noexcept;
		RHIBufferHandle GetBufferHandle(ResourceIndex bufIndex) noexcept;

		void Tick() noexcept;

		void TrimPerKey(uint32_t maxCachedPerKey) noexcept;

		// Check Texture ResourceIndex is compatible with RHITextureDesc, for texture recreate.
		bool IsCompatibleTexture(ResourceIndex texIndex, const RHITextureDesc& desc) const noexcept;

	private:
		static std::optional<RHIClearValue> DefaultRHIClearValue(const RHITextureDesc& desc) noexcept;
		void DestroyTextureHandle(ResourceIndex texIndex) noexcept;
		void DestroyBufferHandle(ResourceIndex bufIndex) noexcept;

		ResourceIndex CreateTexture(const RHITextureDesc& textureDesc) noexcept;

		ResourceIndex CreateBuffer(const RHIBufferDesc& bufferDesc) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		DX12Device* m_DX12Device = nullptr;

		std::vector<TextureRecord> m_Textures;
		std::vector<BufferRecord> m_Buffers;

		std::unordered_map<TextureKey, std::deque<ResourceIndex>, TextureKeyHash> m_FreeTextures;
		std::unordered_map<BufferKey, std::deque<ResourceIndex>, BufferKeyHash> m_FreeBuffers;

		std::deque<Pending> m_Pending;

		uint32_t m_MaxCachedPerKey = 8;
	};

	template<>
	inline ResourceIndex RGGpuResourceAllocator::Acquire<RHITextureDesc>(
		const RHITextureDesc& textureDesc) noexcept
	{
		const auto texKey = MakeKey(textureDesc);

		if (auto iter = m_FreeTextures.find(texKey);
			iter != m_FreeTextures.end() && !iter->second.empty())
		{
			const auto texIndex = iter->second.back();
			iter->second.pop_back();

			return texIndex;
		}

		return CreateTexture(textureDesc);
	}

	template<>
	inline ResourceIndex RGGpuResourceAllocator::Acquire<RHIBufferDesc>(
		const RHIBufferDesc& bufferDesc) noexcept
	{
		const auto bufKey = MakeKey(bufferDesc);
		if (auto iter = m_FreeBuffers.find(bufKey);
			iter != m_FreeBuffers.end() && !iter->second.empty())
		{
			const auto bufIndex = iter->second.back();
			iter->second.pop_back();

			return bufIndex;
		}

		return CreateBuffer(bufferDesc);
	}
}
