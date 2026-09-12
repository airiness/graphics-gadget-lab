#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetCacheStatistics.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadControl.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/IBLBakeConfig.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassInfo.h"
#include "GGLabRuntime/Graphics/RenderTextureAssetAccess.h"
#include "GGLabRuntime/Graphics/Resource/RenderTextureIndex.h"
#include "GGLabRuntime/Graphics/SamplerTypes.h"
#include "GGLabRuntime/Graphics/Shader/ShaderTypes.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	class DynamicConstantBufferAllocator;
	class RenderPipelineOverlayExtensionBase;
	class RHIDevice;
	class RHISwapChain;
	template <typename T>
	class DynamicStructuredBufferAllocator;
	template <typename T>
	class PersistentStructuredBuffer;
	struct ComputePipelineRecipe;
	struct ComputePipelineSlot;
	struct EnvironmentTextureSource;
	struct GraphicsPhysicalPipelineKey;
	struct GraphicsPipelineSlot;

	// Explicit pass service contracts. Each interface is implemented by the
	// existing Runtime service owner; passes receive only the services they
	// actually consume instead of a concrete renderer handle.

	class RenderPipelineResolver
	{
	public:
		virtual ~RenderPipelineResolver() = default;

		[[nodiscard]] virtual RHIPipelineHandle Resolve(GraphicsPipelineSlot& slot,
			const GraphicsPhysicalPipelineKey& physicalKey,
			const RenderPassInfo& renderPassInfo) noexcept = 0;
		[[nodiscard]] virtual RHIPipelineHandle Resolve(ComputePipelineSlot& slot,
			const ComputePipelineRecipe& recipe,
			const RenderPassInfo& renderPassInfo) noexcept = 0;
		virtual void GetPipelineUsages(RHIPipelineHandle pipeline,
			std::vector<RenderPassInfo>& outUsages) const noexcept = 0;
	};

	class RenderShaderProgramAccess
	{
	public:
		virtual ~RenderShaderProgramAccess() = default;

		virtual ShaderID LoadProgram(const ShaderProgramRef& programRef) noexcept = 0;
		[[nodiscard]] virtual uint64_t GetGeneration(ShaderID shaderId) const noexcept = 0;
	};

	class RenderSamplerAccess
	{
	public:
		virtual ~RenderSamplerAccess() = default;

		virtual SamplerID GetOrCreateSampler(const SamplerKey& key) noexcept = 0;
		[[nodiscard]] virtual SamplerID GetPresetSamplerId(SamplerPreset preset)
			const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetSamplerIndex(SamplerPreset preset) const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetSamplerIndex(const SamplerID& samplerId)
			const noexcept = 0;
		[[nodiscard]] virtual uint32_t ResolveSamplerIndex(
			SamplerID samplerId, SamplerPreset fallbackPreset) const noexcept = 0;
	};

	class RenderResourceRegistryAccess
	{
	public:
		virtual ~RenderResourceRegistryAccess() = default;

		virtual void EnsureIBLBakeResources(const IBLBakeConfig& config,
			const RHIFencePoint* retireFence = nullptr) noexcept = 0;
		virtual void PublishIBLBakeResources() noexcept = 0;
		[[nodiscard]] virtual bool HasIBLBakeResources() const noexcept = 0;
		[[nodiscard]] virtual bool HasInitializedActiveIBL() const noexcept = 0;
		virtual void MarkActiveIBLInitialized() noexcept = 0;

		[[nodiscard]] virtual const RHITextureDesc* GetTextureDesc(
			RenderTextureIndex index) const noexcept = 0;
		virtual RHITextureHandle GetTextureHandle(RenderTextureIndex index) noexcept = 0;
		[[nodiscard]] virtual uint32_t GetShaderVisibleSrvIndex(
			RenderTextureIndex index) const noexcept = 0;
		[[nodiscard]] virtual bool IsDirty(RenderTextureIndex index) const noexcept = 0;
		virtual void ClearDirty(RenderTextureIndex index) noexcept = 0;
		virtual void MarkDirty(RenderTextureIndex index) noexcept = 0;

		[[nodiscard]] virtual const RHITextureDesc* GetIBLBakeTextureDesc(
			RenderTextureIndex index) const noexcept = 0;
		virtual RHITextureHandle GetIBLBakeTextureHandle(RenderTextureIndex index) noexcept = 0;
		[[nodiscard]] virtual uint32_t GetIBLBakeShaderVisibleSrvIndex(
			RenderTextureIndex index) const noexcept = 0;

		virtual void EnsureShadowPreviewResources(
			uint32_t previewSize = DefaultDirectionalShadowMapPreviewSize,
			const RHIFencePoint* retireFence = nullptr) noexcept = 0;
		virtual void EnsurePostProcessPreviewResources(uint32_t sourceWidth,
			uint32_t sourceHeight, const RHIFencePoint* retireFence = nullptr) noexcept = 0;

		[[nodiscard]] virtual bool IsPostProcessPreviewRequested() const noexcept = 0;
		[[nodiscard]] virtual bool ConsumePostProcessPreviewRequest() noexcept = 0;
		[[nodiscard]] virtual PostProcessDebugSelection GetPostProcessPreviewSelection()
			const noexcept = 0;
		virtual void SetPostProcessPreviewSelection(PostProcessDebugSelection selection) noexcept = 0;
		virtual void RequestPostProcessPreview() noexcept = 0;
		[[nodiscard]] virtual uint64_t GetPostProcessPreviewUpdateCount() const noexcept = 0;
		[[nodiscard]] virtual PostProcessDebugSelection GetPublishedPostProcessPreviewSelection()
			const noexcept = 0;
		[[nodiscard]] virtual float GetPostProcessPreviewExposureEV() const noexcept = 0;
		virtual void PublishPostProcessPreview(PostProcessDebugSelection selection) noexcept = 0;
		virtual void InvalidatePostProcessPreview(
			PostProcessDebugSelection selection) noexcept = 0;
		[[nodiscard]] virtual bool HasPublishedPostProcessPreview() const noexcept = 0;

		virtual void RequestIBLPreview(IBLPreviewType type) noexcept = 0;
		[[nodiscard]] virtual bool ConsumeIBLPreviewRequest(IBLPreviewType type) noexcept = 0;
		virtual void MarkIBLPreviewDirty(IBLPreviewType type) noexcept = 0;
		virtual void MarkAllIBLPreviewsDirty() noexcept = 0;
		virtual void ClearIBLPreviewDirty(IBLPreviewType type) noexcept = 0;
		[[nodiscard]] virtual bool IsIBLPreviewDirty(IBLPreviewType type) const noexcept = 0;
		[[nodiscard]] virtual bool IsIBLPreviewRequested(IBLPreviewType type) const noexcept = 0;
		[[nodiscard]] virtual IBLPreviewLayout GetIBLEnvironmentPreviewLayout()
			const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetIBLEnvironmentPreviewMip() const noexcept = 0;
		[[nodiscard]] virtual IBLPreviewLayout GetIBLIrradiancePreviewLayout()
			const noexcept = 0;
		[[nodiscard]] virtual IBLPreviewLayout GetIBLPrefilteredSpecularPreviewLayout()
			const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetIBLPrefilteredSpecularPreviewMip() const noexcept = 0;
	};

	class RenderFrameBufferAccess
	{
	public:
		virtual ~RenderFrameBufferAccess() = default;

		[[nodiscard]] virtual const DynamicConstantBufferAllocator* GetSceneConstantBuffer()
			const noexcept = 0;
		virtual DynamicConstantBufferAllocator* GetSceneConstantBuffer() noexcept = 0;

		[[nodiscard]] virtual const PersistentStructuredBuffer<ObjectGPU>*
			GetObjectStructuredBuffer() const noexcept = 0;
		virtual PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept = 0;

		[[nodiscard]] virtual const PersistentStructuredBuffer<MaterialGPU>*
			GetMaterialStructuredBuffer() const noexcept = 0;
		virtual PersistentStructuredBuffer<MaterialGPU>*
			GetMaterialStructuredBuffer() noexcept = 0;

		[[nodiscard]] virtual const PersistentStructuredBuffer<LightGPU>*
			GetLightStructuredBuffer() const noexcept = 0;
		virtual PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() noexcept = 0;

		[[nodiscard]] virtual const DynamicStructuredBufferAllocator<ViewGPU>*
			GetViewStructuredBuffer() const noexcept = 0;
		virtual DynamicStructuredBufferAllocator<ViewGPU>* GetViewStructuredBuffer() noexcept = 0;
	};

	class RenderEnvironmentAccess
	{
	public:
		virtual ~RenderEnvironmentAccess() = default;

		[[nodiscard]] virtual const EnvironmentLightingSettings& GetEnvironmentLightingSettings()
			const noexcept = 0;
		[[nodiscard]] virtual bool ShouldInitializeBakeResources() const noexcept = 0;
		[[nodiscard]] virtual uint64_t GetBakingGeneration() const noexcept = 0;
		[[nodiscard]] virtual const IBLBakeConfig& GetBakingConfig() const noexcept = 0;
		[[nodiscard]] virtual const IBLBakeStatus& GetBakingStatus() const noexcept = 0;
		[[nodiscard]] virtual IBLBakeStage GetStageForRecording() const noexcept = 0;
		virtual void NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept = 0;
		virtual void NotifyBakeResourcesInitialized(uint64_t generation) noexcept = 0;
		[[nodiscard]] virtual const EnvironmentTextureSource& GetBakingSource()
			const noexcept = 0;
		[[nodiscard]] virtual const EnvironmentTextureSource& GetCommittedEnvironmentSource()
			const noexcept = 0;
		[[nodiscard]] virtual ArtifactCacheCoreStatistics GetArtifactCacheStatistics()
			const noexcept = 0;
		[[nodiscard]] virtual LocalDerivedDataStoreStatistics GetDerivedDataStoreStatistics()
			const noexcept = 0;
	};

	class RenderPresentationAccess
	{
	public:
		virtual ~RenderPresentationAccess() = default;

		[[nodiscard]] virtual RHIContext* GetRHIContext() const noexcept = 0;
		[[nodiscard]] virtual RHIDevice* GetDevice() const noexcept = 0;
		[[nodiscard]] virtual RHISwapChain* GetSwapChain() const noexcept = 0;
		[[nodiscard]] virtual const std::array<float, 4>& GetBackBufferClearColor()
			const noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint GetLastSubmittedFencePoint() const noexcept = 0;
	};

	class RenderBindingLayoutAccess
	{
	public:
		virtual ~RenderBindingLayoutAccess() = default;

		[[nodiscard]] virtual RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept = 0;
		[[nodiscard]] virtual RHIBindingLayoutDesc GetCommonBindingLayoutDesc()
			const noexcept = 0;
	};

	class RenderTemporalAccess
	{
	public:
		virtual ~RenderTemporalAccess() = default;

		virtual void PublishTemporalAAResolvePipelineClosure(bool available) noexcept = 0;
		virtual void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept = 0;
		[[nodiscard]] virtual TemporalHistoryManagerDiagnostics GetTemporalHistoryDiagnostics()
			const = 0;
	};

	// Explicit services borrowed for one frame or pipeline invocation. The
	// interface fields are stable for the renderer lifetime.
	struct RenderServices
	{
		RenderPipelineResolver* m_PipelineResolver = nullptr;
		RenderShaderProgramAccess* m_ShaderPrograms = nullptr;
		RenderSamplerAccess* m_Samplers = nullptr;
		RenderResourceRegistryAccess* m_Resources = nullptr;
		RenderTextureAssetAccess* m_TextureAssets = nullptr;
		RenderFrameBufferAccess* m_FrameBuffers = nullptr;
		RenderEnvironmentAccess* m_Environment = nullptr;
		RenderPresentationAccess* m_Presentation = nullptr;
		RenderBindingLayoutAccess* m_BindingLayout = nullptr;
		RenderTemporalAccess* m_Temporal = nullptr;
		AssetUploadControl* m_AssetUpload = nullptr;
		RenderPipelineOverlayExtensionBase* m_OverlayExtension = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_PipelineResolver && m_ShaderPrograms && m_Samplers && m_Resources &&
				m_FrameBuffers && m_Environment && m_Presentation && m_BindingLayout &&
				m_Temporal && m_AssetUpload;
		}
	};
}
