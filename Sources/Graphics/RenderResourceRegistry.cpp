#include "Core/Precompiled.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Graphics/RenderGraph/RGGpuResourceAllocator.h"
#include "Graphics/RenderGraph/RGExternalResourceRegistry.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorManager.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	RenderResourceRegistry::RenderResourceRegistry(const CreateInfo& createInfo) noexcept :
		m_RGGpuResAllocator(createInfo.m_RGGpuResAllocator),
		m_ExternalResourceRegistry(createInfo.m_ExternalResourceRegistry),
		m_DescriptorManager(createInfo.m_DescriptorManager),
		m_SamplerRegistry(createInfo.m_SamplerRegistry)
	{
		GGLAB_ASSERT_NOT_NULL(m_RGGpuResAllocator);
		GGLAB_ASSERT_NOT_NULL(m_ExternalResourceRegistry);
		GGLAB_ASSERT_NOT_NULL(m_DescriptorManager);
		GGLAB_ASSERT_NOT_NULL(m_SamplerRegistry);
	}

	void RenderResourceRegistry::EnsureIblResources(uint32_t brdfLutSize,
		DXGI_FORMAT brdfLutFormat,
		const DX12FencePoint* retireFenceOpt) noexcept
	{
		RGTextureDesc desc{};
		desc.m_Width = brdfLutSize;
		desc.m_Height = brdfLutSize;
		desc.m_ArraySize = 1;
		desc.m_MipLevels = 1;
		desc.m_SampleCount = 1;
		desc.m_Format = brdfLutFormat;
		desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

		EnsureTexture(TextureIndex::IBL_BrdfLut, desc, retireFenceOpt);
	}

	void RenderResourceRegistry::MarkDirty(TextureIndex index) noexcept
	{
		m_TextureEntries[utils::ToIndex(index)].m_Dirty = true;
	}

	bool RenderResourceRegistry::IsDirty(TextureIndex index) const noexcept
	{
		return m_TextureEntries[utils::ToIndex(index)].m_Dirty;
	}

	void RenderResourceRegistry::ClearDirty(TextureIndex index) noexcept
	{
		m_TextureEntries[utils::ToIndex(index)].m_Dirty = false;
	}

	DX12Texture* RenderResourceRegistry::GetTexture(TextureIndex index) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];
		return entry.m_Allocated ? m_RGGpuResAllocator->GetTexture(entry.m_InternalIndex) : nullptr;
	}

	ResourceIndex RenderResourceRegistry::GetExternalIndex(TextureIndex index) const noexcept
	{
		return m_TextureEntries[utils::ToIndex(index)].m_ExternalIndex;
	}

	DX12DescriptorID RenderResourceRegistry::GetBindlessSrvId(TextureIndex index) const noexcept
	{
		return m_TextureEntries[utils::ToIndex(index)].m_SrvId;
	}

	uint32_t RenderResourceRegistry::GetBindlessSrvIndex(TextureIndex index) const noexcept
	{
		return m_DescriptorManager->BindlessSrvIdToGlobalIndex(m_TextureEntries[utils::ToIndex(index)].m_SrvId);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE RenderResourceRegistry::GetBindlessSrvGpuHandle(TextureIndex index) const noexcept
	{
		const auto srvId = GetBindlessSrvId(index);
		GGLAB_ASSERT_MSG(srvId.IsValid(), "RenderResourceRegistry: invalid bindless SRV id.");

		return m_DescriptorManager->GetBindlessSrvGpuHandle(srvId);
	}

	void RenderResourceRegistry::FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept
	{
		out = {};

		const auto& entry = m_TextureEntries[utils::ToIndex(TextureIndex::IBL_BrdfLut)];
		GGLAB_ASSERT_MSG(entry.m_Allocated, "IBL BRDF LUT is not allocated.");
		GGLAB_ASSERT_MSG(entry.m_SrvId.IsValid(), "IBL BRDF LUT SRV is invalid.");

		out.BrdfLutTexIndex = m_DescriptorManager->BindlessSrvIdToGlobalIndex(entry.m_SrvId);
		out.BrdfLutSamplerIndex = m_SamplerRegistry->GetSamplerIndex(SamplerPreset::LinearClamp);
	}

	void RenderResourceRegistry::ReleaseAll(const DX12FencePoint& fencePoint) noexcept
	{
		for (size_t index = 0; index < m_TextureEntries.size(); ++index)
		{
			auto& entry = m_TextureEntries[index];
			if (entry.m_Allocated)
			{
				DestroyTexture(static_cast<TextureIndex>(index), fencePoint);
			}
		}
	}

	void RenderResourceRegistry::EnsureTexture(TextureIndex index, const RGTextureDesc& desc, const DX12FencePoint* retireFenceOpt) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];

		// Create new texture and ResourceIndex
		auto createTexture = [this](TextureEntry& outEntry, const RGTextureDesc& desc) noexcept
			{
				const auto clearValue = DefaultClearValue<RGTextureDesc>(desc);
				const auto texIndex = m_RGGpuResAllocator->Acquire<RGTextureDesc>(
					desc, D3D12_RESOURCE_STATE_COMMON, clearValue);

				GGLAB_ASSERT_MSG(texIndex.IsValid(),
					"RenderResourceRegistry: Acquire texture failed.");

				auto* texture = m_RGGpuResAllocator->GetTexture(texIndex);
				GGLAB_ASSERT_MSG(texture != nullptr,
					"RenderResourceRegistry: allocator returned null texture.");

				outEntry.m_RgTexDesc = desc;
				outEntry.m_InternalIndex = texIndex;
				outEntry.m_ExternalIndex = m_ExternalResourceRegistry->GetOrCreate(texture);
				GGLAB_ASSERT_MSG(ExternalResourceIndex::IsExternal(outEntry.m_ExternalIndex),
					"ExternalIndex is not external.");

				outEntry.m_Allocated = true;
				outEntry.m_Dirty = true;

				// Make srv
				DX12DescriptorManager::TextureSrvCreateInfo srvInfo{};
				srvInfo.m_Format = desc.m_Format;
				srvInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				if (outEntry.m_SrvId.IsValid())
				{
					m_DescriptorManager->WriteBindlessSrv(outEntry.m_SrvId, texture, srvInfo);
				}
				else
				{
					outEntry.m_SrvId = m_DescriptorManager->CreateBindlessSrv(texture, srvInfo);
				}
			};

		// Already exist. Check compatible
		if (entry.m_Allocated && entry.m_InternalIndex.IsValid())
		{
			if (m_RGGpuResAllocator->IsCompatibleTexture(entry.m_InternalIndex, desc))
			{
				entry.m_RgTexDesc = desc;
				return;
			}

			// Texture not compatible to desc, need to recreate.
			GGLAB_ASSERT_MSG(retireFenceOpt != nullptr,
				"RenderResourceRegistry: Texture desc changed but no fence provided. "
				"Provide a fencePoint to retire old resource safely.");

			if (!retireFenceOpt)
			{
				GGLAB_LOG_GRAPHICS_ERROR("RenderResourceRegistry: desc changed but no fence, keep old resource (index={}).",
					static_cast<uint32_t>(index));
				return;
			}

			if (auto* oldTexture = m_RGGpuResAllocator->GetTexture(entry.m_InternalIndex))
			{
				m_ExternalResourceRegistry->Forget(oldTexture, false, retireFenceOpt);
			}

			m_RGGpuResAllocator->ReleaseTexture(entry.m_InternalIndex, *retireFenceOpt);

			// Create new
			createTexture(entry, desc);
			return;
		}

		// First time create
		{
			createTexture(entry, desc);
		}
	}

	void RenderResourceRegistry::DestroyTexture(TextureIndex index, const DX12FencePoint& fencePoint) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];
		if (!entry.m_Allocated)
		{
			return;
		}

		auto* texture = m_RGGpuResAllocator->GetTexture(entry.m_InternalIndex);
		if (texture)
		{
			m_ExternalResourceRegistry->Forget(texture, false, &fencePoint);
		}
		m_RGGpuResAllocator->ReleaseTexture(entry.m_InternalIndex, fencePoint);

		// clear but keep srv id
		const auto keepSrvId = entry.m_SrvId;
		entry = {};
		entry.m_SrvId = keepSrvId;
	}
}