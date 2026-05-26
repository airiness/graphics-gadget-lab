#include "Core/Precompiled.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Graphics/RenderGraph/RGGpuResourceAllocator.h"
#include "Graphics/RenderGraph/RGExternalResourceRegistry.h"
#include "Graphics/RenderGraph/RGResourceUtils.h"
#include "Graphics/SamplerRegistry.h"

#include <algorithm>

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

	void RenderResourceRegistry::EnsureIblResources(const IBLResourceCreateInfo& createInfo, const DX12FencePoint* retireFenceOpt) noexcept
	{
		// IBL_EnvironmentCubemap
		{
			RGTextureDesc desc{};
			desc.m_Width = createInfo.m_EnvironmentCubemapSize;
			desc.m_Height = createInfo.m_EnvironmentCubemapSize;
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_EnvironmentCubemapFormat;
			desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

			TextureSrvCreateInfo srvCreateInfo{};
			srvCreateInfo.m_Format = desc.m_Format;
			srvCreateInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvCreateInfo.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_EnvironmentCubemap, desc, srvCreateInfo, retireFenceOpt);
		}

		// IBL_IrradianceCubemap
		{
			RGTextureDesc desc{};
			desc.m_Width = createInfo.m_IrradianceCubemapSize;
			desc.m_Height = createInfo.m_IrradianceCubemapSize;
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_IrradianceCubemapFormat;
			desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

			TextureSrvCreateInfo srvCreateInfo{};
			srvCreateInfo.m_Format = desc.m_Format;
			srvCreateInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvCreateInfo.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_IrradianceCubemap, desc, srvCreateInfo, retireFenceOpt);
		}

		// IBL_PrefilteredSpecularCubemap
		{
			auto maxMipLevels = [](uint32_t size) noexcept -> uint32_t
				{
					uint32_t levels = 1;
					while (size > 1)
					{
						size >>= 1;
						++levels;
					}
					return levels;
				};

			const uint32_t prefilteredMipLevels = std::clamp(
				createInfo.m_PrefilteredSpecularMipLevels,
				1u,
				maxMipLevels(createInfo.m_PrefilteredSpecularCubemapSize));

			RGTextureDesc desc{};
			desc.m_Width = createInfo.m_PrefilteredSpecularCubemapSize;
			desc.m_Height = createInfo.m_PrefilteredSpecularCubemapSize;
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = static_cast<uint16_t>(prefilteredMipLevels);
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PrefilteredSpecularCubemapFormat;
			desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

			TextureSrvCreateInfo srvCreateInfo{};
			srvCreateInfo.m_Format = desc.m_Format;
			srvCreateInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvCreateInfo.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_PrefilteredSpecularCubemap, desc, srvCreateInfo, retireFenceOpt);
		}

		// IBL_BrdfLut
		{
			RGTextureDesc desc{};
			desc.m_Width = createInfo.m_BrdfLutSize;
			desc.m_Height = createInfo.m_BrdfLutSize;
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_BrdfLutFormat;
			desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

			TextureSrvCreateInfo srvCreateInfo{};
			srvCreateInfo.m_Format = desc.m_Format;
			srvCreateInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvCreateInfo.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_BrdfLut, desc, srvCreateInfo, retireFenceOpt);
		}

		// DebugPreview_IBL_EnvironmentCubemap
		{
			RGTextureDesc desc{};
			desc.m_Width = createInfo.m_DebugPreviewIBLEnvironmentCubemapFaceSize * 4u;
			desc.m_Height = createInfo.m_DebugPreviewIBLEnvironmentCubemapFaceSize * 3u;
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_DebugPreviewIBLEnvironmentCubemapFormat;
			desc.m_Usage = RGTextureUsage::RenderTarget | RGTextureUsage::Sample;

			TextureSrvCreateInfo srvCreateInfo{};
			srvCreateInfo.m_Format = desc.m_Format;
			srvCreateInfo.m_Dimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvCreateInfo.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::DebugPreview_IBL_EnvironmentCubemap, desc, srvCreateInfo, retireFenceOpt);
		}
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

	void RenderResourceRegistry::SetIBLDebugPreviewLayout(IBLDebugPreviewLayout layout) noexcept
	{
		if (layout >= IBLDebugPreviewLayout::Count)
		{
			return;
		}

		m_IBLDebugPreviewLayout = layout;
	}

	void RenderResourceRegistry::FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept
	{
		out = {};

		auto fillTextureSamplerBinding = [this](
			TextureIndex index,
			TextureSamplerBindingGPU& outBinding,
			SamplerPreset samplerPreset) noexcept
			{
				const auto& entry = m_TextureEntries[utils::ToIndex(index)];
				GGLAB_ASSERT_MSG(entry.m_Allocated, "IBL resource is not allocated.");
				GGLAB_ASSERT_MSG(entry.m_SrvId.IsValid(), "IBL resource SRV is invalid.");

				outBinding.TextureIndex = m_DescriptorManager->BindlessSrvIdToGlobalIndex(entry.m_SrvId);
				outBinding.SamplerIndex = m_SamplerRegistry->GetSamplerIndex(samplerPreset);
			};

		fillTextureSamplerBinding(TextureIndex::IBL_EnvironmentCubemap,
			out.EnvironmentBinding,
			SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(TextureIndex::IBL_IrradianceCubemap,
			out.IrradianceBinding,
			SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(TextureIndex::IBL_PrefilteredSpecularCubemap,
			out.PrefilteredSpecularBinding,
			SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(TextureIndex::IBL_BrdfLut,
			out.BrdfLutBinding,
			SamplerPreset::LinearClamp);

		const auto& prefilteredEntry = m_TextureEntries[utils::ToIndex(TextureIndex::IBL_PrefilteredSpecularCubemap)];
		out.PrefilteredSpecularMipLevels = prefilteredEntry.m_RgTexDesc.m_MipLevels;
		out.EnvironmentIntensity = 1.0f;
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

	void RenderResourceRegistry::EnsureTexture(TextureIndex index,
		const RGTextureDesc& desc,
		const TextureSrvCreateInfo& srvCreateInfo,
		const DX12FencePoint* retireFenceOpt) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];

		// Create new texture and ResourceIndex
		auto createTexture = [this, &srvCreateInfo](TextureEntry& outEntry, const RGTextureDesc& desc) noexcept
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
				outEntry.m_SrvCreateInfo = srvCreateInfo;

				// Make srv
				if (outEntry.m_SrvId.IsValid())
				{
					m_DescriptorManager->WriteBindlessSrv(outEntry.m_SrvId, texture, srvCreateInfo);
				}
				else
				{
					outEntry.m_SrvId = m_DescriptorManager->CreateBindlessSrv(texture, srvCreateInfo);
				}
			};

		// Already exist. Check compatible
		if (entry.m_Allocated && entry.m_InternalIndex.IsValid())
		{
			if (m_RGGpuResAllocator->IsCompatibleTexture(entry.m_InternalIndex, desc))
			{
				entry.m_RgTexDesc = desc;

				// If Srv create info changed, update the texture DescriptorID
				if (entry.m_SrvCreateInfo != srvCreateInfo)
				{
					auto* texture = m_RGGpuResAllocator->GetTexture(entry.m_InternalIndex);
					GGLAB_ASSERT_NOT_NULL(texture);

					if (entry.m_SrvId.IsValid())
					{
						m_DescriptorManager->WriteBindlessSrv(entry.m_SrvId, texture, srvCreateInfo);
					}
					else
					{
						entry.m_SrvId = m_DescriptorManager->CreateBindlessSrv(texture, srvCreateInfo);
					}

					entry.m_SrvCreateInfo = srvCreateInfo;
				}

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
