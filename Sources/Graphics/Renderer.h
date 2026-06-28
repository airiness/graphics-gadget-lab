#pragma once
#include "Graphics/Buffer/DynamicConstantBufferAllocator.h"
#include "Graphics/Buffer/DynamicStructuredBufferAllocator.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/RHI/RHIBindingLayout.h"
#include "Graphics/RHI/RHIContext.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/TransferManager.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGTransientResourcePool.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureRegistry.h"
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderParameters.h"
#include "Graphics/RenderScene.h"
#include "DevTools/DevelopGui/DevelopGuiBackend.h"

#include <array>

namespace gglab
{
	class ShaderManager;

	class Renderer
	{
	public:
		class Frame
		{
		public:
			GGLAB_DELETE_COPYABLE_MOVABLE(Frame);
			~Frame() noexcept;

		private:
			enum class State : uint8_t
			{
				Begun,
				Recorded,
				Ended,
			};

			Frame(Renderer* renderer, RHIFrameContext* rhiFrame) noexcept :
				m_Renderer(renderer),
				m_RHIFrame(rhiFrame),
				m_BackBufferIndex(rhiFrame ? rhiFrame->GetBackBufferIndex() : 0)
			{
			}

			friend class Renderer;

			Renderer* m_Renderer = nullptr;
			State m_State = State::Begun;
			uint32_t m_BackBufferIndex = std::numeric_limits<uint32_t>::max();
			RHIFrameContext* m_RHIFrame = nullptr;
			RenderGraph* m_RenderGraph = nullptr;
			RHIFencePoint m_UploadFencePoint = {};
			RenderSceneGpuAllocations m_SceneGpuAllocations{};
		};

		struct CreateInfo
		{
			ShaderManager* m_ShaderManager = nullptr;
			HWND m_Hwnd = nullptr;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

	public:
		Renderer() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Renderer);
		~Renderer();

		void Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;
		bool IsInitialized() const noexcept { return m_IsInitialized; }

		[[nodiscard]] Frame BeginFrame(uint32_t backBufferIndex) noexcept;
		void Render(Frame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept;
		void EndFrame(Frame& frame) noexcept;

		RHIContext* GetRHIContext() const noexcept { return m_RHIContext.get(); }
		RHIDevice* GetDevice() const noexcept { return m_RHIContext ? &m_RHIContext->GetDevice() : nullptr; }
		RHISwapChain* GetSwapChain() const noexcept { return m_RHIContext ? &m_RHIContext->GetSwapChain() : nullptr; }
		TransferManager* GetTransferManager() const noexcept { return m_RHIContext ? &m_RHIContext->GetTransferManager() : nullptr; }
		PipelineCache* GetPipelineCache() const noexcept { return m_PipelineCache.get(); }
		RenderResourceRegistry* GetRenderResourceRegistry() const noexcept { return m_RenderResRegistry.get(); }
		RGTransientResourcePool* GetTransientResourcePool() const noexcept { return m_RGTransientResourcePool.get(); }
		SamplerRegistry* GetSamplerRegistry() const noexcept { return m_SamplerRegistry.get(); }
		TextureRegistry* GetTextureRegistry() const noexcept { return m_TextureRegistry.get(); }
		DevelopGuiBackend* GetDevelopGuiBackend() const noexcept { return m_DevelopGuiBackend.get(); }
		const std::array<float, 4>& GetBackBufferClearColor() const noexcept { return m_BackBufferClearColor; }

		RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept { return m_CommonBindingLayout; }
		[[nodiscard]] static RHIBindingLayoutDesc BuildCommonRHIBindingLayoutDesc() noexcept;

		const DynamicConstantBufferAllocator* GetSceneConstantBuffer() const noexcept { return m_SceneCB.get(); }
		DynamicConstantBufferAllocator* GetSceneConstantBuffer() noexcept { return m_SceneCB.get(); }
		const PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() const noexcept { return m_ObjectSB.get(); }
		PersistentStructuredBuffer<ObjectGPU>* GetObjectStructuredBuffer() noexcept { return m_ObjectSB.get(); }
		const PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() const noexcept { return m_MaterialSB.get(); }
		PersistentStructuredBuffer<MaterialGPU>* GetMaterialStructuredBuffer() noexcept { return m_MaterialSB.get(); }
		const PersistentStructuredBufferTable<uint64_t, ObjectGPU>* GetObjectStructuredBufferTable() const noexcept { return m_ObjectTable.get(); }
		PersistentStructuredBufferTable<uint64_t, ObjectGPU>* GetObjectStructuredBufferTable() noexcept { return m_ObjectTable.get(); }
		const PersistentStructuredBufferTable<MaterialID, MaterialGPU>* GetMaterialStructuredBufferTable() const noexcept { return m_MaterialTable.get(); }
		PersistentStructuredBufferTable<MaterialID, MaterialGPU>* GetMaterialStructuredBufferTable() noexcept { return m_MaterialTable.get(); }
		const DynamicStructuredBufferAllocator<ViewGPU>* GetViewStructuredBuffer() const noexcept { return m_ViewSB.get(); }
		DynamicStructuredBufferAllocator<ViewGPU>* GetViewStructuredBuffer() noexcept { return m_ViewSB.get(); }

		RenderGraph::CreateInfo CreateRenderGraphCreateInfo() const noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;
		void OnSuspend() noexcept;
		void OnResume() noexcept;
		bool IsSuspended() const noexcept;

		RHIFencePoint GetLastSubmittedFencePoint() const noexcept { return m_LastSubmittedFencePoint; }

	private:
		void CreateCommonBindingLayout() noexcept;
		void InitializeGpuBuffers() noexcept;
		void AbortFrame(Frame& frame) noexcept;
		void EndFrameLifetime(Frame& frame) noexcept;
		void RetireSceneGpuAllocations(
			RenderSceneGpuAllocations* allocations,
			const RHIFencePoint& fencePoint) noexcept;

	private:
		std::unique_ptr<RHIContext> m_RHIContext;
		std::unique_ptr<RGTransientResourcePool> m_RGTransientResourcePool;
		std::unique_ptr<PipelineCache> m_PipelineCache;
		std::unique_ptr<RenderResourceRegistry> m_RenderResRegistry;
		std::unique_ptr<SamplerRegistry> m_SamplerRegistry;
		std::unique_ptr<TextureRegistry> m_TextureRegistry;
		std::unique_ptr<DevelopGuiBackend> m_DevelopGuiBackend;

		RHIBindingLayoutHandle m_CommonBindingLayout{};
		std::array<float, 4> m_BackBufferClearColor{ 0.5f, 0.5f, 0.5f, 1.0f };

		std::unique_ptr<DynamicConstantBufferAllocator> m_SceneCB;

		std::unique_ptr<PersistentStructuredBuffer<ObjectGPU>> m_ObjectSB;
		std::unique_ptr<PersistentStructuredBuffer<MaterialGPU>> m_MaterialSB;
		std::unique_ptr<PersistentStructuredBufferTable<uint64_t, ObjectGPU>> m_ObjectTable;
		std::unique_ptr<PersistentStructuredBufferTable<MaterialID, MaterialGPU>> m_MaterialTable;
		std::unique_ptr<DynamicStructuredBufferAllocator<ViewGPU>> m_ViewSB;

		std::atomic_bool m_IsInitialized = false;
		std::atomic_bool m_IsSuspended = false;

		RHIFencePoint m_LastSubmittedFencePoint = {};
		bool m_HasActiveFrame = false;
	};
}
