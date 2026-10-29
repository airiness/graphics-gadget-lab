#pragma once
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace gglab
{
	class PipelineCache;
	class AssetManager;
	class AssetUploadScheduling;
	class EnvironmentLightingSystem;
	class FrameCaptureService;
	class IBLBakeScheduler;
	class AtmosphereSystem;
	class RenderResourceRegistry;
	class SamplerRegistry;
	class ShaderManager;
	class TaskSystem;
	class TransferManager;
	class GpuProfiler;
	class EnvironmentLightingControlBase;
	class EnvironmentLightingViewBase;
	class GpuProfilingControlBase;
	class GpuProfilingViewBase;
	class IBLCacheControlBase;
	class IBLPreviewControlBase;
	class IBLPreviewViewBase;
	class PostProcessPreviewControlBase;
	class PostProcessPreviewViewBase;
	class RenderFrameBuilder;
	class ShadowPreviewViewBase;
	class ShadowPreviewControlBase;
	class TemporalHistoryManager;
	class TemporalReferenceAccumulator;
	class GTAOTemporalHistory;
	struct RenderFrameOutcome;

	class Renderer : public RenderHost,
		public RenderFrameBufferAccess,
		public RenderEnvironmentAccess,
		public RenderPresentationAccess,
		public RenderBindingLayoutAccess,
		public RenderTemporalAccess,
		public RenderCompositionAccess
	{
	public:
		struct CreateInfo
		{
			const RHIContextFactoryBase* m_RHIContextFactory = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			std::filesystem::path m_IblDerivedDataCacheDirectory;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			std::optional<std::string> m_AdapterSelector;
			bool m_EnableDebugValidation = false;
			bool m_EnableGpuBasedValidation = false;

			[[nodiscard]] bool HasRequiredRuntimePaths() const noexcept
			{
				return !m_IblDerivedDataCacheDirectory.empty();
			}
		};

	public:
		Renderer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer() override;

		[[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
		[[nodiscard]] RenderServicesView PublishRenderServices(
			const RenderServices& bindings) noexcept;
		void CheckServiceLifecycleAccess() const noexcept override;
		void RevokeRenderServices() noexcept override;
		void Finalize() noexcept override;
		bool IsInitialized() const noexcept override { return m_IsInitialized; }

		[[nodiscard]] RenderFrame BeginFrame() noexcept override;
		[[nodiscard]] RenderFrameBuildResult BuildFrame(
			const RenderFrameBuildRequest& request) noexcept override;
		TemporalFrameTransaction& BeginTemporalFrame(RenderFrame& frame,
			const ResolvedTemporalFramePlan& plan, const ViewResolution& resolution,
			float scenePreExposure,
			const std::optional<TemporalReferenceSample>& referenceSample) noexcept override;
		void InvalidateTemporalFrameAfterLateContractFailure(RenderFrame& frame) noexcept override;
		void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept override;
		void Render(RenderFrame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept override;
		[[nodiscard]] RHIFrameEndResult EndFrame(RenderFrame& frame) noexcept override;

		RHIContext* GetRHIContext() const noexcept override { return m_RHIContext.get(); }
		RHIDevice* GetDevice() const noexcept override
		{
			return m_RHIContext ? &m_RHIContext->GetDevice() : nullptr;
		}
		RHISwapChain* GetSwapChain() const noexcept override
		{
			return m_RHIContext ? &m_RHIContext->GetSwapChain() : nullptr;
		}
		TransferManager* GetTransferManager() const noexcept override
		{
			return m_RHIContext ? &m_RHIContext->GetTransferManager() : nullptr;
		}
		AssetUploadScheduling* GetAssetUploadScheduler() const noexcept override
		{
			return m_AssetUploadScheduler.get();
		}
		PipelineCache* GetPipelineCache() const noexcept { return m_PipelineCache.get(); }
		EnvironmentSourceControl* GetEnvironmentSourceControl() const noexcept override;
		WorldLightingControlBase* GetWorldLightingControl() const noexcept override;
		EnvironmentLightingSystem* GetEnvironmentLightingSystemService() const noexcept
		{
			return m_EnvironmentLightingSystem.get();
		}
		AtmosphereSystem* GetAtmosphereSystem() const noexcept { return m_Atmosphere.get(); }
		AtmosphereSystem* GetBakeAtmosphereSystem() const noexcept { return m_BakeAtmosphere.get(); }
		IBLBakeScheduler* GetIBLBakeScheduler() const noexcept { return m_IBLBakeScheduler.get(); }
		RenderResourceRegistry* GetRenderResourceRegistry() const noexcept
		{
			return m_RenderResRegistry.get();
		}
		TransientResourcePool* GetTransientResourcePool() const noexcept
		{
			return m_TransientResourcePool.get();
		}
		PersistentTexturePool* GetPersistentTexturePool() const noexcept
		{
			return m_PersistentTexturePool.get();
		}
		GIReferenceObservation* GetGIReferenceObservation() const noexcept
		{
			return m_GIReferenceObservation.get();
		}
		[[nodiscard]] GIReferenceObservationDiagnostics GetGIReferenceObservationDiagnostics()
			const noexcept override;
		TemporalHistoryManager* GetTemporalHistoryManager() const noexcept
		{
			return m_TemporalHistoryManager.get();
		}
		RenderSamplerAccess* GetSamplerRegistry() const noexcept override;
		SamplerRegistry* GetSamplerRegistryService() const noexcept
		{
			return m_SamplerRegistry.get();
		}
		GpuProfiler* GetGpuProfiler() const noexcept
		{
			return m_RHIContext ? m_RHIContext->GetGpuProfiler() : nullptr;
		}
		// Narrow capability access for tooling and content. The concrete
		// environment, IBL, preview and profiling services remain Runtime-internal.
		[[nodiscard]] EnvironmentLightingViewBase* GetEnvironmentLightingView()
			const noexcept override;
		[[nodiscard]] EnvironmentLightingControlBase* GetEnvironmentLightingControl()
			const noexcept override;
		[[nodiscard]] IBLCacheControlBase* GetIBLCacheControl() const noexcept override;
		[[nodiscard]] IBLPreviewViewBase* GetIBLPreviewView() const noexcept override;
		[[nodiscard]] IBLPreviewControlBase* GetIBLPreviewControl() const noexcept override;
		[[nodiscard]] PostProcessPreviewViewBase* GetPostProcessPreviewView()
			const noexcept override;
		[[nodiscard]] PostProcessPreviewControlBase* GetPostProcessPreviewControl()
			const noexcept override;
		[[nodiscard]] ShadowPreviewViewBase* GetShadowPreviewView() const noexcept override;
		[[nodiscard]] ShadowPreviewControlBase* GetShadowPreviewControl() const noexcept override;
		[[nodiscard]] GpuProfilingViewBase* GetGpuProfilingView() const noexcept override;
		[[nodiscard]] GpuProfilingControlBase* GetGpuProfilingControl() const noexcept override;
		[[nodiscard]] FrameCaptureControlBase* GetFrameCaptureControl() const noexcept override;
		[[nodiscard]] RenderFrameCaptureAccess* GetFrameCaptureAccess() const noexcept;
		// Composition-time asset lease wiring for the IBL bake scheduler. The
		// scheduler and its derived-data ownership remain Runtime-internal.
		void AttachAssetManager(AssetManager& assetManager) noexcept override;
		void DetachAssetManager() noexcept override;
		const std::array<float, 4>& GetBackBufferClearColor() const noexcept override
		{
			return m_BackBufferClearColor;
		}
		const TemporalAACapabilityStatus& GetTemporalAACapabilityStatus() const noexcept override
		{
			return m_TemporalAACapabilityStatus;
		}
		const GTAOCapabilityStatus& GetGTAOCapabilityStatus() const noexcept override
		{
			return m_GTAOCapabilityStatus;
		}
		// Explicit environment access for pass services. The concrete
		// environment and bake services remain Runtime-internal.
		[[nodiscard]] const EnvironmentLightingSettings& GetEnvironmentLightingSettings()
			const noexcept override;
		[[nodiscard]] bool ShouldInitializeBakeResources() const noexcept override;
		[[nodiscard]] uint64_t GetBakingGeneration() const noexcept override;
		[[nodiscard]] const IBLBakeConfig& GetBakingConfig() const noexcept override;
		[[nodiscard]] const IBLBakeStatus& GetBakingStatus() const noexcept override;
		[[nodiscard]] uint64_t GetBakeRequestGeneration() const noexcept override;
		[[nodiscard]] IBLBakeStage GetStageForRecording() const noexcept override;
		void NotifyStageExecuted(IBLBakeStage stage, uint64_t generation) noexcept override;
		void NotifyBakeResourcesInitialized(uint64_t generation) noexcept override;
		[[nodiscard]] const EnvironmentTextureSource& GetBakingSource() const noexcept override;
		[[nodiscard]] const AtmosphereGPU* GetBakingAtmosphereParameters() const noexcept override;
		[[nodiscard]] Vector3 GetBakingSunDirection() const noexcept override;
		[[nodiscard]] const EnvironmentTextureSource& GetCommittedEnvironmentSource()
			const noexcept override;
		[[nodiscard]] ArtifactCacheCoreStatistics GetArtifactCacheStatistics()
			const noexcept override;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetDerivedDataStoreStatistics()
			const noexcept override;
		[[nodiscard]] TemporalHistoryManagerDiagnostics GetTemporalHistoryDiagnostics()
			const override;

		RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override
		{
			return m_CommonBindingLayout.GetNativeLayout();
		}
		[[nodiscard]] const ComposedLayout* GetCommonLayoutContract() const noexcept override
		{
			return m_CommonBindingLayout.GetContract();
		}
		[[nodiscard]] RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override;
		[[nodiscard]] static RHIBindingLayoutDesc BuildCommonRHIBindingLayoutDesc() noexcept;
		const PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() const noexcept override
		{
			return m_ObjectSB.get();
		}
		PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept override
		{
			return m_ObjectSB.get();
		}
		const PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer()
			const noexcept override
		{
			return m_MaterialSB.get();
		}
		PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() noexcept override
		{
			return m_MaterialSB.get();
		}
		const PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer()
			const noexcept override
		{
			return m_LightSB.get();
		}
		PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() noexcept override
		{
			return m_LightSB.get();
		}
		const PersistentStructuredBufferTable<uint64_t, ObjectGPU>* GetObjectStructuredBufferTable()
			const noexcept
		{
			return m_ObjectTable.get();
		}
		PersistentStructuredBufferTable<uint64_t, ObjectGPU>*
			GetObjectStructuredBufferTable() noexcept
		{
			return m_ObjectTable.get();
		}
		const PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>*
			GetMaterialStructuredBufferTable() const noexcept
		{
			return m_MaterialTable.get();
		}
		PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>*
			GetMaterialStructuredBufferTable() noexcept
		{
			return m_MaterialTable.get();
		}
		const PersistentStructuredBufferTable<uint64_t, LightGPU>* GetLightStructuredBufferTable()
			const noexcept
		{
			return m_LightTable.get();
		}
		PersistentStructuredBufferTable<uint64_t, LightGPU>*
			GetLightStructuredBufferTable() noexcept
		{
			return m_LightTable.get();
		}

		RenderGraph::CreateInfo CreateRenderGraphCreateInfo() const noexcept override;

		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnSuspend() noexcept override;
		void OnResume() noexcept override;
		bool IsSuspended() const noexcept override;

		RHIFencePoint GetLastSubmittedFencePoint() const noexcept override
		{
			return m_LastSubmittedFencePoint;
		}

	private:
		enum class FramePhase : uint8_t
		{
			Begun,
			Recorded,
		};

		struct ActiveFrameState
		{
			RHIFrameContext* m_RHIFrame = nullptr;
			// Persistent scene tables still arrive through the transfer queue.
			RHIFencePoint m_UploadFencePoint{};
			RenderGraph* m_RenderGraph = nullptr;
			uint64_t m_Serial = 0;
			uint32_t m_FrameSlotIndex = std::numeric_limits<uint32_t>::max();
			uint32_t m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
			FramePhase m_Phase = FramePhase::Begun;
			TemporalFrameTransaction m_TemporalTransaction{};
			RGDeferredCommitList m_DeferredCommits;
		};

		void CreateCommonBindingLayout() noexcept;
		void InitializeGpuBuffers() noexcept;
		void AbortFrame(uint64_t frameSerial) noexcept override;
		[[nodiscard]] RHIFrameEndResult AbortActiveFrame(uint64_t frameSerial) noexcept;
		void SettleFrame(const RenderFrameOutcome& outcome) noexcept;
		// Leaks every GPU-visible owner when teardown cannot verify idle on a live device.
		void AbandonGpuOwners() noexcept;
		void EndFrameLifetime() noexcept;

	private:
		std::unique_ptr<RHIContext> m_RHIContext;
		std::unique_ptr<AssetUploadScheduling> m_AssetUploadScheduler;
		std::unique_ptr<TransientResourcePool> m_TransientResourcePool;
		std::unique_ptr<PersistentTexturePool> m_PersistentTexturePool;
		std::unique_ptr<GIReferenceObservation> m_GIReferenceObservation;
		std::unique_ptr<TemporalHistoryManager> m_TemporalHistoryManager;
		std::unique_ptr<TemporalReferenceAccumulator> m_TemporalReferenceAccumulator;
		std::unique_ptr<GTAOTemporalHistory> m_GTAOTemporalHistory;
		std::unique_ptr<RenderFrameBuilder> m_FrameBuilder;
		// Outlives Finalize so the application can drain the final capture results.
		std::unique_ptr<FrameCaptureService> m_FrameCapture;
		std::unique_ptr<PipelineCache> m_PipelineCache;
		std::unique_ptr<EnvironmentLightingSystem> m_EnvironmentLightingSystem;
		std::unique_ptr<IBLBakeScheduler> m_IBLBakeScheduler;
		std::unique_ptr<AtmosphereSystem> m_Atmosphere;
		std::unique_ptr<AtmosphereSystem> m_BakeAtmosphere;
		std::unique_ptr<RenderResourceRegistry> m_RenderResRegistry;
		std::unique_ptr<SamplerRegistry> m_SamplerRegistry;
		ResolvedRenderLayout m_CommonBindingLayout{};
		std::array<float, 4> m_BackBufferClearColor{ 0.5f, 0.5f, 0.5f, 1.0f };
		TemporalAACapabilityStatus m_TemporalAACapabilityStatus{};
		GTAOCapabilityStatus m_GTAOCapabilityStatus{};
		TemporalViewHistory m_TemporalViewHistory{};
		TemporalObjectHistory m_TemporalObjectHistory{};

		std::unique_ptr<PersistentStructuredBuffer<ObjectGPU>> m_ObjectSB;
		std::unique_ptr<PersistentStructuredBuffer<MaterialGPU>> m_MaterialSB;
		std::unique_ptr<PersistentStructuredBuffer<LightGPU>> m_LightSB;
		std::unique_ptr<PersistentStructuredBufferTable<uint64_t, ObjectGPU>> m_ObjectTable;
		std::unique_ptr<PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>>
			m_MaterialTable;
		std::unique_ptr<PersistentStructuredBufferTable<uint64_t, LightGPU>> m_LightTable;

		std::atomic_bool m_IsInitialized = false;
		std::atomic_bool m_IsSuspended = false;

		RHIFencePoint m_LastSubmittedFencePoint = {};
		uint64_t m_NextFrameSerial = 1;
		ActiveFrameState m_ActiveFrame{};
		AssetManager* m_AttachedAssetManager = nullptr;
		bool m_HasActiveFrame = false;
		// Set once a frame's GPU completion became unknown; no further frame begins.
		bool m_Poisoned = false;
		std::unique_ptr<RenderServicesPublication> m_ServicePublication;
	};
}
