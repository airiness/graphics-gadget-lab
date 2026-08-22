#pragma once
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Resource/TransientResourcePool.h"
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderParameters.h"
#include "Graphics/RenderScene.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	class PipelineCache;
	class AssetUploadScheduler;
	class EnvironmentLightingSystem;
	class IBLBakeScheduler;
	class RenderResourceRegistry;
	class SamplerRegistry;
	class ShaderManager;
	class TaskSystem;
	class TransferManager;
	class GpuProfiler;

	class Renderer
	{
	public:
		class Frame
		{
		public:
			GGLAB_DELETE_COPYABLE_MOVABLE(Frame);
			~Frame() noexcept;
			[[nodiscard]] RHIFrameBeginStatus GetBeginStatus() const noexcept
			{
				return m_BeginStatus;
			}
			[[nodiscard]] bool IsReady() const noexcept
			{
				return m_BeginStatus == RHIFrameBeginStatus::Ready;
			}
			[[nodiscard]] bool IsUnavailable() const noexcept
			{
				return m_BeginStatus == RHIFrameBeginStatus::Unavailable;
			}
			[[nodiscard]] bool IsFatal() const noexcept
			{
				return m_BeginStatus == RHIFrameBeginStatus::Fatal;
			}
			[[nodiscard]] uint64_t GetSerial() const noexcept { return m_FrameSerial; }
			[[nodiscard]] uint32_t GetFrameSlotIndex() const noexcept
			{
				return m_FrameSlotIndex;
			}
			[[nodiscard]] uint32_t GetBackBufferIndex() const noexcept
			{
				return m_BackBufferIndex;
			}

		private:
			enum class State : uint8_t
			{
				Begun,
				Recorded,
				Ended,
			};

			Frame(Renderer* renderer, RHIFrameContext* rhiFrame, uint64_t frameSerial) noexcept :
				m_Renderer(renderer), m_FrameSerial(frameSerial),
				m_FrameSlotIndex(rhiFrame ? rhiFrame->GetFrameSlotIndex() : 0),
				m_BackBufferIndex(rhiFrame ? rhiFrame->GetBackBufferIndex() : 0),
				m_RHIFrame(rhiFrame),
				m_BeginStatus(RHIFrameBeginStatus::Ready)
			{
			}
			explicit Frame(RHIFrameBeginStatus beginStatus) noexcept :
				m_State(State::Ended), m_BeginStatus(beginStatus)
			{
				GGLAB_ASSERT(beginStatus != RHIFrameBeginStatus::Ready);
			}

			friend class Renderer;

			Renderer* m_Renderer = nullptr;
			State m_State = State::Begun;
			uint64_t m_FrameSerial = 0;
			uint32_t m_FrameSlotIndex = std::numeric_limits<uint32_t>::max();
			uint32_t m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
			RHIFrameContext* m_RHIFrame = nullptr;
			RenderGraph* m_RenderGraph = nullptr;
			RHIFencePoint m_UploadFencePoint = {};
			RenderSceneGpuAllocations m_SceneGpuAllocations{};
			RHIFrameBeginStatus m_BeginStatus = RHIFrameBeginStatus::Fatal;
		};

		struct CreateInfo
		{
			const RHIContextFactoryBase* m_RHIContextFactory = nullptr;
			ShaderManager* m_ShaderManager = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			std::filesystem::path m_IblDerivedDataCacheDirectory;
			std::filesystem::path m_ShaderSourceRoot;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			std::optional<std::string> m_AdapterSelector;
			bool m_EnableDebugValidation = false;

			[[nodiscard]] bool HasRequiredRuntimePaths() const noexcept
			{
				return !m_IblDerivedDataCacheDirectory.empty() && !m_ShaderSourceRoot.empty();
			}
		};

	public:
		Renderer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer();

		[[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;
		bool IsInitialized() const noexcept { return m_IsInitialized; }

		[[nodiscard]] Frame BeginFrame() noexcept;
		void Render(
			Frame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept;
		[[nodiscard]] RHIFrameEndResult EndFrame(Frame& frame) noexcept;

		RHIContext* GetRHIContext() const noexcept { return m_RHIContext.get(); }
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
		SamplerRegistry* GetSamplerRegistry() const noexcept { return m_SamplerRegistry.get(); }
		GpuProfiler* GetGpuProfiler() const noexcept
		{
			return m_RHIContext ? m_RHIContext->GetGpuProfiler() : nullptr;
		}
		const std::array<float, 4>& GetBackBufferClearColor() const noexcept
		{
			return m_BackBufferClearColor;
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

		RenderGraph::CreateInfo CreateRenderGraphCreateInfo() const noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		bool IsSuspended() const noexcept;

		RHIFencePoint GetLastSubmittedFencePoint() const noexcept
		{
			return m_LastSubmittedFencePoint;
		}

	private:
		void CreateCommonBindingLayout() noexcept;
		void InitializeGpuBuffers() noexcept;
		[[nodiscard]] RHIFencePoint AbortFrame(Frame& frame) noexcept;
		void EndFrameLifetime(Frame& frame) noexcept;
		void RetireSceneGpuAllocations(
			RenderSceneGpuAllocations* allocations, const RHIFencePoint& fencePoint) noexcept;

	private:
		std::unique_ptr<RHIContext> m_RHIContext;
		std::unique_ptr<AssetUploadScheduler> m_AssetUploadScheduler;
		std::unique_ptr<TransientResourcePool> m_TransientResourcePool;
		std::unique_ptr<PipelineCache> m_PipelineCache;
		std::unique_ptr<EnvironmentLightingSystem> m_EnvironmentLightingSystem;
		std::unique_ptr<IBLBakeScheduler> m_IBLBakeScheduler;
		std::unique_ptr<RenderResourceRegistry> m_RenderResRegistry;
		std::unique_ptr<SamplerRegistry> m_SamplerRegistry;
		RHIBindingLayoutHandle m_CommonBindingLayout{};
		std::array<float, 4> m_BackBufferClearColor{ 0.5f, 0.5f, 0.5f, 1.0f };

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
		bool m_HasActiveFrame = false;
	};
}
