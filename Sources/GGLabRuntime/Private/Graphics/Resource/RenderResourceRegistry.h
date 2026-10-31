#pragma once
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/IBLPreviewViewBase.h"
#include "GGLabRuntime/Graphics/IBLPreviewControlBase.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Resource/RenderTextureIndex.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
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
		public PostProcessPreviewControlBase,
		public ShadowPreviewViewBase,
		public ShadowPreviewControlBase,
		public IBLPreviewViewBase,
		public IBLPreviewControlBase,
		public RenderResourceRegistryAccess
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

		using TextureIndex = RenderTextureIndex;

	private:
		struct TextureEntry
		{
			RHITextureDesc m_TextureDesc{};
			TransientTextureAllocation m_PhysicalAllocation{};
			RHITextureViewHandle m_Srv{};
			RHITextureViewDesc m_SrvDesc{};

			uint64_t m_ContentRevision = 0;
			uint64_t m_PublishedContentRevision = 0;
			bool m_Allocated = false;
			bool m_Dirty = false;
		};

	public:
		explicit RenderResourceRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderResourceRegistry);
		~RenderResourceRegistry() override = default;

		[[nodiscard]] IBLPreviewResourcesDiagnostics GetIBLPreviewResourcesDiagnostics()
			const noexcept override;

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
		void RequestShadowPreview() noexcept override;
		[[nodiscard]] RenderPreviewRequest ObserveShadowPreviewRequest() const noexcept override;
		void PublishShadowPreview(uint64_t generation, uint64_t revision) noexcept override;
		void EnsureShadowPreviewResources(
			uint32_t previewSize = DefaultDirectionalShadowMapPreviewSize,
			const RHIFencePoint* retireFenceOpt = nullptr) noexcept;
		[[nodiscard]] ShadowPreviewDiagnostics GetShadowPreviewDiagnostics()
			const noexcept override;
		void EnsurePostProcessPreviewResources(uint32_t sourceWidth, uint32_t sourceHeight,
			const RHIFencePoint* retireFenceOpt = nullptr,
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) noexcept;

		void MarkDirty(TextureIndex index) noexcept;
		bool IsDirty(TextureIndex index) const noexcept;
		[[nodiscard]] uint64_t GetTextureContentRevision(TextureIndex index) const noexcept override;
		[[nodiscard]] uint64_t GetPublishedTextureContentRevision(TextureIndex index) const noexcept;
		void CommitTextureContent(TextureIndex index, uint64_t revision) noexcept override;

		const RHITextureDesc* GetTextureDesc(TextureIndex index) const noexcept;
		RHITextureHandle GetTextureHandle(TextureIndex index) noexcept;
		RHIDescriptorHandle GetSrvDescriptor(TextureIndex index) const noexcept;
		uint32_t GetShaderVisibleSrvIndex(TextureIndex index) const noexcept;
		const RHITextureDesc* GetIBLBakeTextureDesc(TextureIndex index) const noexcept;
		RHITextureHandle GetIBLBakeTextureHandle(TextureIndex index) noexcept;
		uint32_t GetIBLBakeShaderVisibleSrvIndex(TextureIndex index) const noexcept;

		void SetIBLEnvironmentPreviewLayout(IBLPreviewLayout layout) noexcept override;
		IBLPreviewLayout GetIBLEnvironmentPreviewLayout() const noexcept
		{
			return m_IBLEnvironmentPreviewLayout;
		}
		void SetIBLEnvironmentPreviewMip(uint32_t mip) noexcept override;
		uint32_t GetIBLEnvironmentPreviewMip() const noexcept { return m_IBLEnvironmentPreviewMip; }

		void SetIBLIrradiancePreviewLayout(IBLPreviewLayout layout) noexcept override;
		IBLPreviewLayout GetIBLIrradiancePreviewLayout() const noexcept
		{
			return m_IBLIrradiancePreviewLayout;
		}

		void SetIBLPrefilteredSpecularPreviewLayout(IBLPreviewLayout layout) noexcept override;
		IBLPreviewLayout GetIBLPrefilteredSpecularPreviewLayout() const noexcept
		{
			return m_IBLPrefilteredSpecularPreviewLayout;
		}

		void SetIBLPrefilteredSpecularPreviewMip(uint32_t mip) noexcept override;
		uint32_t GetIBLPrefilteredSpecularPreviewMip() const noexcept
		{
			return m_IBLPrefilteredSpecularPreviewMip;
		}

		void RequestIBLPreview(IBLPreviewType type) noexcept override;
		[[nodiscard]] RenderPreviewRequest ObserveIBLPreviewRequest(IBLPreviewType type) const noexcept override;
		void AcknowledgeIBLPreviewRequest(IBLPreviewType type, uint64_t generation) noexcept override;
		void MarkIBLPreviewDirty(IBLPreviewType type) noexcept;
		void MarkAllIBLPreviewsDirty() noexcept;
		void PublishIBLPreview(IBLPreviewType type, const RenderPreviewRequest& request,
			IBLPreviewLayout layout, uint32_t mip) noexcept override;
		[[nodiscard]] bool IsIBLPreviewDirty(IBLPreviewType type) const noexcept;
		[[nodiscard]] bool IsIBLPreviewRequested(IBLPreviewType type) const noexcept;
		[[nodiscard]] uint64_t GetIBLPreviewUpdateCount(IBLPreviewType type) const noexcept;

		[[nodiscard]] PostProcessPreviewDiagnostics GetPostProcessPreviewDiagnostics(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing)
			const noexcept override;
		void SetPostProcessPreviewSelection(PostProcessDebugSelection selection,
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) noexcept override;
		[[nodiscard]] PostProcessDebugSelection GetPostProcessPreviewSelection(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_Selection;
		}
		void SetPostProcessPreviewExposureEV(float exposureEV,
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) noexcept override;
		[[nodiscard]] float GetPostProcessPreviewExposureEV(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_ExposureEV;
		}
		void RequestPostProcessPreview(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) noexcept override;
		[[nodiscard]] RenderPreviewRequest ObservePostProcessPreviewRequest(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept override;
		void PublishPostProcessPreview(PostProcessDebugSelection selection,
			PostProcessPreviewChannel channel, uint64_t frameSerial,
			const RenderPreviewRequest& request) noexcept override;
		void BeginPreviewFrame() noexcept { m_GIPreviewSourceAvailable = false; }
		void SetGIPreviewSourceAvailable() noexcept override { m_GIPreviewSourceAvailable = true; }
		void PublishGIPreviewSnapshot(GIPreviewSnapshot snapshot) noexcept override
		{
			m_PostProcessPreviewStates[utils::ToIndex(PostProcessPreviewChannel::GlobalIllumination)]
				.m_GISnapshot = snapshot;
		}
		void InvalidatePostProcessPreview(PostProcessDebugSelection selection,
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) noexcept;
		[[nodiscard]] bool IsPostProcessPreviewRequested(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_Requested;
		}
		[[nodiscard]] bool HasPublishedPostProcessPreview(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_HasPublished;
		}
		[[nodiscard]] PostProcessDebugSelection GetPublishedPostProcessPreviewSelection(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing)
			const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_PublishedSelection;
		}
		[[nodiscard]] uint64_t GetPostProcessPreviewUpdateCount(
			PostProcessPreviewChannel channel = PostProcessPreviewChannel::PostProcessing) const noexcept
		{
			return m_PostProcessPreviewStates[utils::ToIndex(channel)].m_UpdateCount;
		}
		[[nodiscard]] static TextureIndex GetPostProcessPreviewTextureIndex(
			PostProcessPreviewChannel channel) noexcept;

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

		[[nodiscard]] IBLTextureDiagnostics BuildIBLTextureDiagnostics(TextureIndex index)
			const noexcept;

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
		uint64_t m_NextContentRevision = 0;
		uint64_t m_NextRequestGeneration = 0;
		struct IBLPreviewState
		{
			uint64_t m_RequestGeneration = 0;
			IBLPreviewLayout m_PublishedLayout = IBLPreviewLayout::Cross;
			uint32_t m_PublishedMip = 0;
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
			GIPreviewSnapshot m_GISnapshot{};
			float m_ExposureEV = 0.0f;
			uint64_t m_UpdateCount = 0;
			uint64_t m_PublishedFrameSerial = 0;
			uint64_t m_RequestGeneration = 0;
			bool m_Requested = false;
			bool m_HasPublished = false;
		};
		std::array<PostProcessPreviewState, utils::EnumCount<PostProcessPreviewChannel>()> m_PostProcessPreviewStates{};
		bool m_GIPreviewSourceAvailable = false;
		bool m_ShadowPreviewRequested = false;
		uint64_t m_ShadowPreviewRequestGeneration = 0;

	};
}
