#pragma once
#include "DX12Texture.h"
#include "DX12Buffer.h"
#include "DX12FencePoint.h"
#include "RGResource.h"
#include "FNV1a.h"

namespace gglab
{
	class DX12Device;
	class RGGpuResAllocator
	{
	public:
		using ResourceIndex = int32_t;
		static constexpr ResourceIndex InvalidResourceIndex = static_cast<ResourceIndex>(-1);

	private:
		enum class Type : uint8_t
		{
			Texture,
			Buffer
		};

		struct TextureKey
		{
			uint32_t m_Width = 0;;
			uint32_t m_Height = 0;
			uint16_t m_ArraySize = 1;
			uint16_t m_MipLevels = 1;
			uint16_t m_SampleCount = 1;
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			D3D12_RESOURCE_FLAGS m_Flags = D3D12_RESOURCE_FLAG_NONE;

			bool operator==(const TextureKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Width, m_Height, m_ArraySize, m_MipLevels, m_SampleCount, m_Format, m_Flags);
			}
		};
		using TextureKeyHash = KeyHash<TextureKey>;

		struct BufferKey 
		{
			uint64_t m_SizeInBytes = 0;
			D3D12_RESOURCE_FLAGS m_Flags = D3D12_RESOURCE_FLAG_NONE;

			bool operator==(const BufferKey& other) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_SizeInBytes, m_Flags);
			}
		};
		using BufferKeyHash = KeyHash<BufferKey>;

		static TextureKey MakeKey(const RGTextureDesc& desc) noexcept
		{
			return TextureKey
			{
				.m_Width = desc.m_Width,
				.m_Height = desc.m_Height,
				.m_ArraySize = desc.m_ArraySize,
				.m_MipLevels = desc.m_MipLevels,
				.m_SampleCount = desc.m_SampleCount,
				.m_Format = desc.m_Format,
				.m_Flags = ToD3D12ResourceFlags(desc.m_Usage)
			};
		}

		static BufferKey MakeKey(const RGBufferDesc& desc) noexcept
		{
			return BufferKey
			{
				.m_SizeInBytes = desc.m_SizeInBytes,
				.m_Flags = ToD3D12ResourceFlags(desc.m_Usage)
			};
		}

		struct FreeItem
		{
			ResourceIndex m_Index = InvalidResourceIndex;
			uint64_t m_LastUsedFrame = 0;
		};

		struct Pending
		{
			Type m_Type;
			ResourceIndex m_Index = 0;
			DX12FencePoint m_FencePoint;
		};

	public:
		explicit RGGpuResAllocator(DX12Device* dx12device) noexcept;
		~RGGpuResAllocator() = default;

		template<typename ResourceDesc>
		ResourceIndex AcquireResource(const ResourceDesc& rgResourceDesc,
			D3D12_RESOURCE_STATES initStates = D3D12_RESOURCE_STATE_COMMON,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept = delete;

		template<typename ResourceDesc>
		void ReleaseResource(ResourceIndex resourceIndex, 
			D3D12_RESOURCE_STATES lastKnownStates, 
			DX12FencePoint fencePoint) noexcept = delete;

		DX12Texture* GetTexture(ResourceIndex texIndex) const noexcept;
		DX12Buffer* GetBuffer(ResourceIndex bufIndex) const noexcept;

		void Tick(DX12FencePoint fencePoint) noexcept;

		void TrimPerKey(uint32_t maxCachedPerKey) noexcept;

	private:
		ResourceIndex CreateTexture(const RGTextureDesc& rgTexDesc,
			D3D12_RESOURCE_STATES initStates,
			std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept;

		ResourceIndex CreateBuffer(const RGBufferDesc& rgBufDesc,
			D3D12_RESOURCE_STATES initStates) noexcept;


	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12MA::Allocator* m_Allocator = nullptr;

		std::vector<std::unique_ptr<DX12Texture>> m_Textures;
		std::vector<std::unique_ptr<DX12Buffer>> m_Buffers;

		std::unordered_map<TextureKey, std::deque<FreeItem>, TextureKeyHash> m_FreeTextures;
		std::unordered_map<BufferKey, std::deque<FreeItem>, BufferKeyHash> m_FreeBuffers;

		std::deque<Pending> m_Pendings;
	};

	template<>
	inline RGGpuResAllocator::ResourceIndex RGGpuResAllocator::AcquireResource<RGTextureDesc>(
		const RGTextureDesc& rgTexDesc,
		D3D12_RESOURCE_STATES initStates,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		const auto texKey = MakeKey(rgTexDesc);
		auto iter = m_FreeTextures.find(texKey);
		if (iter != m_FreeTextures.end() && !iter->second.empty())
		{
			FreeItem freeItem = iter->second.back();
			return freeItem.m_Index;
		}

		return CreateTexture(rgTexDesc, initStates, clearValue);
	}

	template<>
	inline RGGpuResAllocator::ResourceIndex RGGpuResAllocator::AcquireResource<RGBufferDesc>(
		const RGBufferDesc& rgBufDesc,
		D3D12_RESOURCE_STATES initStates,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		const auto bufKey = MakeKey(rgBufDesc);
		auto iter = m_FreeBuffers.find(bufKey);
		if (iter != m_FreeBuffers.end() && !iter->second.empty())
		{
			FreeItem freeItem = iter->second.back();
			return freeItem.m_Index;
		}

		return CreateBuffer(rgBufDesc, initStates);
	}

	template<>
	inline void RGGpuResAllocator::ReleaseResource<RGTextureDesc>(ResourceIndex texIndex, 
		D3D12_RESOURCE_STATES lastKnownStates, 
		DX12FencePoint fencePoint) noexcept
	{
		if (texIndex < 0 || static_cast<size_t>(texIndex) >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResAllocator::ReleaseResource() : Release Invalid texture.");
			return;
		}

		auto* tex = m_Textures[texIndex].get();

		// TODO: last know state
		m_Pendings.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Texture,
					.m_Index = texIndex,
					.m_FencePoint = fencePoint,
				}));
	}

	template<>
	inline void RGGpuResAllocator::ReleaseResource<RGBufferDesc>(ResourceIndex bufIndex,
		D3D12_RESOURCE_STATES lastKnownStates,
		DX12FencePoint fencePoint) noexcept
	{
		if (bufIndex < 0 || static_cast<size_t>(bufIndex) >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResAllocator::ReleaseResource() : Release Invalid buffer.");
			return;
		}

		auto* buf = m_Buffers[bufIndex].get();

		// TODO: last know state
		m_Pendings.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Texture,
					.m_Index = bufIndex,
					.m_FencePoint = fencePoint,
				}));
	}

}