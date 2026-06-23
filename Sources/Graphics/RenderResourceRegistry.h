#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ShadowSettings.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	class RGGpuResourceAllocator;
	class RGExternalResourceRegistry;
	class DX12Texture;
	class DX12DescriptorManager;
	class SamplerRegistry;

	/*
	* Management runtime generated GPU Textures
	*/
	class RenderResourceRegistry
	{
	public:
		struct CreateInfo
		{
			RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
			RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;
			DX12DescriptorManager* m_DescriptorManager = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
		};

		struct IBLResourceCreateInfo
		{
			uint32_t m_EnvironmentCubemapSize = 512;
			RGFormat m_EnvironmentCubemapFormat = RGFormat::R16G16B16A16Float;

			uint32_t m_IrradianceCubemapSize = 32;
			RGFormat m_IrradianceCubemapFormat = RGFormat::R16G16B16A16Float;

			uint32_t m_PrefilteredSpecularCubemapSize = 128;
			uint32_t m_PrefilteredSpecularMipLevels = 5;
			RGFormat m_PrefilteredSpecularCubemapFormat = RGFormat::R16G16B16A16Float;

			uint32_t m_BrdfLutSize = 256;
			RGFormat m_BrdfLutFormat = RGFormat::R16G16Float;

			uint32_t m_PreviewIBLEnvironmentCubemapFaceSize = 256;
			RGFormat m_PreviewIBLEnvironmentCubemapFormat = RGFormat::R8G8B8A8Unorm;

			uint32_t m_PreviewIBLPrefilteredSpecularCubemapFaceSize = 256;
			RGFormat m_PreviewIBLPrefilteredSpecularCubemapFormat = RGFormat::R8G8B8A8Unorm;
		};

		enum class TextureIndex : uint8_t
		{
			IBL_EnvironmentCubemap,
			IBL_IrradianceCubemap,
			IBL_PrefilteredSpecularCubemap,
			IBL_BrdfLut,
			Preview_IBL_EnvironmentCubemap,
			Preview_IBL_PrefilteredSpecularCubemap,
			Preview_Shadow_DirectionalShadowMap,

			Count
		};

		enum class IBLPreviewLayout : uint32_t
		{
			Grid2x3,
			Cross,

			Count
		};

	private:
		struct TextureEntry
		{
			RGTextureDesc m_RgTexDesc{};
			ResourceIndex m_InternalIndex{};
			ResourceIndex m_ExternalIndex{};
			DX12DescriptorID m_SrvId{};
			TextureSrvCreateInfo m_SrvCreateInfo{};

			bool m_Allocated = false;
			bool m_Dirty = false;
		};

	public:
		explicit RenderResourceRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderResourceRegistry);
		~RenderResourceRegistry() = default;

		void EnsureIblResources(const IBLResourceCreateInfo& createInfo = {},
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;
		void EnsureShadowPreviewResources(uint32_t previewSize = DefaultDirectionalShadowMapPreviewSize,
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;

		void MarkDirty(TextureIndex index) noexcept;
		bool IsDirty(TextureIndex index) const noexcept;
		void ClearDirty(TextureIndex index) noexcept;

		DX12Texture* GetTexture(TextureIndex index) noexcept;
		ResourceIndex GetExternalIndex(TextureIndex index) const noexcept;
		DX12DescriptorID GetBindlessSrvId(TextureIndex index) const noexcept;
		uint32_t GetBindlessSrvIndex(TextureIndex index) const noexcept;
		D3D12_GPU_DESCRIPTOR_HANDLE GetBindlessSrvGpuHandle(TextureIndex index) const noexcept;

		void SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept;
		IBLPreviewLayout GetIBLEnvironmentPreviewLayout() const noexcept { return m_IBLEnvironmentPreviewLayout; }

		void SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout layout) noexcept;
		IBLPreviewLayout GetIBLPrefilteredSpecularPreviewLayout() const noexcept { return m_IBLPrefilteredSpecularPreviewLayout; }

		void SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept;
		uint32_t GetIBLPrefilteredSpecularPreviewMip() const noexcept { return m_IBLPrefilteredSpecularPreviewMip; }

		void FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept;

		void ReleaseAll(const DX12FencePoint& fencePoint) noexcept;

	private:
		void EnsureTexture(TextureIndex index,
			const RGTextureDesc& desc,
			const TextureSrvCreateInfo& srvCreateInfo,
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;

		void InvalidateDependents(TextureIndex index) noexcept;

		void DestroyTexture(TextureIndex index, const DX12FencePoint& fencePoint) noexcept;

	private:
		RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
		RGExternalResourceRegistry* m_ExternalResourceRegistry = nullptr;
		DX12DescriptorManager* m_DescriptorManager = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;

		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_TextureEntries;
		IBLPreviewLayout m_IBLEnvironmentPreviewLayout = IBLPreviewLayout::Cross;
		IBLPreviewLayout m_IBLPrefilteredSpecularPreviewLayout = IBLPreviewLayout::Cross;
		uint32_t m_IBLPrefilteredSpecularPreviewMip = 0;
	};
}
