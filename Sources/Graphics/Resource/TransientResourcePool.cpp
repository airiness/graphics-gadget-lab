#include "Core/Precompiled.h"
#include "Graphics/Resource/TransientResourcePool.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	TransientResourcePool::TransientResourcePool(RHIDevice* device) noexcept :
		m_Device(device)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice must not be null.");
	}

	TransientResourcePool::~TransientResourcePool() noexcept
	{
		for (uint32_t index = 0; index < m_Textures.size(); ++index)
		{
			DestroyTexture(TransientResourcePoolSlot(index));
		}
		for (uint32_t index = 0; index < m_Buffers.size(); ++index)
		{
			DestroyBuffer(TransientResourcePoolSlot(index));
		}
	}

	void TransientResourcePool::RetireTexture(
		TransientTextureAllocation&& allocation,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocation.IsValid() || allocation.m_PoolSlot.Value() >= m_Textures.size())
		{
			GGLAB_LOG_WARN("TransientResourcePool::RetireTexture received an invalid allocation.");
			return;
		}

		const auto& record = m_Textures[allocation.m_PoolSlot.Value()];
		GGLAB_ASSERT_MSG(record.m_Texture == allocation.m_Texture && record.m_Key == allocation.m_Key,
			"Texture allocation does not match its transient pool slot.");

		m_PendingRetirements.push_back(
			{
				.m_Type = ResourceType::Texture,
				.m_PoolSlot = allocation.m_PoolSlot,
				.m_FencePoint = fencePoint,
			});
		allocation.Reset();
	}

	void TransientResourcePool::RetireBuffer(
		TransientBufferAllocation&& allocation,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocation.IsValid() || allocation.m_PoolSlot.Value() >= m_Buffers.size())
		{
			GGLAB_LOG_WARN("TransientResourcePool::RetireBuffer received an invalid allocation.");
			return;
		}

		const auto& record = m_Buffers[allocation.m_PoolSlot.Value()];
		GGLAB_ASSERT_MSG(record.m_Buffer == allocation.m_Buffer && record.m_Key == allocation.m_Key,
			"Buffer allocation does not match its transient pool slot.");

		m_PendingRetirements.push_back(
			{
				.m_Type = ResourceType::Buffer,
				.m_PoolSlot = allocation.m_PoolSlot,
				.m_FencePoint = fencePoint,
			});
		allocation.Reset();
	}

	TransientTextureKey TransientResourcePool::MakeTextureKey(const RHITextureDesc& desc) noexcept
	{
		return
		{
			.m_Dimension = desc.m_Dimension,
			.m_Extent = desc.m_Extent,
			.m_ArraySize = desc.m_ArraySize,
			.m_MipLevels = desc.m_MipLevels,
			.m_SampleCount = desc.m_SampleCount,
			.m_Format = desc.m_Format,
			.m_Usage = desc.m_Usage,
			.m_ClearValue = desc.m_ClearValue ? desc.m_ClearValue : DefaultClearValue(desc),
		};
	}

	TransientBufferKey TransientResourcePool::MakeBufferKey(const RHIBufferDesc& desc) noexcept
	{
		return
		{
			.m_SizeInBytes = desc.m_SizeInBytes,
			.m_StrideInBytes = desc.m_StrideInBytes,
			.m_Usage = desc.m_Usage,
			.m_MemoryUsage = desc.m_MemoryUsage,
		};
	}

	TransientTextureAllocation TransientResourcePool::AcquireTexture(const RHITextureDesc& desc) noexcept
	{
		const auto key = MakeTextureKey(desc);
		if (auto iter = m_FreeTextures.find(key);
			iter != m_FreeTextures.end() && !iter->second.empty())
		{
			const auto poolSlot = iter->second.back();
			iter->second.pop_back();
			const auto& record = m_Textures[poolSlot.Value()];
			return { record.m_Texture, poolSlot, record.m_Key };
		}

		return CreateTexture(desc);
	}

	TransientBufferAllocation TransientResourcePool::AcquireBuffer(const RHIBufferDesc& desc) noexcept
	{
		const auto key = MakeBufferKey(desc);
		if (auto iter = m_FreeBuffers.find(key);
			iter != m_FreeBuffers.end() && !iter->second.empty())
		{
			const auto poolSlot = iter->second.back();
			iter->second.pop_back();
			const auto& record = m_Buffers[poolSlot.Value()];
			return { record.m_Buffer, poolSlot, record.m_Key };
		}

		return CreateBuffer(desc);
	}

	TransientTextureAllocation TransientResourcePool::CreateTexture(const RHITextureDesc& desc) noexcept
	{
		const TransientResourcePoolSlot poolSlot{ static_cast<uint32_t>(m_Textures.size()) };
		RHITextureDesc textureDesc = desc;
		if (!textureDesc.m_DebugName)
		{
			textureDesc.m_DebugName = "TransientResourcePool.Texture";
		}
		if (!textureDesc.m_ClearValue)
		{
			textureDesc.m_ClearValue = DefaultClearValue(textureDesc);
		}

		RHITextureHandle texture = m_Device->CreateTexture(textureDesc);
		GGLAB_ASSERT_MSG(texture.IsValid(), "TransientResourcePool: CreateTexture failed.");
		if (!texture.IsValid())
		{
			return {};
		}

		const auto key = MakeTextureKey(desc);
		m_Textures.push_back(TextureRecord{
			.m_Texture = texture,
			.m_Key = key,
			});

		return { texture, poolSlot, key };
	}

	TransientBufferAllocation TransientResourcePool::CreateBuffer(const RHIBufferDesc& desc) noexcept
	{
		const TransientResourcePoolSlot poolSlot{ static_cast<uint32_t>(m_Buffers.size()) };
		RHIBufferDesc bufferDesc = desc;
		if (!bufferDesc.m_DebugName)
		{
			bufferDesc.m_DebugName = "TransientResourcePool.Buffer";
		}

		RHIBufferHandle buffer = m_Device->CreateBuffer(bufferDesc);
		GGLAB_ASSERT_MSG(buffer.IsValid(), "TransientResourcePool: CreateBuffer failed.");
		if (!buffer.IsValid())
		{
			return {};
		}

		const auto key = MakeBufferKey(desc);
		m_Buffers.push_back(BufferRecord{
			.m_Buffer = buffer,
			.m_Key = key,
			});

		return { buffer, poolSlot, key };
	}

	void TransientResourcePool::Tick() noexcept
	{
		std::erase_if(m_PendingRetirements,
			[this](const PendingRetirement& pending)
			{
				const bool done = m_Device->IsFencePointCompleted(pending.m_FencePoint);
				if (!done)
				{
					return false;
				}

				switch (pending.m_Type)
				{
				case ResourceType::Texture:
				{
					if (pending.m_PoolSlot.Value() >= m_Textures.size())
					{
						return true;
					}

					const auto texKey = m_Textures[pending.m_PoolSlot.Value()].m_Key;
					auto& list = m_FreeTextures[texKey];
					list.push_back(pending.m_PoolSlot);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						const auto poolSlot = list.front();
						list.pop_front();
						DestroyTexture(poolSlot);
					}
					return true;
				}
				case ResourceType::Buffer:
				{
					if (pending.m_PoolSlot.Value() >= m_Buffers.size())
					{
						return true;
					}

					const auto bufKey = m_Buffers[pending.m_PoolSlot.Value()].m_Key;
					auto& list = m_FreeBuffers[bufKey];
					list.push_back(pending.m_PoolSlot);

					if (m_MaxCachedPerKey > 0 && list.size() > m_MaxCachedPerKey)
					{
						const auto poolSlot = list.front();
						list.pop_front();
						DestroyBuffer(poolSlot);
					}
					return true;
				}
				default:
					GGLAB_UNREACHABLE("switch error");
				}
			});
	}

	void TransientResourcePool::TrimPerKey(uint32_t maxCachedPerKey) noexcept
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
						const auto poolSlot = list.front();
						list.pop_front();
						if (poolSlot.Value() < storeVec.size() &&
							storeVec[poolSlot.Value()].m_Buffer.IsValid())
						{
							DestroyBuffer(poolSlot);
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
				const auto poolSlot = list.front();
				list.pop_front();
				DestroyTexture(poolSlot);
			}
		}
	}

	bool TransientResourcePool::IsCompatibleTexture(
		const TransientTextureAllocation& allocation,
		const RHITextureDesc& desc) const noexcept
	{
		if (!allocation.IsValid() || allocation.m_PoolSlot.Value() >= m_Textures.size())
		{
			return false;
		}

		const auto& record = m_Textures[allocation.m_PoolSlot.Value()];
		if (!record.m_Texture.IsValid() || !m_Device->IsAlive(record.m_Texture))
		{
			return false;
		}

		return record.m_Texture == allocation.m_Texture &&
			record.m_Key == allocation.m_Key &&
			allocation.m_Key == MakeTextureKey(desc);
	}

	std::optional<RHIClearValue> TransientResourcePool::DefaultClearValue(const RHITextureDesc& desc) noexcept
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
			clearValue.m_Format = GetRHIFormatInfo(desc.m_Format).m_DepthStencilViewFormat;
			clearValue.m_Depth = 1.0f;
			clearValue.m_Stencil = 0;
			clearValue.m_IsDepthStencil = true;
			return clearValue;
		}

		return std::nullopt;
	}

	void TransientResourcePool::DestroyTexture(TransientResourcePoolSlot poolSlot) noexcept
	{
		if (!poolSlot.IsValid() || poolSlot.Value() >= m_Textures.size())
		{
			return;
		}

		RHITextureHandle& texture = m_Textures[poolSlot.Value()].m_Texture;
		if (!texture.IsValid())
		{
			return;
		}

		m_Device->DestroyTexture(texture);
		texture.Reset();
	}

	void TransientResourcePool::DestroyBuffer(TransientResourcePoolSlot poolSlot) noexcept
	{
		if (!poolSlot.IsValid() || poolSlot.Value() >= m_Buffers.size())
		{
			return;
		}

		RHIBufferHandle& buffer = m_Buffers[poolSlot.Value()].m_Buffer;
		if (!buffer.IsValid())
		{
			return;
		}

		m_Device->DestroyBuffer(buffer);
		buffer.Reset();
	}
}
