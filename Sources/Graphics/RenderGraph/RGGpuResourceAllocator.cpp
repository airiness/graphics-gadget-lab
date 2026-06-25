#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGGpuResourceAllocator.h"
#include "Graphics/RHI/DX12/DX12Device.h"

namespace gglab
{
	RGGpuResourceAllocator::RGGpuResourceAllocator(DX12Device* dx12device) noexcept :
		m_DX12Device(dx12device),
		m_Allocator(dx12device ? dx12device->GetMemAllocator() : nullptr)
	{
		GGLAB_ASSERT_MSG(m_Allocator != nullptr, "Allocator must not be null.");
	}

	RGGpuResourceAllocator::~RGGpuResourceAllocator() noexcept
	{
		for (uint32_t index = 0; index < m_Textures.size(); ++index)
		{
			DestroyTextureHandle(ResourceIndex(index));
		}
	}

	void RGGpuResourceAllocator::ReleaseTexture(ResourceIndex texIndex, const DX12FencePoint& fencePoint) noexcept
	{
		if (texIndex.Value() < 0 || texIndex.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::ReleaseResource() : Release Invalid texture.");
			return;
		}

		// TODO: last know state
		m_Pending.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Texture,
					.m_Index = texIndex,
					.m_FencePoint = fencePoint,
				}));
	}

	void RGGpuResourceAllocator::ReleaseBuffer(ResourceIndex bufIndex, const DX12FencePoint& fencePoint) noexcept
	{
		if (bufIndex.Value() < 0 || bufIndex.Value() >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::ReleaseResource() : Release Invalid buffer.");
			return;
		}

		//auto* buf = m_Buffers[bufIndex].get();

		// TODO: last know state
		m_Pending.emplace_back(
			std::move(Pending
				{
					.m_Type = Type::Buffer,
					.m_Index = bufIndex,
					.m_FencePoint = fencePoint,
				}));
	}

	ResourceIndex RGGpuResourceAllocator::CreateTexture(const RGTextureDesc& rgTexDesc,
		D3D12_RESOURCE_STATES initStates,
		std::optional<D3D12_CLEAR_VALUE> clearValue) noexcept
	{
		GGLAB_UNUSED(initStates);
		GGLAB_UNUSED(clearValue);

		ResourceIndex index = ResourceIndex(static_cast<uint32_t>(m_Textures.size()));
		RHITextureDesc textureDesc{};
		textureDesc.m_Dimension = RHITextureDimension::Texture2D;
		textureDesc.m_Format = rgTexDesc.m_Format;
		textureDesc.m_Usage = ToRHITextureUsage(rgTexDesc.m_Usage);
		textureDesc.m_Extent =
		{
			.m_Width = rgTexDesc.m_Width,
			.m_Height = rgTexDesc.m_Height,
			.m_Depth = 1u,
		};
		textureDesc.m_ArraySize = rgTexDesc.m_ArraySize;
		textureDesc.m_MipLevels = rgTexDesc.m_MipLevels;
		textureDesc.m_SampleCount = rgTexDesc.m_SampleCount;
		textureDesc.m_DebugName = "RGGpuResourceAllocator.Texture";
		textureDesc.m_ClearValue = DefaultRHIClearValue(rgTexDesc);

		RHITextureHandle texture = m_DX12Device->CreateTexture(textureDesc);
		GGLAB_ASSERT_MSG(texture.IsValid(), "RGGpuResourceAllocator: CreateTexture failed.");
		m_Textures.emplace_back(texture);

		return index;
	}

	ResourceIndex RGGpuResourceAllocator::CreateBuffer(const RGBufferDesc& rgBufDesc,
		D3D12_RESOURCE_STATES initStates) noexcept
	{
		ResourceIndex index = ResourceIndex(static_cast<uint32_t>(m_Buffers.size()));
		m_Buffers.emplace_back(std::make_unique<DX12Buffer>());
		auto& buf = m_Buffers.back();

		DX12Resource::CreateInfo createInfo = {};
		createInfo.m_Allocator = m_Allocator;
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_InitStates = initStates;
		createInfo.m_ResourceDesc = ToD3D12ResourceDesc(rgBufDesc);
		buf->Create(createInfo);

		return index;
	}

	DX12Texture* RGGpuResourceAllocator::GetTexture(ResourceIndex texIndex) const noexcept
	{
		if (!texIndex.IsValid() || texIndex.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTexture() : Invalid texIndex.");
			return nullptr;
		}

		return m_DX12Device->ResolveTexture(m_Textures[texIndex.Value()]);
	}

	DX12Buffer* RGGpuResourceAllocator::GetBuffer(ResourceIndex bufIndex) const noexcept
	{
		if (!bufIndex.IsValid() || bufIndex.Value() >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetBuffer() : Invalid bufIndex.");
			return nullptr;
		}
		return m_Buffers[bufIndex.Value()].get();
	}

	RHITextureHandle RGGpuResourceAllocator::GetTextureHandle(ResourceIndex texIndex) noexcept
	{
		if (!texIndex.IsValid() || texIndex.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTextureHandle() : Invalid texIndex.");
			return {};
		}

		const RHITextureHandle handle = m_Textures[texIndex.Value()];
		if (handle.IsValid() && m_DX12Device->IsAlive(handle))
		{
			return handle;
		}

		GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTextureHandle() : Texture is not live.");
		return {};
	}

	void RGGpuResourceAllocator::Tick() noexcept
	{
		std::erase_if(m_Pending,
			[this](const Pending& pending)
			{
				const auto done = pending.m_FencePoint.IsCompleted();
				if (!done)
				{
					return false;
				}

				switch (pending.m_Type)
				{
				case Type::Texture:
				{
					auto* texture = GetTexture(pending.m_Index);
					if (!texture)
					{
						return true;
					}

					auto texKey = TextureKey::ConvertResourceDescToKey(texture->GetDesc());
					auto& list = m_FreeTextures[texKey];
					list.push_back(pending.m_Index);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						DestroyTextureHandle(releaseResIndex);
					}
					return true;
				}
				case Type::Buffer:
				{
					auto bufKey = BufferKey::ConvertResourceDescToKey(m_Buffers[pending.m_Index.Value()]->GetDesc());
					auto& list = m_FreeBuffers[bufKey];
					list.push_back(pending.m_Index);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						m_Buffers[releaseResIndex.Value()]->Release();
					}
					return true;
				}
				default:
					GGLAB_UNREACHABLE("switch error");
				}
			});
	}

	void RGGpuResourceAllocator::TrimPerKey(uint32_t maxCachedPerKey) noexcept
	{
		m_MaxCachedPerKey = maxCachedPerKey;
		if (maxCachedPerKey == 0)
		{
			return;
		}

		auto trim = [&](auto& map, auto& storeVec)
			{
				for (auto& keyValue : map)
				{
					auto& list = keyValue.second;
					while (list.size() > maxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						if (releaseResIndex.Value() >= 0 && releaseResIndex.Value() < storeVec.size() && storeVec[releaseResIndex.Value()])
						{
							storeVec[releaseResIndex.Value()]->Release();
						}
					}
				}
			};
		trim(m_FreeBuffers, m_Buffers);

		for (auto& keyValue : m_FreeTextures)
		{
			auto& list = keyValue.second;
			while (list.size() > maxCachedPerKey)
			{
				auto releaseResIndex = list.front();
				list.pop_front();
				DestroyTextureHandle(releaseResIndex);
			}
		}
	}

	bool RGGpuResourceAllocator::IsCompatibleTexture(ResourceIndex texIndex, const RGTextureDesc& desc) const noexcept
	{
		if (!texIndex.IsValid())
		{
			return false;
		}

		auto* texture = GetTexture(texIndex);
		if (!texture)
		{
			return false;
		}

		// Only check 2D texture
		const auto currentDesc = texture->Get()->GetDesc();
		if (currentDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
		{
			return false;
		}

		const auto currentKey = TextureKey::ConvertResourceDescToKey(currentDesc);
		const auto wantKey = MakeKey(desc);

		return currentKey == wantKey;
	}

	RHITextureUsage RGGpuResourceAllocator::ToRHITextureUsage(RGTextureUsage usage) noexcept
	{
		RHITextureUsage result = RHITextureUsage::None;
		if (Test(usage, RGTextureUsage::Sample))
		{
			result |= RHITextureUsage::Sampled;
		}
		if (Test(usage, RGTextureUsage::RenderTarget))
		{
			result |= RHITextureUsage::RenderTarget;
		}
		if (Test(usage, RGTextureUsage::DepthStencil | RGTextureUsage::DepthStencilRead))
		{
			result |= RHITextureUsage::DepthStencil;
		}
		if (Test(usage, RGTextureUsage::UAV))
		{
			result |= RHITextureUsage::UnorderedAccess;
		}
		if (Test(usage, RGTextureUsage::CopySrc))
		{
			result |= RHITextureUsage::CopySource;
		}
		if (Test(usage, RGTextureUsage::CopyDst))
		{
			result |= RHITextureUsage::CopyDest;
		}
		if (Test(usage, RGTextureUsage::Present))
		{
			result |= RHITextureUsage::Present;
		}
		return result;
	}

	std::optional<RHIClearValue> RGGpuResourceAllocator::DefaultRHIClearValue(const RGTextureDesc& desc) noexcept
	{
		if (Test(desc.m_Usage, RGTextureUsage::RenderTarget))
		{
			RHIClearValue clearValue{};
			clearValue.m_Format = desc.m_Format;
			clearValue.m_Color[0] = 0.0f;
			clearValue.m_Color[1] = 0.0f;
			clearValue.m_Color[2] = 0.0f;
			clearValue.m_Color[3] = 1.0f;
			clearValue.m_IsDepthStencil = false;
			return clearValue;
		}

		if (Test(desc.m_Usage, RGTextureUsage::DepthStencil))
		{
			RHIClearValue clearValue{};
			clearValue.m_Format = desc.m_Format == RHIFormat::R32Typeless ?
				RHIFormat::D32Float :
				desc.m_Format;
			clearValue.m_Depth = 1.0f;
			clearValue.m_Stencil = 0;
			clearValue.m_IsDepthStencil = true;
			return clearValue;
		}

		return std::nullopt;
	}

	void RGGpuResourceAllocator::DestroyTextureHandle(ResourceIndex texIndex) noexcept
	{
		if (!texIndex.IsValid() || texIndex.Value() >= m_Textures.size())
		{
			return;
		}

		RHITextureHandle& texture = m_Textures[texIndex.Value()];
		if (!texture.IsValid())
		{
			return;
		}

		m_DX12Device->DestroyTexture(texture);
		texture.Reset();
	}
}
