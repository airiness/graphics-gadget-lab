#include "Core/Precompiled.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Graphics/TransientResourcePool.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/SamplerRegistry.h"

#include <algorithm>

namespace gglab
{
	RenderResourceRegistry::RenderResourceRegistry(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransientResourcePool(createInfo.m_TransientResourcePool),
		m_SamplerRegistry(createInfo.m_SamplerRegistry)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TransientResourcePool);
		GGLAB_ASSERT_NOT_NULL(m_SamplerRegistry);
	}

	void RenderResourceRegistry::EnsureIblResources(const IBLResourceCreateInfo& createInfo, const RHIFencePoint* retireFenceOpt) noexcept
	{
		// IBL_EnvironmentCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_EnvironmentCubemapSize, createInfo.m_EnvironmentCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_EnvironmentCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;
			srvDesc.m_NumCubes = 1;

			EnsureTexture(TextureIndex::IBL_EnvironmentCubemap, desc, srvDesc, retireFenceOpt);
		}

		// IBL_IrradianceCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_IrradianceCubemapSize, createInfo.m_IrradianceCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_IrradianceCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;
			srvDesc.m_NumCubes = 1;

			EnsureTexture(TextureIndex::IBL_IrradianceCubemap, desc, srvDesc, retireFenceOpt);
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

			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_PrefilteredSpecularCubemapSize, createInfo.m_PrefilteredSpecularCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = static_cast<uint16_t>(prefilteredMipLevels);
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PrefilteredSpecularCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;
			srvDesc.m_NumCubes = 1;

			EnsureTexture(TextureIndex::IBL_PrefilteredSpecularCubemap, desc, srvDesc, retireFenceOpt);
		}

		// IBL_BrdfLut
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_BrdfLutSize, createInfo.m_BrdfLutSize, 1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_BrdfLutFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_BrdfLut, desc, srvDesc, retireFenceOpt);
		}

		// Preview_IBL_EnvironmentCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = {
				createInfo.m_PreviewIBLEnvironmentCubemapFaceSize * 4u,
				createInfo.m_PreviewIBLEnvironmentCubemapFaceSize * 3u,
				1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PreviewIBLEnvironmentCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::Preview_IBL_EnvironmentCubemap, desc, srvDesc, retireFenceOpt);
		}

		// Preview_IBL_PrefilteredSpecularCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = {
				createInfo.m_PreviewIBLPrefilteredSpecularCubemapFaceSize * 4u,
				createInfo.m_PreviewIBLPrefilteredSpecularCubemapFaceSize * 3u,
				1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PreviewIBLPrefilteredSpecularCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_MipLevels = desc.m_MipLevels;

			EnsureTexture(TextureIndex::Preview_IBL_PrefilteredSpecularCubemap, desc, srvDesc, retireFenceOpt);
		}
	}

	void RenderResourceRegistry::EnsureShadowPreviewResources(uint32_t previewSize, const RHIFencePoint* retireFenceOpt) noexcept
	{
		const uint32_t size = std::max(previewSize, 1u);

		RHITextureDesc desc{};
		desc.m_Extent = { size, size, 1u };
		desc.m_ArraySize = 1;
		desc.m_MipLevels = 1;
		desc.m_SampleCount = 1;
		desc.m_Format = RHIFormat::R8G8B8A8Unorm;
		desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

		RHITextureViewDesc srvDesc{};
		srvDesc.m_Type = RHITextureViewType::ShaderResource;
		srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
		srvDesc.m_Format = desc.m_Format;
		srvDesc.m_MipLevels = desc.m_MipLevels;

		EnsureTexture(TextureIndex::Preview_Shadow_DirectionalShadowMap, desc, srvDesc, retireFenceOpt);
	}

	void RenderResourceRegistry::MarkDirty(TextureIndex index) noexcept
	{
		m_TextureEntries[utils::ToIndex(index)].m_Dirty = true;
		InvalidateDependents(index);
	}

	void RenderResourceRegistry::InvalidateDependents(TextureIndex index) noexcept
	{
		if (index == TextureIndex::IBL_EnvironmentCubemap)
		{
			MarkDirty(TextureIndex::IBL_IrradianceCubemap);
			MarkDirty(TextureIndex::IBL_PrefilteredSpecularCubemap);
		}
	}

	bool RenderResourceRegistry::IsDirty(TextureIndex index) const noexcept
	{
		return m_TextureEntries[utils::ToIndex(index)].m_Dirty;
	}

	void RenderResourceRegistry::ClearDirty(TextureIndex index) noexcept
	{
		m_TextureEntries[utils::ToIndex(index)].m_Dirty = false;
	}

	const RHITextureDesc* RenderResourceRegistry::GetTextureDesc(TextureIndex index) const noexcept
	{
		const auto& entry = m_TextureEntries[utils::ToIndex(index)];
		return entry.m_Allocated ? &entry.m_TextureDesc : nullptr;
	}

	RHITextureHandle RenderResourceRegistry::GetTextureHandle(TextureIndex index) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];
		return entry.m_Allocated ? entry.m_PhysicalAllocation.m_Texture : RHITextureHandle{};
	}

	RHIDescriptorHandle RenderResourceRegistry::GetSrvDescriptor(TextureIndex index) const noexcept
	{
		const auto& entry = m_TextureEntries[utils::ToIndex(index)];
		GGLAB_ASSERT_MSG(entry.m_Allocated, "RenderResourceRegistry: texture is not allocated.");
		GGLAB_ASSERT_MSG(entry.m_Srv.IsValid(), "RenderResourceRegistry: texture SRV is invalid.");

		const RHIDescriptorHandle descriptor = m_Device->GetTextureViewDescriptor(entry.m_Srv);
		GGLAB_ASSERT_MSG(
			descriptor.IsValid() && descriptor.m_HeapType == RHIDescriptorHeapType::CbvSrvUav,
			"RenderResourceRegistry: texture SRV descriptor is invalid.");
		return descriptor;
	}

	uint32_t RenderResourceRegistry::GetShaderVisibleSrvIndex(TextureIndex index) const noexcept
	{
		const RHIDescriptorHandle descriptor = GetSrvDescriptor(index);
		return descriptor.m_Index;
	}

	void RenderResourceRegistry::SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept
	{
		if (layout >= IBLPreviewLayout::Count)
		{
			return;
		}

		m_IBLEnvironmentPreviewLayout = layout;
	}

	void RenderResourceRegistry::SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout layout) noexcept
	{
		if (layout >= IBLPreviewLayout::Count)
		{
			return;
		}

		m_IBLPrefilteredSpecularPreviewLayout = layout;
	}

	void RenderResourceRegistry::SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept
	{
		m_IBLPrefilteredSpecularPreviewMip = mip;
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
				GGLAB_ASSERT_MSG(entry.m_Srv.IsValid(), "IBL resource SRV is invalid.");

				outBinding.TextureIndex = GetShaderVisibleSrvIndex(index);
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
		out.PrefilteredSpecularMipLevels = prefilteredEntry.m_TextureDesc.m_MipLevels;
		out.EnvironmentIntensity = 1.0f;
	}

	void RenderResourceRegistry::ReleaseAll(const RHIFencePoint& fencePoint) noexcept
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
		const RHITextureDesc& desc,
		const RHITextureViewDesc& srvDesc,
		const RHIFencePoint* retireFenceOpt) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];

		// Acquire a physical texture allocation from the reusable pool.
		auto createTexture = [this, index, &srvDesc](TextureEntry& outEntry, const RHITextureDesc& desc) noexcept
			{
				auto allocation = m_TransientResourcePool->AcquireTexture(desc);
				GGLAB_ASSERT_MSG(allocation.IsValid(),
					"RenderResourceRegistry: Acquire texture failed.");
				if (!allocation.IsValid())
				{
					return;
				}

				outEntry.m_TextureDesc = desc;
				outEntry.m_PhysicalAllocation = std::move(allocation);
				outEntry.m_Allocated = true;
				outEntry.m_SrvDesc = srvDesc;

				const RHITextureHandle textureHandle = outEntry.m_PhysicalAllocation.m_Texture;
				GGLAB_ASSERT_MSG(textureHandle.IsValid(),
					"RenderResourceRegistry: acquired transient texture handle is invalid.");

				outEntry.m_Srv = m_Device->CreateTextureView(textureHandle, srvDesc);
				GGLAB_ASSERT_MSG(outEntry.m_Srv.IsValid(),
					"RenderResourceRegistry: failed to create RHI texture SRV.");

				MarkDirty(index);
			};

		// Already exist. Check compatible
		if (entry.m_Allocated && entry.m_PhysicalAllocation.IsValid())
		{
			if (m_TransientResourcePool->IsCompatibleTexture(entry.m_PhysicalAllocation, desc))
			{
				entry.m_TextureDesc = desc;

				// If Srv create info changed, update the texture DescriptorID
				if (entry.m_SrvDesc != srvDesc)
				{
					const RHITextureHandle textureHandle = entry.m_PhysicalAllocation.m_Texture;
					GGLAB_ASSERT_MSG(textureHandle.IsValid(),
						"RenderResourceRegistry: acquired transient texture handle is invalid.");

					entry.m_Srv = m_Device->CreateTextureView(textureHandle, srvDesc);
					GGLAB_ASSERT_MSG(entry.m_Srv.IsValid(),
						"RenderResourceRegistry: failed to create RHI texture SRV.");

					entry.m_SrvDesc = srvDesc;
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

			m_TransientResourcePool->RetireTexture(
				std::move(entry.m_PhysicalAllocation),
				*retireFenceOpt);

			// Create new
			createTexture(entry, desc);
			return;
		}

		// First time create
		{
			createTexture(entry, desc);
		}
	}

	void RenderResourceRegistry::DestroyTexture(TextureIndex index, const RHIFencePoint& fencePoint) noexcept
	{
		auto& entry = m_TextureEntries[utils::ToIndex(index)];
		if (!entry.m_Allocated)
		{
			return;
		}

		m_TransientResourcePool->RetireTexture(std::move(entry.m_PhysicalAllocation), fencePoint);

		entry = {};
	}
}
