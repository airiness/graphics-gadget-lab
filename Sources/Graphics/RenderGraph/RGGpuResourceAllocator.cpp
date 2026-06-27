#include "Core/Precompiled.h"
#include "Graphics/RenderGraph/RGGpuResourceAllocator.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/DX12/DX12Device.h"

namespace gglab
{
	RGGpuResourceAllocator::RGGpuResourceAllocator(RHIDevice* device) noexcept :
		m_Device(device),
		m_DX12Device(dynamic_cast<DX12Device*>(device))
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice must not be null.");
		GGLAB_ASSERT_MSG(m_DX12Device != nullptr, "RGGpuResourceAllocator still requires a DX12 backend.");
	}

	RGGpuResourceAllocator::~RGGpuResourceAllocator() noexcept
	{
		for (uint32_t index = 0; index < m_Textures.size(); ++index)
		{
			DestroyTextureHandle(ResourceIndex(index));
		}
		for (uint32_t index = 0; index < m_Buffers.size(); ++index)
		{
			DestroyBufferHandle(ResourceIndex(index));
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

	ResourceIndex RGGpuResourceAllocator::CreateTexture(const RHITextureDesc& desc) noexcept
	{
		ResourceIndex index = ResourceIndex(static_cast<uint32_t>(m_Textures.size()));
		RHITextureDesc textureDesc = desc;
		if (!textureDesc.m_DebugName)
		{
			textureDesc.m_DebugName = "RGGpuResourceAllocator.Texture";
		}
		if (!textureDesc.m_ClearValue)
		{
			textureDesc.m_ClearValue = DefaultRHIClearValue(textureDesc);
		}

		RHITextureHandle texture = m_Device->CreateTexture(textureDesc);
		GGLAB_ASSERT_MSG(texture.IsValid(), "RGGpuResourceAllocator: CreateTexture failed.");
		m_Textures.push_back(TextureRecord{
			.m_Texture = texture,
			.m_Key = MakeKey(desc),
			});

		return index;
	}

	ResourceIndex RGGpuResourceAllocator::CreateBuffer(const RHIBufferDesc& desc) noexcept
	{
		ResourceIndex index = ResourceIndex(static_cast<uint32_t>(m_Buffers.size()));
		RHIBufferDesc bufferDesc = desc;
		if (!bufferDesc.m_DebugName)
		{
			bufferDesc.m_DebugName = "RGGpuResourceAllocator.Buffer";
		}

		RHIBufferHandle buffer = m_Device->CreateBuffer(bufferDesc);
		GGLAB_ASSERT_MSG(buffer.IsValid(), "RGGpuResourceAllocator: CreateBuffer failed.");
		m_Buffers.push_back(BufferRecord{
			.m_Buffer = buffer,
			.m_Key = MakeKey(desc),
			});

		return index;
	}

	DX12Texture* RGGpuResourceAllocator::GetTexture(ResourceIndex texIndex) const noexcept
	{
		if (!texIndex.IsValid() || texIndex.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTexture() : Invalid texIndex.");
			return nullptr;
		}

		return m_DX12Device->ResolveTexture(m_Textures[texIndex.Value()].m_Texture);
	}

	DX12Buffer* RGGpuResourceAllocator::GetBuffer(ResourceIndex bufIndex) const noexcept
	{
		if (!bufIndex.IsValid() || bufIndex.Value() >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetBuffer() : Invalid bufIndex.");
			return nullptr;
		}
		return m_DX12Device->ResolveBuffer(m_Buffers[bufIndex.Value()].m_Buffer);
	}

	RHITextureHandle RGGpuResourceAllocator::GetTextureHandle(ResourceIndex texIndex) noexcept
	{
		if (!texIndex.IsValid() || texIndex.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTextureHandle() : Invalid texIndex.");
			return {};
		}

		const RHITextureHandle handle = m_Textures[texIndex.Value()].m_Texture;
		if (handle.IsValid() && m_Device->IsAlive(handle))
		{
			return handle;
		}

		GGLAB_LOG_WARN("RGGpuResourceAllocator::GetTextureHandle() : Texture is not live.");
		return {};
	}

	RHIBufferHandle RGGpuResourceAllocator::GetBufferHandle(ResourceIndex bufIndex) noexcept
	{
		if (!bufIndex.IsValid() || bufIndex.Value() >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("RGGpuResourceAllocator::GetBufferHandle() : Invalid bufIndex.");
			return {};
		}

		const RHIBufferHandle handle = m_Buffers[bufIndex.Value()].m_Buffer;
		if (handle.IsValid() && m_Device->IsAlive(handle))
		{
			return handle;
		}

		GGLAB_LOG_WARN("RGGpuResourceAllocator::GetBufferHandle() : Buffer is not live.");
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
					if (pending.m_Index.Value() >= m_Textures.size())
					{
						return true;
					}

					auto texKey = m_Textures[pending.m_Index.Value()].m_Key;
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
					if (pending.m_Index.Value() >= m_Buffers.size())
					{
						return true;
					}

					auto bufKey = m_Buffers[pending.m_Index.Value()].m_Key;
					auto& list = m_FreeBuffers[bufKey];
					list.push_back(pending.m_Index);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						auto releaseResIndex = list.front();
						list.pop_front();
						DestroyBufferHandle(releaseResIndex);
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
						if (releaseResIndex.Value() >= 0 &&
							releaseResIndex.Value() < storeVec.size() &&
							storeVec[releaseResIndex.Value()].m_Buffer.IsValid())
						{
							DestroyBufferHandle(releaseResIndex);
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

	bool RGGpuResourceAllocator::IsCompatibleTexture(ResourceIndex texIndex, const RHITextureDesc& desc) const noexcept
	{
		if (!texIndex.IsValid())
		{
			return false;
		}

		if (texIndex.Value() >= m_Textures.size())
		{
			return false;
		}

		const auto& record = m_Textures[texIndex.Value()];
		if (!record.m_Texture.IsValid() || !m_Device->IsAlive(record.m_Texture))
		{
			return false;
		}

		const auto currentKey = record.m_Key;
		const auto wantKey = MakeKey(desc);

		return currentKey == wantKey;
	}

	std::optional<RHIClearValue> RGGpuResourceAllocator::DefaultRHIClearValue(const RHITextureDesc& desc) noexcept
	{
		if (Test(desc.m_Usage, RHITextureUsage::RenderTarget))
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

		if (Test(desc.m_Usage, RHITextureUsage::DepthStencil))
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

		RHITextureHandle& texture = m_Textures[texIndex.Value()].m_Texture;
		if (!texture.IsValid())
		{
			return;
		}

		m_Device->DestroyTexture(texture);
		texture.Reset();
	}

	void RGGpuResourceAllocator::DestroyBufferHandle(ResourceIndex bufIndex) noexcept
	{
		if (!bufIndex.IsValid() || bufIndex.Value() >= m_Buffers.size())
		{
			return;
		}

		RHIBufferHandle& buffer = m_Buffers[bufIndex.Value()].m_Buffer;
		if (!buffer.IsValid())
		{
			return;
		}

		m_Device->DestroyBuffer(buffer);
		buffer.Reset();
	}
}
