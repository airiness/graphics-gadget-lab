#pragma once
#include "Graphics/Resource/TransientResourcePool.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabFoundation/Base/TypeUtils.h"

namespace gglab
{
	class RHIDevice;
	class SamplerRegistry;

	/*
	* Management runtime generated GPU Textures
	*/
	class RenderResourceRegistry : public PostProcessPreviewViewBase,
		public PostProcessPreviewControlBase
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TransientResourcePool* m_TransientResourcePool = nullptr;
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

			uint32_t m_PreviewIBLIrradianceCubemapFaceSize = 256;
			RHIFormat m_PreviewIBLIrradianceCubemapFormat = RHIFormat::R8G8B8A8Unorm;

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
			Preview_IBL_IrradianceCubemap,
			Preview_IBL_PrefilteredSpecularCubemap,
			Preview_Shadow_DirectionalShadowMap,
			Preview_PostProcess,

			Count
		};

		enum class IBLPreviewType : uint8_t
		{
			Environment,
			Irradiance,
			PrefilteredSpecular,

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
			TransientTextureAllocation m_PhysicalAllocation{};
			RHITextureViewHandle m_Srv{};
			RHITextureViewDesc m_SrvDesc{};

			bool m_Allocated = false;
			bool m_Dirty = false;
		};

	public:
		explicit RenderResourceRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderResourceRegistry);
		~RenderResourceRegistry() override = default;

		void EnsureIblResources(const IBLResourceCreateInfo& createInfo = {},
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		void EnsureIBLBakeResources(
			const IBLBakeConfig& config, const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		void PublishIBLBakeResources() noexcept;
		[[nodiscard]] bool HasIBLBakeResources() const noexcept;
		[[nodiscard]] bool HasInitializedActiveIBL() const noexcept
		{
			return m_HasInitializedActiveIBL;
		}
		void MarkActiveIBLInitialized() noexcept { m_HasInitializedActiveIBL = true; }
		void EnsureShadowPreviewResources(
			uint32_t previewSize = DefaultDirectionalShadowMapPreviewSize,
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		void EnsurePostProcessPreviewResources(uint32_t sourceWidth, uint32_t sourceHeight,
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;

		void MarkDirty(TextureIndex index) noexcept;
		bool IsDirty(TextureIndex index) const noexcept;
		void ClearDirty(TextureIndex index) noexcept;

		const RHITextureDesc* GetTextureDesc(TextureIndex index) const noexcept;
		RHITextureHandle GetTextureHandle(TextureIndex index) noexcept;
		RHIDescriptorHandle GetSrvDescriptor(TextureIndex index) const noexcept;
		uint32_t GetShaderVisibleSrvIndex(TextureIndex index) const noexcept;
		const RHITextureDesc* GetIBLBakeTextureDesc(TextureIndex index) const noexcept;
		RHITextureHandle GetIBLBakeTextureHandle(TextureIndex index) noexcept;
		uint32_t GetIBLBakeShaderVisibleSrvIndex(TextureIndex index) const noexcept;

		void SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept;
		IBLPreviewLayout GetIBLEnvironmentPreviewLayout() const noexcept
		{
			return m_IBLEnvironmentPreviewLayout;
		}
		void SetIBLEnvironmentPreviewMip(uint32_t mip) noexcept;
		uint32_t GetIBLEnvironmentPreviewMip() const noexcept { return m_IBLEnvironmentPreviewMip; }

		void SetIBLIrradiancePreviewLayout(IBLPreviewLayout layout) noexcept;
		IBLPreviewLayout GetIBLIrradiancePreviewLayout() const noexcept
		{
			return m_IBLIrradiancePreviewLayout;
		}

		void SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout layout) noexcept;
		IBLPreviewLayout GetIBLPrefilteredSpecularPreviewLayout() const noexcept
		{
			return m_IBLPrefilteredSpecularPreviewLayout;
		}

		void SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept;
		uint32_t GetIBLPrefilteredSpecularPreviewMip() const noexcept
		{
			return m_IBLPrefilteredSpecularPreviewMip;
		}

		void RequestIBLPreview(IBLPreviewType type) noexcept;
		[[nodiscard]] bool ConsumeIBLPreviewRequest(IBLPreviewType type) noexcept;
		void MarkIBLPreviewDirty(IBLPreviewType type) noexcept;
		void MarkAllIBLPreviewsDirty() noexcept;
		void ClearIBLPreviewDirty(IBLPreviewType type) noexcept;
		[[nodiscard]] bool IsIBLPreviewDirty(IBLPreviewType type) const noexcept;
		[[nodiscard]] bool IsIBLPreviewRequested(IBLPreviewType type) const noexcept;
		[[nodiscard]] uint64_t GetIBLPreviewUpdateCount(IBLPreviewType type) const noexcept;

		[[nodiscard]] PostProcessPreviewDiagnostics GetPostProcessPreviewDiagnostics()
			const noexcept override;
		void SetPostProcessPreviewSelection(PostProcessDebugSelection selection) noexcept override;
		[[nodiscard]] PostProcessDebugSelection GetPostProcessPreviewSelection() const noexcept
		{
			return m_PostProcessPreviewState.m_Selection;
		}
		void SetPostProcessPreviewExposureEV(float exposureEV) noexcept override;
		[[nodiscard]] float GetPostProcessPreviewExposureEV() const noexcept
		{
			return m_PostProcessPreviewState.m_ExposureEV;
		}
		void RequestPostProcessPreview() noexcept override;
		[[nodiscard]] bool ConsumePostProcessPreviewRequest() noexcept;
		void PublishPostProcessPreview(PostProcessDebugSelection selection) noexcept;
		void InvalidatePostProcessPreview(PostProcessDebugSelection selection) noexcept;
		[[nodiscard]] bool IsPostProcessPreviewRequested() const noexcept
		{
			return m_PostProcessPreviewState.m_Requested;
		}
		[[nodiscard]] bool HasPublishedPostProcessPreview() const noexcept
		{
			return m_PostProcessPreviewState.m_HasPublished;
		}
		[[nodiscard]] PostProcessDebugSelection GetPublishedPostProcessPreviewSelection()
			const noexcept
		{
			return m_PostProcessPreviewState.m_PublishedSelection;
		}
		[[nodiscard]] uint64_t GetPostProcessPreviewUpdateCount() const noexcept
		{
			return m_PostProcessPreviewState.m_UpdateCount;
		}

		void FillIBLBindlessGPU(IBLResourceGPU& out) const noexcept;

		void ReleaseAll(const RHIFencePoint& fencePoint) noexcept;

	private:
		void EnsureTexture(TextureIndex index, const RHITextureDesc& desc,
			const RHITextureViewDesc& srvDesc,
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		void EnsureTexture(std::array<TextureEntry, utils::EnumCount<TextureIndex>()>& entries,
			TextureIndex index, const RHITextureDesc& desc, const RHITextureViewDesc& srvDesc,
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		void EnsureIBLTextureSet(
			std::array<TextureEntry, utils::EnumCount<TextureIndex>()>& entries,
			const IBLBakeConfig& config, const RHIFencePoint* retireFenceOpt) noexcept;

		void InvalidateDependents(TextureIndex index) noexcept;
		void InvalidatePreviewForSource(TextureIndex index) noexcept;
		[[nodiscard]] static TextureIndex GetPreviewTextureIndex(IBLPreviewType type) noexcept;

		void DestroyTexture(TextureIndex index, const RHIFencePoint& fencePoint) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransientResourcePool* m_TransientResourcePool = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;

		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_TextureEntries;
		std::array<TextureEntry, utils::EnumCount<TextureIndex>()> m_IBLBakeTextureEntries;
		bool m_HasInitializedActiveIBL = false;
		struct IBLPreviewState
		{
			bool m_Requested = false;
			bool m_Dirty = true;
			uint64_t m_UpdateCount = 0;
		};
		std::array<IBLPreviewState, utils::EnumCount<IBLPreviewType>()> m_IBLPreviewStates;
		IBLPreviewLayout m_IBLEnvironmentPreviewLayout = IBLPreviewLayout::Cross;
		uint32_t m_IBLEnvironmentPreviewMip = 0;
		IBLPreviewLayout m_IBLIrradiancePreviewLayout = IBLPreviewLayout::Cross;
		IBLPreviewLayout m_IBLPrefilteredSpecularPreviewLayout = IBLPreviewLayout::Cross;
		uint32_t m_IBLPrefilteredSpecularPreviewMip = 0;
		struct PostProcessPreviewState
		{
			PostProcessDebugSelection m_Selection{};
			PostProcessDebugSelection m_PublishedSelection{};
			float m_ExposureEV = 0.0f;
			uint64_t m_UpdateCount = 0;
			bool m_Requested = false;
			bool m_HasPublished = false;
		};
		PostProcessPreviewState m_PostProcessPreviewState{};
	};
}
