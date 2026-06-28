#include "Core/Precompiled.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGTransientResourcePool.h"
#include "Graphics/AssetManager.h"
#include "Graphics/TransferManager.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderPass/RenderPassForwardPBR.h"
#include "Core/Utility/MathUtils.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	Renderer::Renderer() noexcept = default;

	namespace
	{
		void AddBindingSlot(
			RHIBindingLayoutDesc& desc,
			RHIBindingType type,
			RHIShaderStage visibility,
			uint32_t binding,
			uint32_t space,
			uint32_t count,
			uint32_t sizeInBytes,
			const char* debugName) noexcept
		{
			GGLAB_ASSERT(desc.m_SlotCount < RHIBindingLayoutDesc::MaxSlots);
			auto& slot = desc.m_Slots[desc.m_SlotCount++];
			slot.m_Type = type;
			slot.m_Visibility = visibility;
			slot.m_Binding = binding;
			slot.m_Space = space;
			slot.m_Count = count;
			slot.m_SizeInBytes = sizeInBytes;
			slot.m_DebugName = debugName;
		}
	}

	Renderer::Frame::~Frame() noexcept
	{
		if (m_Renderer && m_State != State::Ended)
		{
			m_Renderer->AbortFrame(*this);
		}
	}

	Renderer::~Renderer()
	{
		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer destroyed while a Renderer::Frame is still active.");
	}

	void Renderer::Initialize(const CreateInfo& createInfo) noexcept
	{
		RHIContextDesc contextDesc{};
		contextDesc.m_WindowHandle = createInfo.m_Hwnd;
		contextDesc.m_Width = createInfo.m_Width;
		contextDesc.m_Height = createInfo.m_Height;
		m_RHIContext = CreateRHIContext(contextDesc);
		GGLAB_ASSERT_MSG(m_RHIContext != nullptr, "Renderer failed to create an RHI context.");

		auto* device = &m_RHIContext->GetDevice();

		m_RGTransientResourcePool = std::make_unique<RGTransientResourcePool>(device);

		PipelineCache::CreateInfo pipelineCacheCreateInfo{
			.m_PipelineSystem = &m_RHIContext->GetPipelineSystem(),
			.m_ShaderManager = createInfo.m_ShaderManager,
		};
		m_PipelineCache =
			std::make_unique<PipelineCache>(pipelineCacheCreateInfo);

		SamplerRegistry::CreateInfo samplerRegistryCreateInfo{};
		samplerRegistryCreateInfo.m_Device = device;
		m_SamplerRegistry = std::make_unique<SamplerRegistry>(samplerRegistryCreateInfo);
		m_SamplerRegistry->InitializePresetSamplers();

		TextureRegistry::CreateInfo textureRegistryCreateInfo{};
		textureRegistryCreateInfo.m_Device = device;
		textureRegistryCreateInfo.m_TransferManager = GetTransferManager();
		m_TextureRegistry = std::make_unique<TextureRegistry>(textureRegistryCreateInfo);
		m_TextureRegistry->InitializeReservedTextures();

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_Device = device;
		renderResRegistryCreateInfo.m_TransientResourcePool = m_RGTransientResourcePool.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);

		m_DevelopGuiBackend = std::make_unique<DevelopGuiBackend>();
		DevelopGuiBackend::CreateInfo developGuiBackendCreateInfo{};
		developGuiBackendCreateInfo.m_Hwnd = createInfo.m_Hwnd;
		developGuiBackendCreateInfo.m_RHIContext = m_RHIContext.get();
		m_DevelopGuiBackend->Initialize(developGuiBackendCreateInfo);

		CreateCommonBindingLayout();
		InitializeGpuBuffers();

		m_IsInitialized = true;
	}

	void Renderer::Finalize() noexcept
	{
		if (!m_IsInitialized)
		{
			return;
		}

		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::Finalize called while a Renderer::Frame is still active.");

		m_IsSuspended.store(true, std::memory_order_relaxed);

		m_RHIContext->WaitIdle();

		m_DevelopGuiBackend->Finalize();
		m_DevelopGuiBackend.reset();

		m_RenderResRegistry.reset();
		m_TextureRegistry->Finalize(m_LastSubmittedFencePoint);
		m_TextureRegistry.reset();
		m_SamplerRegistry.reset();
		m_PipelineCache.reset();
		m_RGTransientResourcePool.reset();

		m_SceneCB.reset();
		m_ObjectTable.reset();
		m_MaterialTable.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();
		m_ViewSB.reset();

		m_RHIContext.reset();

		m_IsInitialized = false;
	}

	Renderer::Frame Renderer::BeginFrame(uint32_t backBufferIndex) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::BeginFrame called before initialization.");
		GGLAB_ASSERT_MSG(!m_HasActiveFrame,
			"Renderer::BeginFrame called without ending the previous frame.");
		GGLAB_ASSERT_NOT_NULL(m_RHIContext.get());
		RHIFrameContext& rhiFrame = m_RHIContext->BeginFrame();
		GGLAB_ASSERT(rhiFrame.GetBackBufferIndex() == backBufferIndex);

		m_SceneCB->Tick();
		m_ViewSB->Tick();

		m_RGTransientResourcePool->Tick();

		m_HasActiveFrame = true;
		return Frame(this, &rhiFrame);
	}

	void Renderer::Render(
		Frame& frame,
		RenderGraph& rg,
		const RenderFrameContext& renderContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::Render called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::Render received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State == Frame::State::Begun,
			"Renderer::Render requires an active frame begun by Renderer::BeginFrame.");
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == frame.m_BackBufferIndex);

		frame.m_RenderGraph = &rg;
		frame.m_UploadFencePoint = renderContext.m_UploadFencePoint;
		if (renderContext.m_SceneGpuAllocations)
		{
			frame.m_SceneGpuAllocations = *renderContext.m_SceneGpuAllocations;
			*renderContext.m_SceneGpuAllocations = {};
		}

		// Window suspended do nothing
		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			return;
		}

		if (!GetSwapChain() || !GetSwapChain()->IsValid())
		{
			return;
		}

		auto* swapChain = GetSwapChain();
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == swapChain->GetCurrentBackBufferIndex());

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(RHIQueueType::Graphics, renderContext.m_UploadFencePoint);
		}

		RGExecuteContext executeContext{
			RGBackendExecuteContext{
				.m_GraphicsCommandContext = &frame.m_RHIFrame->GetGraphicsContext(),
				// Compute is acquired lazily through RHIFrameContext when a pass is
				// scheduled for the compute queue.
				.m_ComputeCommandContext = nullptr,
			}
		};
		rg.Execute(executeContext);

		frame.m_State = Frame::State::Recorded;
	}

	void Renderer::EndFrame(Frame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::EndFrame called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::EndFrame received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State != Frame::State::Ended,
			"Renderer::EndFrame called without a matching Renderer::BeginFrame.");

		if (frame.m_State != Frame::State::Recorded)
		{
			AbortFrame(frame);
			return;
		}

		GGLAB_ASSERT_NOT_NULL(frame.m_RHIFrame);
		GGLAB_ASSERT_NOT_NULL(frame.m_RenderGraph);

		m_LastSubmittedFencePoint = m_RHIContext->EndFrame(*frame.m_RHIFrame);

		RetireSceneGpuAllocations(
			&frame.m_SceneGpuAllocations,
			m_LastSubmittedFencePoint);

		frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint);
		EndFrameLifetime(frame);
	}

	void Renderer::AbortFrame(Frame& frame) noexcept
	{
		if (frame.m_State == Frame::State::Ended)
		{
			return;
		}

		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::AbortFrame received a frame created by another Renderer.");

		if (m_RHIContext && frame.m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(RHIQueueType::Graphics, frame.m_UploadFencePoint);
		}

		if (m_RHIContext && frame.m_RHIFrame)
		{
			m_RHIContext->AbortFrame(*frame.m_RHIFrame);
			m_LastSubmittedFencePoint = frame.m_RHIFrame->GetSubmittedFence();
			RetireSceneGpuAllocations(
				&frame.m_SceneGpuAllocations,
				m_LastSubmittedFencePoint);

			if (frame.m_RenderGraph)
			{
				frame.m_RenderGraph->Retire(m_LastSubmittedFencePoint);
			}
		}

		EndFrameLifetime(frame);
	}

	void Renderer::EndFrameLifetime(Frame& frame) noexcept
	{
		frame.m_State = Frame::State::Ended;
		frame.m_RHIFrame = nullptr;
		frame.m_RenderGraph = nullptr;
		frame.m_UploadFencePoint = {};
		frame.m_SceneGpuAllocations = {};
		frame.m_Renderer = nullptr;
		m_HasActiveFrame = false;
	}

	void Renderer::RetireSceneGpuAllocations(
		RenderSceneGpuAllocations* allocations,
		const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocations || allocations->IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(fencePoint.IsValid(),
			"Scene GPU allocations require a valid graphics fence point.");

		if (allocations->m_Views.IsValid())
		{
			m_ViewSB->Retire(&allocations->m_Views, fencePoint);
		}
		if (allocations->m_SceneConstants.IsValid())
		{
			m_SceneCB->Retire(&allocations->m_SceneConstants, fencePoint);
		}
		*allocations = {};
	}

	RHIBindingLayoutDesc Renderer::BuildCommonRHIBindingLayoutDesc() noexcept
	{
		RHIBindingLayoutDesc desc{};
		desc.m_DebugName = "RendererCommonBindingLayout";

		AddBindingSlot(desc, RHIBindingType::ConstantBuffer, RHIShaderStage::AllGraphics, 0, 0, 1, 0, "SceneCB");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::AllGraphics, 1, 0, 1, MaxDrawConstantDWORDs * sizeof(uint32_t), "DrawConstants");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::AllGraphics, 2, 0, 1, MaxPassConstantDWORDs * sizeof(uint32_t), "PassConstants");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 1, 0, 1, 0, "ObjectSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 2, 0, 1, 0, "MaterialSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::AllGraphics, 3, 0, 1, 0, "ViewSB");
		AddBindingSlot(desc, RHIBindingType::BindlessSampledTextureTable, RHIShaderStage::AllGraphics, 0, 0, 0, 0, "BindlessTextures");
		AddBindingSlot(desc, RHIBindingType::BindlessSamplerTable, RHIShaderStage::AllGraphics, 0, 0, 0, 0, "BindlessSamplers");
		return desc;
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_Device = GetDevice();
		rgCreateInfo.m_TransientResourcePool = m_RGTransientResourcePool.get();

		return rgCreateInfo;
	}

	void Renderer::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_IsInitialized)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Renderer is not initialized.");
			return;
		}

		if (m_IsSuspended.load(std::memory_order_relaxed))
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize ignored because renderer is suspended.");
			return;
		}

		if (!GetSwapChain() || !GetSwapChain()->IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. SwapChain is invalid.");
			return;
		}

		if (width == 0 || height == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("Renderer::OnResize. Invalid resize resolution.");
			return;
		}

		m_RHIContext->Resize(width, height);
	}

	void Renderer::OnSuspend() noexcept
	{
		m_IsSuspended.store(true, std::memory_order_relaxed);
	}

	void Renderer::OnResume() noexcept
	{
		m_IsSuspended.store(false, std::memory_order_relaxed);
	}

	bool Renderer::IsSuspended() const noexcept
	{
		return m_IsSuspended.load(std::memory_order_relaxed);
	}

	void Renderer::CreateCommonBindingLayout() noexcept
	{
		const RHIBindingLayoutDesc commonBindingLayout = BuildCommonRHIBindingLayoutDesc();
		m_CommonBindingLayout =
			m_RHIContext->GetPipelineSystem().CreateBindingLayout(commonBindingLayout);
		GGLAB_ASSERT_MSG(m_CommonBindingLayout.IsValid(),
			"Renderer failed to create the common RHI binding layout.");
	}

	void Renderer::InitializeGpuBuffers() noexcept
	{
		// Small, per-frame CPU-visible constant allocations.
		{
			DynamicConstantBufferAllocator::CreateInfo createInfo{};
			createInfo.m_Device = GetDevice();
			createInfo.m_CapacityInBytes =
				static_cast<uint32_t>(sizeof(SceneGPU)) * GetSwapChain()->GetBufferCount() * 4;
			createInfo.m_DebugName = "Renderer.DynamicConstants";
			m_SceneCB = std::make_unique<DynamicConstantBufferAllocator>(createInfo);
		}

		// Persistent scene tables are replicated per backbuffer. Each frame only
		// updates the safe physical version selected by its backbuffer index.
		{
			PersistentStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_Device = GetDevice();
			objectSBCreateInfo.m_ElementCapacity = MaxObjectCapacity;
			objectSBCreateInfo.m_BufferCount = GetSwapChain()->GetBufferCount();
			objectSBCreateInfo.m_DebugName = "Renderer.PersistentObjects";
			m_ObjectSB = std::make_unique<PersistentStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);
			m_ObjectTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, ObjectGPU>>(
				MaxObjectCapacity, GetSwapChain()->GetBufferCount());

			PersistentStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_Device = GetDevice();
			materialSBCreateInfo.m_ElementCapacity = MaxMaterialCapacity;
			materialSBCreateInfo.m_BufferCount = GetSwapChain()->GetBufferCount();
			materialSBCreateInfo.m_DebugName = "Renderer.PersistentMaterials";
			m_MaterialSB = std::make_unique<PersistentStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);
			m_MaterialTable = std::make_unique<PersistentStructuredBufferTable<MaterialID, MaterialGPU>>(
				MaxMaterialCapacity, GetSwapChain()->GetBufferCount());

			// View data remains a small per-frame dynamic upload allocation.
			DynamicStructuredBufferAllocator<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_Device = GetDevice();
			viewSBCreateInfo.m_ElementCapacity =
				MaxViewCapacity * GetSwapChain()->GetBufferCount();
			viewSBCreateInfo.m_DebugName = "Renderer.DynamicViews";
			m_ViewSB = std::make_unique<DynamicStructuredBufferAllocator<ViewGPU>>(viewSBCreateInfo);
		}
	}
}
