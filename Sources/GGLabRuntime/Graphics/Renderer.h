#pragma once
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	class PipelineCache;
	class AssetManager;
	class AssetUploadScheduler;
	class EnvironmentLightingSystem;
	class IBLBakeScheduler;
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
	class ShadowPreviewViewBase;
	class TemporalHistoryManager;
	struct RenderFrameGpuResources;
	struct RenderSceneGpuAllocations;

	class Renderer : public RenderHost
	{
	public:
		// Transitional alias for the Public RAII frame handle. The nested frame
		// type was replaced by RenderFrame when the render host contract landed.
		using Frame = RenderFrame;

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
		void Finalize() noexcept override;
		bool IsInitialized() const noexcept override { return m_IsInitialized; }

		[[nodiscard]] Frame BeginFrame() noexcept override;
		TemporalFrameTransaction& BeginTemporalFrame(Frame& frame,
			const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept override;
		void AdoptFrameGpuResources(Frame& frame,
			RenderSceneGpuAllocations& sceneGpuAllocations,
			const RHIFencePoint& uploadFencePoint) noexcept;
		void InvalidateTemporalFrameAfterLateContractFailure(Frame& frame) noexcept override;
		void InvalidateTemporalHistoryAfterResolveProgramChange() noexcept override;
		void Render(
			Frame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept override;
		[[nodiscard]] RHIFrameEndResult EndFrame(Frame& frame) noexcept override;

		RHIContext* GetRHIContext() const noexcept override { return m_RHIContext.get(); }
		RHIDevice* GetDevice() const noexcept
		{
			return m_RHIContext ? &m_RHIContext->GetDevice() : nullptr;
		}
		RHISwapChain* GetSwapChain() const noexcept
		{
			return m_RHIContext ? &m_RHIContext->GetSwapChain() : nullptr;
		}
		TransferManager* GetTransferManager() const noexcept
		{
			return m_RHIContext ? &m_RHIContext->GetTransferManager() : nullptr;
		}
		AssetUploadScheduler* GetAssetUploadScheduler() const noexcept
		{
			return m_AssetUploadScheduler.get();
		}
		PipelineCache* GetPipelineCache() const noexcept { return m_PipelineCache.get(); }
		EnvironmentLightingSystem* GetEnvironmentLightingSystem() const noexcept
		{
			return m_EnvironmentLightingSystem.get();
		}
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
		TemporalHistoryManager* GetTemporalHistoryManager() const noexcept
		{
			return m_TemporalHistoryManager.get();
		}
		SamplerRegistry* GetSamplerRegistry() const noexcept { return m_SamplerRegistry.get(); }
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
		[[nodiscard]] GpuProfilingViewBase* GetGpuProfilingView() const noexcept override;
		[[nodiscard]] GpuProfilingControlBase* GetGpuProfilingControl() const noexcept override;
		// Composition-time asset lease wiring for the IBL bake scheduler. The
		// scheduler and its derived-data ownership remain Runtime-internal.
		void AttachAssetManager(AssetManager& assetManager) noexcept;
		void DetachAssetManager() noexcept;
		const std::array<float, 4>& GetBackBufferClearColor() const noexcept
		{
			return m_BackBufferClearColor;
		}
		const TemporalAACapabilityStatus& GetTemporalAACapabilityStatus() const noexcept override
		{
			return m_TemporalAACapabilityStatus;
		}
		void PublishTemporalAAResolvePipelineClosure(bool available) noexcept
		{
			m_TemporalAACapabilityStatus.m_ResolveProgramAvailable = available;
		}

		RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept
		{
			return m_CommonBindingLayout;
		}
		[[nodiscard]] static RHIBindingLayoutDesc BuildCommonRHIBindingLayoutDesc() noexcept;

		const DynamicConstantBufferAllocator* GetSceneConstantBuffer() const noexcept
		{
			return m_SceneCB.get();
		}
		DynamicConstantBufferAllocator* GetSceneConstantBuffer() noexcept
		{
			return m_SceneCB.get();
		}
		const PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() const noexcept
		{
			return m_ObjectSB.get();
		}
		PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept
		{
			return m_ObjectSB.get();
		}
		const PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() const noexcept
		{
			return m_MaterialSB.get();
		}
		PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() noexcept
		{
			return m_MaterialSB.get();
		}
		const PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() const noexcept
		{
			return m_LightSB.get();
		}
		PersistentStructuredBuffer<LightGPU>* GetLightStructuredBuffer() noexcept
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
		const DynamicStructuredBufferAllocator<ViewGPU>* GetViewStructuredBuffer() const noexcept
		{
			return m_ViewSB.get();
		}
		DynamicStructuredBufferAllocator<ViewGPU>* GetViewStructuredBuffer() noexcept
		{
			return m_ViewSB.get();
		}

		RenderGraph::CreateInfo CreateRenderGraphCreateInfo() const noexcept override;

		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnSuspend() noexcept override;
		void OnResume() noexcept override;
		bool IsSuspended() const noexcept override;

		RHIFencePoint GetLastSubmittedFencePoint() const noexcept
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
			RenderGraph* m_RenderGraph = nullptr;
			uint64_t m_Serial = 0;
			uint32_t m_FrameSlotIndex = std::numeric_limits<uint32_t>::max();
			uint32_t m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
			FramePhase m_Phase = FramePhase::Begun;
			TemporalFrameTransaction m_TemporalTransaction{};
		};

		void CreateCommonBindingLayout() noexcept;
		void InitializeGpuBuffers() noexcept;
		void AbortFrame(uint64_t frameSerial) noexcept override;
		[[nodiscard]] RHIFencePoint AbortActiveFrame(uint64_t frameSerial) noexcept;
		void EndFrameLifetime() noexcept;
		void RetireSceneGpuAllocations(
			RenderSceneGpuAllocations* allocations, const RHIFencePoint& fencePoint) noexcept;

	private:
		std::unique_ptr<RHIContext> m_RHIContext;
		std::unique_ptr<AssetUploadScheduler> m_AssetUploadScheduler;
		std::unique_ptr<TransientResourcePool> m_TransientResourcePool;
		std::unique_ptr<PersistentTexturePool> m_PersistentTexturePool;
		std::unique_ptr<TemporalHistoryManager> m_TemporalHistoryManager;
		std::unique_ptr<RenderFrameGpuResources> m_FrameGpuResources;
		std::unique_ptr<PipelineCache> m_PipelineCache;
		std::unique_ptr<EnvironmentLightingSystem> m_EnvironmentLightingSystem;
		std::unique_ptr<IBLBakeScheduler> m_IBLBakeScheduler;
		std::unique_ptr<RenderResourceRegistry> m_RenderResRegistry;
		std::unique_ptr<SamplerRegistry> m_SamplerRegistry;
		RHIBindingLayoutHandle m_CommonBindingLayout{};
		std::array<float, 4> m_BackBufferClearColor{ 0.5f, 0.5f, 0.5f, 1.0f };
		TemporalAACapabilityStatus m_TemporalAACapabilityStatus{};
		TemporalViewHistory m_TemporalViewHistory{};
		TemporalObjectHistory m_TemporalObjectHistory{};

		std::unique_ptr<DynamicConstantBufferAllocator> m_SceneCB;

		std::unique_ptr<PersistentStructuredBuffer<ObjectGPU>> m_ObjectSB;
		std::unique_ptr<PersistentStructuredBuffer<MaterialGPU>> m_MaterialSB;
		std::unique_ptr<PersistentStructuredBuffer<LightGPU>> m_LightSB;
		std::unique_ptr<PersistentStructuredBufferTable<uint64_t, ObjectGPU>> m_ObjectTable;
		std::unique_ptr<PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>>
			m_MaterialTable;
		std::unique_ptr<PersistentStructuredBufferTable<uint64_t, LightGPU>> m_LightTable;
		std::unique_ptr<DynamicStructuredBufferAllocator<ViewGPU>> m_ViewSB;

		std::atomic_bool m_IsInitialized = false;
		std::atomic_bool m_IsSuspended = false;

		RHIFencePoint m_LastSubmittedFencePoint = {};
		uint64_t m_NextFrameSerial = 1;
		ActiveFrameState m_ActiveFrame{};
		bool m_HasActiveFrame = false;
	};
}
