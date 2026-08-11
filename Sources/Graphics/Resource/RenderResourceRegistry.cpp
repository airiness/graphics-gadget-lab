#include "Core/Precompiled.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Utility/TextureUtils.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		struct TextureLogicalNameEntry
		{
			std::string_view m_Scope;
			std::string_view m_Leaf;
			bool m_SupportsBakeRole = false;
		};

		constexpr std::array TextureLogicalNameEntries = {
			TextureLogicalNameEntry{"IBL.Active", "EnvironmentCubemap", true},
			TextureLogicalNameEntry{"IBL.Active", "IrradianceCubemap", true},
			TextureLogicalNameEntry{"IBL.Active", "PrefilteredSpecularCubemap", true},
			TextureLogicalNameEntry{"IBL.Active", "BrdfLut", true},
			TextureLogicalNameEntry{"Preview.IBL", "EnvironmentCubemap"},
			TextureLogicalNameEntry{"Preview.IBL", "IrradianceCubemap"},
			TextureLogicalNameEntry{"Preview.IBL", "PrefilteredSpecularCubemap"},
			TextureLogicalNameEntry{"Preview.Shadow", "DirectionalShadowMap"},
			TextureLogicalNameEntry{"Preview.PostProcess", "SelectedTap"},
		};
		static_assert(TextureLogicalNameEntries.size() ==
			utils::EnumCount<RenderResourceRegistry::TextureIndex>());

		std::string TextureLogicalName(
			RenderResourceRegistry::TextureIndex index, bool bakeResource) noexcept
		{
			const size_t entryIndex = utils::ToIndex(index);
			if (entryIndex >= TextureLogicalNameEntries.size())
			{
				return "Registry.Texture.Unknown";
			}

			const auto& entry = TextureLogicalNameEntries[entryIndex];
			GGLAB_ASSERT_MSG(!bakeResource || entry.m_SupportsBakeRole,
				"Only IBL registry textures support the bake role.");
			const std::string_view scope = bakeResource ? "IBL.Bake" : entry.m_Scope;
			return std::format("Registry.{}.{}", scope, entry.m_Leaf);
		}
	}

	RenderResourceRegistry::RenderResourceRegistry(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransientResourcePool(createInfo.m_TransientResourcePool),
		m_SamplerRegistry(createInfo.m_SamplerRegistry)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
		GGLAB_ASSERT_NOT_NULL(m_TransientResourcePool);
		GGLAB_ASSERT_NOT_NULL(m_SamplerRegistry);
	}

	void RenderResourceRegistry::EnsureIblResources(
		const IBLResourceCreateInfo& createInfo, const RHIFencePoint* retireFenceOpt) noexcept
	{
		// IBL_EnvironmentCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = {
				createInfo.m_EnvironmentCubemapSize, createInfo.m_EnvironmentCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels =
				static_cast<uint16_t>(CalculateMipLevelCount(createInfo.m_EnvironmentCubemapSize));
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_EnvironmentCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
				RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
			desc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;
			srvDesc.m_Subresources.m_ArraySliceCount = CubemapFaceCount;

			EnsureTexture(TextureIndex::IBL_EnvironmentCubemap, desc, srvDesc, retireFenceOpt);
		}

		// IBL_IrradianceCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = {
				createInfo.m_IrradianceCubemapSize, createInfo.m_IrradianceCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_IrradianceCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
				RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
			desc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;
			srvDesc.m_Subresources.m_ArraySliceCount = CubemapFaceCount;

			EnsureTexture(TextureIndex::IBL_IrradianceCubemap, desc, srvDesc, retireFenceOpt);
		}

		// IBL_PrefilteredSpecularCubemap
		{
			const uint32_t prefilteredMipLevels =
				std::clamp(createInfo.m_PrefilteredSpecularMipLevels, 1u,
					CalculateMipLevelCount(createInfo.m_PrefilteredSpecularCubemapSize));

			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_PrefilteredSpecularCubemapSize,
				createInfo.m_PrefilteredSpecularCubemapSize, 1u };
			desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
			desc.m_MipLevels = static_cast<uint16_t>(prefilteredMipLevels);
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PrefilteredSpecularCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
				RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
			desc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;
			srvDesc.m_Subresources.m_ArraySliceCount = CubemapFaceCount;

			EnsureTexture(
				TextureIndex::IBL_PrefilteredSpecularCubemap, desc, srvDesc, retireFenceOpt);
		}

		// IBL_BrdfLut
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_BrdfLutSize, createInfo.m_BrdfLutSize, 1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_BrdfLutFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
				RHITextureUsage::CopySource | RHITextureUsage::CopyDest;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;

			EnsureTexture(TextureIndex::IBL_BrdfLut, desc, srvDesc, retireFenceOpt);
		}

		// Preview_IBL_EnvironmentCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_PreviewIBLEnvironmentCubemapFaceSize * 4u,
				createInfo.m_PreviewIBLEnvironmentCubemapFaceSize * 3u, 1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PreviewIBLEnvironmentCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;

			EnsureTexture(
				TextureIndex::Preview_IBL_EnvironmentCubemap, desc, srvDesc, retireFenceOpt);
		}

		// Preview_IBL_IrradianceCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_PreviewIBLIrradianceCubemapFaceSize * 4u,
				createInfo.m_PreviewIBLIrradianceCubemapFaceSize * 3u, 1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PreviewIBLIrradianceCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;

			EnsureTexture(
				TextureIndex::Preview_IBL_IrradianceCubemap, desc, srvDesc, retireFenceOpt);
		}

		// Preview_IBL_PrefilteredSpecularCubemap
		{
			RHITextureDesc desc{};
			desc.m_Extent = { createInfo.m_PreviewIBLPrefilteredSpecularCubemapFaceSize * 4u,
				createInfo.m_PreviewIBLPrefilteredSpecularCubemapFaceSize * 3u, 1u };
			desc.m_ArraySize = 1;
			desc.m_MipLevels = 1;
			desc.m_SampleCount = 1;
			desc.m_Format = createInfo.m_PreviewIBLPrefilteredSpecularCubemapFormat;
			desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

			RHITextureViewDesc srvDesc{};
			srvDesc.m_Type = RHITextureViewType::ShaderResource;
			srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
			srvDesc.m_Format = desc.m_Format;
			srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;

			EnsureTexture(TextureIndex::Preview_IBL_PrefilteredSpecularCubemap, desc, srvDesc,
				retireFenceOpt);
		}
	}

	void RenderResourceRegistry::EnsureIBLBakeResources(
		const IBLBakeConfig& config, const RHIFencePoint* retireFenceOpt) noexcept
	{
		EnsureIBLTextureSet(m_IBLBakeTextureEntries, config, retireFenceOpt);
	}

	void RenderResourceRegistry::EnsureIBLTextureSet(
		std::array<TextureEntry, utils::EnumCount<TextureIndex>()>& entries,
		const IBLBakeConfig& config, const RHIFencePoint* retireFenceOpt) noexcept
	{
		auto ensureCubemap = [this, &entries, retireFenceOpt](TextureIndex index, uint32_t size,
			uint32_t mipLevels, RHIFormat format) noexcept
			{
				RHITextureDesc desc{};
				desc.m_Extent = { size, size, 1u };
				desc.m_ArraySize = static_cast<uint16_t>(CubemapFaceCount);
				desc.m_MipLevels = static_cast<uint16_t>(mipLevels);
				desc.m_Format = format;
				desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
					RHITextureUsage::CopySource | RHITextureUsage::CopyDest;
				desc.m_CreateFlags = RHITextureCreateFlags::CubeCompatible;

				RHITextureViewDesc srvDesc{};
				srvDesc.m_Type = RHITextureViewType::ShaderResource;
				srvDesc.m_Dimension = RHITextureViewDimension::TextureCube;
				srvDesc.m_Format = format;
				srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;
				srvDesc.m_Subresources.m_ArraySliceCount = CubemapFaceCount;
				EnsureTexture(entries, index, desc, srvDesc, retireFenceOpt);
			};

		const uint32_t environmentSize = std::max(config.m_EnvironmentCubemapSize, 1u);
		ensureCubemap(TextureIndex::IBL_EnvironmentCubemap, environmentSize,
			CalculateMipLevelCount(environmentSize), config.m_EnvironmentCubemapFormat);

		const uint32_t irradianceSize = std::max(config.m_IrradianceCubemapSize, 1u);
		ensureCubemap(TextureIndex::IBL_IrradianceCubemap, irradianceSize, 1,
			config.m_IrradianceCubemapFormat);

		const uint32_t specularSize = std::max(config.m_PrefilteredSpecularCubemapSize, 1u);
		ensureCubemap(TextureIndex::IBL_PrefilteredSpecularCubemap, specularSize,
			std::clamp(
				config.m_PrefilteredSpecularMipLevels, 1u, CalculateMipLevelCount(specularSize)),
			config.m_PrefilteredSpecularCubemapFormat);

		RHITextureDesc brdfDesc{};
		brdfDesc.m_Extent = {
			std::max(config.m_BrdfLutSize, 1u), std::max(config.m_BrdfLutSize, 1u), 1u };
		brdfDesc.m_Format = config.m_BrdfLutFormat;
		brdfDesc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
			RHITextureUsage::CopySource | RHITextureUsage::CopyDest;

		RHITextureViewDesc brdfSrvDesc{};
		brdfSrvDesc.m_Type = RHITextureViewType::ShaderResource;
		brdfSrvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
		brdfSrvDesc.m_Format = brdfDesc.m_Format;
		brdfSrvDesc.m_Subresources.m_MipCount = 1;
		EnsureTexture(entries, TextureIndex::IBL_BrdfLut, brdfDesc, brdfSrvDesc, retireFenceOpt);
	}

	bool RenderResourceRegistry::HasIBLBakeResources() const noexcept
	{
		for (uint32_t index = 0; index <= utils::ToIndex(TextureIndex::IBL_BrdfLut); ++index)
		{
			if (!m_IBLBakeTextureEntries[index].m_Allocated)
			{
				return false;
			}
		}
		return true;
	}

	void RenderResourceRegistry::PublishIBLBakeResources() noexcept
	{
		GGLAB_ASSERT_MSG(
			HasIBLBakeResources(), "IBL bake resources must be complete before publication.");
		if (!HasIBLBakeResources())
		{
			return;
		}

		for (uint32_t index = 0; index <= utils::ToIndex(TextureIndex::IBL_BrdfLut); ++index)
		{
			std::swap(m_TextureEntries[index], m_IBLBakeTextureEntries[index]);
			m_TextureEntries[index].m_Dirty = false;
			const auto textureIndex = static_cast<TextureIndex>(index);
			m_TransientResourcePool->SetTextureLogicalName(
				m_TextureEntries[index].m_PhysicalAllocation,
				TextureLogicalName(textureIndex, false));
			if (m_IBLBakeTextureEntries[index].m_PhysicalAllocation.IsValid())
			{
				m_TransientResourcePool->SetTextureLogicalName(
					m_IBLBakeTextureEntries[index].m_PhysicalAllocation,
					TextureLogicalName(textureIndex, true));
			}
		}
		m_HasInitializedActiveIBL = true;
		MarkAllIBLPreviewsDirty();
	}

	void RenderResourceRegistry::EnsureShadowPreviewResources(
		uint32_t previewSize, const RHIFencePoint* retireFenceOpt) noexcept
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
		srvDesc.m_Subresources.m_MipCount = desc.m_MipLevels;

		EnsureTexture(
			TextureIndex::Preview_Shadow_DirectionalShadowMap, desc, srvDesc, retireFenceOpt);
	}

	void RenderResourceRegistry::EnsurePostProcessPreviewResources(
		uint32_t sourceWidth, uint32_t sourceHeight, const RHIFencePoint* retireFenceOpt) noexcept
	{
		constexpr uint32_t MaxPreviewDimension = 512;
		const uint32_t safeWidth = std::max(sourceWidth, 1u);
		const uint32_t safeHeight = std::max(sourceHeight, 1u);
		const float scale = static_cast<float>(MaxPreviewDimension) /
			static_cast<float>(std::max(safeWidth, safeHeight));
		const uint32_t previewWidth =
			std::max(static_cast<uint32_t>(std::round(static_cast<float>(safeWidth) * scale)), 1u);
		const uint32_t previewHeight =
			std::max(static_cast<uint32_t>(std::round(static_cast<float>(safeHeight) * scale)), 1u);

		const auto index = TextureIndex::Preview_PostProcess;
		const auto& previousEntry = m_TextureEntries[utils::ToIndex(index)];
		const bool descriptorChanged =
			previousEntry.m_Allocated &&
			(previousEntry.m_TextureDesc.m_Extent.m_Width != previewWidth ||
				previousEntry.m_TextureDesc.m_Extent.m_Height != previewHeight);

		RHITextureDesc desc{};
		desc.m_Extent = { previewWidth, previewHeight, 1u };
		desc.m_ArraySize = 1;
		desc.m_MipLevels = 1;
		desc.m_SampleCount = 1;
		desc.m_Format = RHIFormat::R8G8B8A8Unorm;
		desc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;

		RHITextureViewDesc srvDesc{};
		srvDesc.m_Type = RHITextureViewType::ShaderResource;
		srvDesc.m_Dimension = RHITextureViewDimension::Texture2D;
		srvDesc.m_Format = desc.m_Format;
		srvDesc.m_Subresources.m_MipCount = 1;

		EnsureTexture(index, desc, srvDesc, retireFenceOpt);
		if (descriptorChanged)
		{
			m_PostProcessPreviewState.m_HasPublished = false;
		}
	}

	void RenderResourceRegistry::MarkDirty(TextureIndex index) noexcept
	{
		m_TextureEntries[utils::ToIndex(index)].m_Dirty = true;
		InvalidatePreviewForSource(index);
		InvalidateDependents(index);
	}

	void RenderResourceRegistry::InvalidatePreviewForSource(TextureIndex index) noexcept
	{
		switch (index)
		{
		case TextureIndex::IBL_EnvironmentCubemap:
		case TextureIndex::Preview_IBL_EnvironmentCubemap:
			MarkIBLPreviewDirty(IBLPreviewType::Environment);
			break;
		case TextureIndex::IBL_IrradianceCubemap:
		case TextureIndex::Preview_IBL_IrradianceCubemap:
			MarkIBLPreviewDirty(IBLPreviewType::Irradiance);
			break;
		case TextureIndex::IBL_PrefilteredSpecularCubemap:
		case TextureIndex::Preview_IBL_PrefilteredSpecularCubemap:
			MarkIBLPreviewDirty(IBLPreviewType::PrefilteredSpecular);
			break;
		default:
			break;
		}
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

	const RHITextureDesc* RenderResourceRegistry::GetIBLBakeTextureDesc(
		TextureIndex index) const noexcept
	{
		const auto& entry = m_IBLBakeTextureEntries[utils::ToIndex(index)];
		return entry.m_Allocated ? &entry.m_TextureDesc : nullptr;
	}

	RHITextureHandle RenderResourceRegistry::GetIBLBakeTextureHandle(TextureIndex index) noexcept
	{
		auto& entry = m_IBLBakeTextureEntries[utils::ToIndex(index)];
		return entry.m_Allocated ? entry.m_PhysicalAllocation.m_Texture : RHITextureHandle{};
	}

	uint32_t RenderResourceRegistry::GetIBLBakeShaderVisibleSrvIndex(
		TextureIndex index) const noexcept
	{
		const auto& entry = m_IBLBakeTextureEntries[utils::ToIndex(index)];
		GGLAB_ASSERT_MSG(
			entry.m_Allocated && entry.m_Srv.IsValid(), "IBL bake texture SRV is unavailable.");
		const RHIDescriptorHandle descriptor = m_Device->GetTextureViewDescriptor(entry.m_Srv);
		GGLAB_ASSERT_MSG(descriptor.IsValid(), "IBL bake texture SRV descriptor is invalid.");
		return descriptor.m_Index;
	}

	void RenderResourceRegistry::SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept
	{
		if (layout >= IBLPreviewLayout::Count)
		{
			return;
		}

		m_IBLEnvironmentPreviewLayout = layout;
		MarkIBLPreviewDirty(IBLPreviewType::Environment);
	}

	void RenderResourceRegistry::SetIBLEnvironmentPreviewMip(uint32_t mip) noexcept
	{
		if (m_IBLEnvironmentPreviewMip != mip)
		{
			m_IBLEnvironmentPreviewMip = mip;
			MarkIBLPreviewDirty(IBLPreviewType::Environment);
		}
	}

	void RenderResourceRegistry::SetIBLIrradiancePreviewLayout(IBLPreviewLayout layout) noexcept
	{
		if (layout >= IBLPreviewLayout::Count || m_IBLIrradiancePreviewLayout == layout)
		{
			return;
		}

		m_IBLIrradiancePreviewLayout = layout;
		MarkIBLPreviewDirty(IBLPreviewType::Irradiance);
	}

	void RenderResourceRegistry::SetIBLPrefilteredSpecularPreviewLayout(
		IBLPreviewLayout layout) noexcept
	{
		if (layout >= IBLPreviewLayout::Count)
		{
			return;
		}

		if (m_IBLPrefilteredSpecularPreviewLayout != layout)
		{
			m_IBLPrefilteredSpecularPreviewLayout = layout;
			MarkIBLPreviewDirty(IBLPreviewType::PrefilteredSpecular);
		}
	}

	void RenderResourceRegistry::SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept
	{
		if (m_IBLPrefilteredSpecularPreviewMip != mip)
		{
			m_IBLPrefilteredSpecularPreviewMip = mip;
			MarkIBLPreviewDirty(IBLPreviewType::PrefilteredSpecular);
		}
	}

	void RenderResourceRegistry::RequestIBLPreview(IBLPreviewType type) noexcept
	{
		m_IBLPreviewStates[utils::ToIndex(type)].m_Requested = true;
	}

	bool RenderResourceRegistry::ConsumeIBLPreviewRequest(IBLPreviewType type) noexcept
	{
		auto& state = m_IBLPreviewStates[utils::ToIndex(type)];
		const bool shouldUpdate = state.m_Requested && state.m_Dirty;
		state.m_Requested = false;
		return shouldUpdate;
	}

	void RenderResourceRegistry::MarkIBLPreviewDirty(IBLPreviewType type) noexcept
	{
		m_IBLPreviewStates[utils::ToIndex(type)].m_Dirty = true;
		m_TextureEntries[utils::ToIndex(GetPreviewTextureIndex(type))].m_Dirty = true;
	}

	void RenderResourceRegistry::MarkAllIBLPreviewsDirty() noexcept
	{
		MarkIBLPreviewDirty(IBLPreviewType::Environment);
		MarkIBLPreviewDirty(IBLPreviewType::Irradiance);
		MarkIBLPreviewDirty(IBLPreviewType::PrefilteredSpecular);
	}

	void RenderResourceRegistry::ClearIBLPreviewDirty(IBLPreviewType type) noexcept
	{
		auto& state = m_IBLPreviewStates[utils::ToIndex(type)];
		state.m_Dirty = false;
		++state.m_UpdateCount;
		m_TextureEntries[utils::ToIndex(GetPreviewTextureIndex(type))].m_Dirty = false;
	}

	bool RenderResourceRegistry::IsIBLPreviewDirty(IBLPreviewType type) const noexcept
	{
		return m_IBLPreviewStates[utils::ToIndex(type)].m_Dirty;
	}

	bool RenderResourceRegistry::IsIBLPreviewRequested(IBLPreviewType type) const noexcept
	{
		return m_IBLPreviewStates[utils::ToIndex(type)].m_Requested;
	}

	uint64_t RenderResourceRegistry::GetIBLPreviewUpdateCount(IBLPreviewType type) const noexcept
	{
		return m_IBLPreviewStates[utils::ToIndex(type)].m_UpdateCount;
	}

	void RenderResourceRegistry::SetPostProcessPreviewSelection(
		PostProcessDebugSelection selection) noexcept
	{
		if (selection.m_Tap >= PostProcessDebugTap::Count)
		{
			return;
		}
		selection.m_BloomPyramidLevel =
			std::min(selection.m_BloomPyramidLevel, MaxBloomPyramidLevels - 1u);
		m_PostProcessPreviewState.m_Selection = selection;
	}

	void RenderResourceRegistry::SetPostProcessPreviewExposureEV(float exposureEV) noexcept
	{
		m_PostProcessPreviewState.m_ExposureEV = std::clamp(exposureEV, -8.0f, 8.0f);
	}

	void RenderResourceRegistry::RequestPostProcessPreview() noexcept
	{
		m_PostProcessPreviewState.m_Requested = true;
	}

	bool RenderResourceRegistry::ConsumePostProcessPreviewRequest() noexcept
	{
		const bool requested = m_PostProcessPreviewState.m_Requested;
		m_PostProcessPreviewState.m_Requested = false;
		return requested;
	}

	void RenderResourceRegistry::PublishPostProcessPreview(
		PostProcessDebugSelection selection) noexcept
	{
		m_PostProcessPreviewState.m_PublishedSelection = selection;
		m_PostProcessPreviewState.m_HasPublished = true;
		++m_PostProcessPreviewState.m_UpdateCount;
		m_TextureEntries[utils::ToIndex(TextureIndex::Preview_PostProcess)].m_Dirty = false;
	}

	void RenderResourceRegistry::InvalidatePostProcessPreview(
		PostProcessDebugSelection selection) noexcept
	{
		if (m_PostProcessPreviewState.m_HasPublished &&
			m_PostProcessPreviewState.m_PublishedSelection == selection)
		{
			m_PostProcessPreviewState.m_HasPublished = false;
		}
	}

	RenderResourceRegistry::TextureIndex RenderResourceRegistry::GetPreviewTextureIndex(
		IBLPreviewType type) noexcept
	{
		switch (type)
		{
		case IBLPreviewType::Environment:
			return TextureIndex::Preview_IBL_EnvironmentCubemap;
		case IBLPreviewType::Irradiance:
			return TextureIndex::Preview_IBL_IrradianceCubemap;
		case IBLPreviewType::PrefilteredSpecular:
			return TextureIndex::Preview_IBL_PrefilteredSpecularCubemap;
		default:
			GGLAB_ASSERT_MSG(false, "Unknown IBL preview type.");
			return TextureIndex::Preview_IBL_EnvironmentCubemap;
		}
	}

	void RenderResourceRegistry::FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept
	{
		out = {};

		auto fillTextureSamplerBinding = [this](TextureIndex index,
			TextureSamplerBindingGPU& outBinding,
			SamplerPreset samplerPreset) noexcept
			{
				const auto& entry = m_TextureEntries[utils::ToIndex(index)];
				GGLAB_ASSERT_MSG(entry.m_Allocated, "IBL resource is not allocated.");
				GGLAB_ASSERT_MSG(entry.m_Srv.IsValid(), "IBL resource SRV is invalid.");

				outBinding.TextureIndex = GetShaderVisibleSrvIndex(index);
				outBinding.SamplerIndex = m_SamplerRegistry->GetSamplerIndex(samplerPreset);
			};

		fillTextureSamplerBinding(TextureIndex::IBL_EnvironmentCubemap, out.EnvironmentBinding,
			SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(
			TextureIndex::IBL_IrradianceCubemap, out.IrradianceBinding, SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(TextureIndex::IBL_PrefilteredSpecularCubemap,
			out.PrefilteredSpecularBinding, SamplerPreset::LinearClamp);

		fillTextureSamplerBinding(
			TextureIndex::IBL_BrdfLut, out.BrdfLutBinding, SamplerPreset::LinearClamp);

		const auto& prefilteredEntry =
			m_TextureEntries[utils::ToIndex(TextureIndex::IBL_PrefilteredSpecularCubemap)];
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
		for (size_t index = 0; index < m_IBLBakeTextureEntries.size(); ++index)
		{
			auto& entry = m_IBLBakeTextureEntries[index];
			if (entry.m_Allocated)
			{
				m_TransientResourcePool->RetireTexture(
					std::move(entry.m_PhysicalAllocation), fencePoint);
				entry = {};
			}
		}
		m_HasInitializedActiveIBL = false;
		MarkAllIBLPreviewsDirty();
		m_PostProcessPreviewState.m_Requested = false;
		m_PostProcessPreviewState.m_HasPublished = false;
	}

	void RenderResourceRegistry::EnsureTexture(TextureIndex index, const RHITextureDesc& desc,
		const RHITextureViewDesc& srvDesc, const RHIFencePoint* retireFenceOpt) noexcept
	{
		EnsureTexture(m_TextureEntries, index, desc, srvDesc, retireFenceOpt);
	}

	void RenderResourceRegistry::EnsureTexture(
		std::array<TextureEntry, utils::EnumCount<TextureIndex>()>& entries, TextureIndex index,
		const RHITextureDesc& desc, const RHITextureViewDesc& srvDesc,
		const RHIFencePoint* retireFenceOpt) noexcept
	{
		auto& entry = entries[utils::ToIndex(index)];

		// Acquire a physical texture allocation from the reusable pool.
		auto createTexture = [this, index, &srvDesc, &entries](
			TextureEntry& outEntry, const RHITextureDesc& desc) noexcept
			{
				const bool bakeResource = &entries == &m_IBLBakeTextureEntries;
				auto allocation = m_TransientResourcePool->AcquireTexture(desc,
					TextureLogicalName(index, bakeResource), RHIResourceDebugBindingMode::Aliased);
				GGLAB_ASSERT_MSG(
					allocation.IsValid(), "RenderResourceRegistry: Acquire texture failed.");
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

				outEntry.m_Dirty = true;
				if (&entries == &m_TextureEntries)
				{
					InvalidatePreviewForSource(index);
					InvalidateDependents(index);
				}
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
				GGLAB_LOG_GRAPHICS_ERROR(
					"RenderResourceRegistry: desc changed but no fence, keep old resource (index={}).",
					static_cast<uint32_t>(index));
				return;
			}

			m_TransientResourcePool->RetireTexture(
				std::move(entry.m_PhysicalAllocation), *retireFenceOpt);

			// Create new
			createTexture(entry, desc);
			return;
		}

		// First time create
		{
			createTexture(entry, desc);
		}
	}

	void RenderResourceRegistry::DestroyTexture(
		TextureIndex index, const RHIFencePoint& fencePoint) noexcept
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
