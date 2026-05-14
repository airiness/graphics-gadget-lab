#pragma once
#include "Graphics/DX12/DX12Texture.h"
#include "Graphics/DX12/DX12Buffer.h"
#include "Graphics/DX12/DX12FencePoint.h"
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/GraphicsTypes.h"
#include "Core/Hash/FNV1a.h"

namespace gglab
{
	class DX12Device;
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
			uint32_t m_Width = 0;;
			uint32_t m_Height = 0;
			uint16_t m_ArraySize = 1;
			uint16_t m_MipLevels = 1;
			uint16_t m_SampleCount = 1;
			DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
			D3D12_RESOURCE_FLAGS m_Flags = D3D12_RESOURCE_FLAG_NONE;

			bool operator==(const TextureKey&) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_Width, m_Height, m_ArraySize, m_MipLevels, m_SampleCount, m_Format, m_Flags);
			}

			static TextureKey ConvertResourceDescToKey(D3D12_RESOURCE_DESC resDesc) noexcept
			{
				TextureKey texKey
				{
					.m_Width = static_cast<uint32_t>(resDesc.Width),
					.m_Height = static_cast<uint32_t>(resDesc.Height),
					.m_ArraySize = static_cast<uint16_t>(resDesc.DepthOrArraySize),
					.m_MipLevels = static_cast<uint16_t>(resDesc.MipLevels),
					.m_SampleCount = static_cast<uint16_t>(resDesc.SampleDesc.Count),
					.m_Format = resDesc.Format,
					.m_Flags = resDesc.Flags
				};
				return texKey;
			}
		};
		using TextureKeyHash = KeyHash<TextureKey>;

		struct BufferKey
		{
			uint64_t m_SizeInBytes = 0;
			D3D12_RESOURCE_FLAGS m_Flags = D3D12_RESOURCE_FLAG_NONE;

			bool operator==(const BufferKey&) const noexcept = default;

			auto AsTuple() const noexcept
			{
				return std::tie(m_SizeInBytes, m_Flags);
			}

			static BufferKey ConvertResourceDescToKey(D3D12_RESOURCE_DESC resDesc) noexcept
			{
				BufferKey bufKey
				{
					.m_SizeInBytes = static_cast<uint64_t>(resDesc.Width),
					.m_Flags = resDesc.Flags
				};
				return bufKey;
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

		struct Pending
		{
			Type m_Type;
			ResourceIndex m_Index;
			DX12FencePoint m_FencePoint;
		};

	public:
		explicit RGGpuResourceAllocator(DX12Device* dx12device) noexcept;
		GGLAB_DEFAULT_COPYABLE_MOVABLE(RGGpuResourceAllocator);
		~RGGpuResourceAllocator() = default;

		template<typename ResourceDesc>
		ResourceIndex Acquire(const ResourceDesc& rgResourceDesc,
			D3D12_RESOURCE_STATES initStates = D3D12_RESOURCE_STATE_COMMON,
			std::optional<D3D12_CLEAR_VALUE> clearValue = std::nullopt) noexcept = delete;

		void ReleaseTexture(ResourceIndex texIndex, const DX12FencePoint& fencePoint) noexcept;
		void ReleaseBuffer(ResourceIndex bufIndex, const DX12FencePoint& fencePoint) noexcept;

		DX12Texture* GetTexture(ResourceIndex texIndex) const noexcept;
		DX12Buffer* GetBuffer(ResourceIndex bufIndex) const noexcept;

		void Tick() noexcept;

		void TrimPerKey(uint32_t maxCachedPerKey) noexcept;

		// Check Texture ResourceIndex is compatible with RGTextureDesc, for texture recreate.
		bool IsCompatibleTexture(ResourceIndex texIndex, const RGTextureDesc& desc) const noexcept;

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

		std::unordered_map<TextureKey, std::deque<ResourceIndex>, TextureKeyHash> m_FreeTextures;
		std::unordered_map<BufferKey, std::deque<ResourceIndex>, BufferKeyHash> m_FreeBuffers;

		std::deque<Pending> m_Pending;

		uint32_t m_MaxCachedPerKey = 8;
	};

	template<>
	inline ResourceIndex RGGpuResourceAllocator::Acquire<RGTextureDesc>(
		const RGTextureDesc& rgTexDesc,
		D3D12_RESOURCE_STATES initStates,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		const auto texKey = MakeKey(rgTexDesc);

		if (auto iter = m_FreeTextures.find(texKey);
			iter != m_FreeTextures.end() && !iter->second.empty())
		{
			const auto texIndex = iter->second.back();
			iter->second.pop_back();

			return texIndex;
		}

		return CreateTexture(rgTexDesc, initStates, clearValue);
	}

	template<>
	inline ResourceIndex RGGpuResourceAllocator::Acquire<RGBufferDesc>(
		const RGBufferDesc& rgBufDesc,
		D3D12_RESOURCE_STATES initStates,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		const auto bufKey = MakeKey(rgBufDesc);
		if (auto iter = m_FreeBuffers.find(bufKey);
			iter != m_FreeBuffers.end() && !iter->second.empty())
		{
			const auto bufIndex = iter->second.back();
			iter->second.pop_back();

			return bufIndex;
		}

		return CreateBuffer(rgBufDesc, initStates);
	}
}