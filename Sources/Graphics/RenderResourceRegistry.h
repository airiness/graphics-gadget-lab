#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ShadowSettings.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITexture.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	class RHIDevice;
	class RGGpuResourceAllocator;
	class DX12Texture;
	class SamplerRegistry;
	class DX12FencePoint;

	/*
	* Management runtime generated GPU Textures
	*/
	class RenderResourceRegistry
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
		};

		struct IBLResourceCreateInfo
		{
			uint32_t m_EnvironmentCubemapSize = 512;
			RHIFormat m_EnvironmentCubemapFormat = RHIFormat::R16G16B16A16Float;

			uint32_t m_IrradianceCubemapSize = 32;
			RHIFormat m_IrradianceCubemapFormat = RHIFormat::R16G16B16A16Float;

			uint32_t m_PrefilteredSpecularCubemapSize = 128;
			uint32_t m_PrefilteredSpecularMipLevels = 5;
			RHIFormat m_PrefilteredSpecularCubemapFormat = RHIFormat::R16G16B16A16Float;

			uint32_t m_BrdfLutSize = 256;
			RHIFormat m_BrdfLutFormat = RHIFormat::R16G16Float;

			uint32_t m_PreviewIBLEnvironmentCubemapFaceSize = 256;
			RHIFormat m_PreviewIBLEnvironmentCubemapFormat = RHIFormat::R8G8B8A8Unorm;

			uint32_t m_PreviewIBLPrefilteredSpecularCubemapFaceSize = 256;
			RHIFormat m_PreviewIBLPrefilteredSpecularCubemapFormat = RHIFormat::R8G8B8A8Unorm;
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
			RHITextureDesc m_TextureDesc{};
			ResourceIndex m_InternalIndex{};
			RHITextureViewHandle m_Srv{};
			RHITextureViewDesc m_SrvDesc{};

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
		RHITextureHandle GetTextureHandle(TextureIndex index) noexcept;
		RHIDescriptorHandle GetSrvDescriptor(TextureIndex index) const noexcept;
		uint32_t GetShaderVisibleSrvIndex(TextureIndex index) const noexcept;

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
			const RHITextureDesc& desc,
			const RHITextureViewDesc& srvDesc,
			const DX12FencePoint* retireFenceOpt = nullptr) noexcept;

		void InvalidateDependents(TextureIndex index) noexcept;

		void DestroyTexture(TextureIndex index, const DX12FencePoint& fencePoint) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		RGGpuResourceAllocator* m_RGGpuResAllocator = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;

		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_TextureEntries;
		IBLPreviewLayout m_IBLEnvironmentPreviewLayout = IBLPreviewLayout::Cross;
		IBLPreviewLayout m_IBLPrefilteredSpecularPreviewLayout = IBLPreviewLayout::Cross;
		uint32_t m_IBLPrefilteredSpecularPreviewMip = 0;
	};
}
