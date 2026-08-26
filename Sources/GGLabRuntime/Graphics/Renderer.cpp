#include "Graphics/Renderer.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/IBLBakeScheduler.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TransferManager.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace gglab
{
	Renderer::Renderer() noexcept = default;

	namespace
	{
		void AddBindingSlot(RHIBindingLayoutDesc& desc, RHIBindingType type,
			RHIShaderStage visibility, uint32_t binding, uint32_t space, uint32_t count,
			uint32_t sizeInBytes, const char* debugName) noexcept
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
			GGLAB_UNUSED(m_Renderer->AbortFrame(*this));
		}
	}

	Renderer::~Renderer()
	{
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer destroyed while a Renderer::Frame is still active.");
	}

	bool Renderer::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (createInfo.m_RHIContextFactory == nullptr)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer initialization requires a host-supplied RHI context factory.");
			return false;
		}
		if (!createInfo.HasRequiredRuntimePaths())
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer initialization requires a non-empty IBL cache root.");
			return false;
		}

		RHIContextDesc contextDesc{};
		contextDesc.m_Width = createInfo.m_Width;
		contextDesc.m_Height = createInfo.m_Height;
		contextDesc.m_AdapterSelector = createInfo.m_AdapterSelector;
		contextDesc.m_EnableDebugValidation = createInfo.m_EnableDebugValidation;
		m_RHIContext = createInfo.m_RHIContextFactory->CreateContext(contextDesc);
		if (!m_RHIContext)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"Renderer failed to create the explicitly selected RHI context.");
			return false;
		}

		auto* device = &m_RHIContext->GetDevice();
		m_AssetUploadScheduler =
			std::make_unique<AssetUploadScheduler>(AssetUploadScheduler::CreateInfo{
				.m_Device = device,
				.m_TransferManager = GetTransferManager(),
				});

		m_TransientResourcePool = std::make_unique<TransientResourcePool>(device);

		PipelineCache::CreateInfo pipelineCacheCreateInfo{
			.m_PipelineSystem = &m_RHIContext->GetPipelineSystem(),
			.m_ShaderManager = createInfo.m_ShaderManager,
		};
		m_PipelineCache = std::make_unique<PipelineCache>(pipelineCacheCreateInfo);

		SamplerRegistry::CreateInfo samplerRegistryCreateInfo{};
		samplerRegistryCreateInfo.m_Device = device;
		m_SamplerRegistry = std::make_unique<SamplerRegistry>(samplerRegistryCreateInfo);

		RenderResourceRegistry::CreateInfo renderResRegistryCreateInfo{};
		renderResRegistryCreateInfo.m_Device = device;
		renderResRegistryCreateInfo.m_TransientResourcePool = m_TransientResourcePool.get();
		renderResRegistryCreateInfo.m_SamplerRegistry = m_SamplerRegistry.get();
		m_RenderResRegistry = std::make_unique<RenderResourceRegistry>(renderResRegistryCreateInfo);
		m_RenderResRegistry->EnsureIblResources();

		EnvironmentLightingSystem::CreateInfo environmentLightingCreateInfo{};
		environmentLightingCreateInfo.m_RenderResourceRegistry = m_RenderResRegistry.get();
		m_EnvironmentLightingSystem =
			std::make_unique<EnvironmentLightingSystem>(environmentLightingCreateInfo);

		IBLBakeScheduler::CreateInfo iblBakeSchedulerCreateInfo{};
		iblBakeSchedulerCreateInfo.m_Device = device;
		iblBakeSchedulerCreateInfo.m_TaskSystem = createInfo.m_TaskSystem;
		iblBakeSchedulerCreateInfo.m_EnvironmentLightingSystem = m_EnvironmentLightingSystem.get();
		iblBakeSchedulerCreateInfo.m_RenderResourceRegistry = m_RenderResRegistry.get();
		iblBakeSchedulerCreateInfo.m_TransferManager = GetTransferManager();
		iblBakeSchedulerCreateInfo.m_GpuProfiler = GetGpuProfiler();
		iblBakeSchedulerCreateInfo.m_ShaderManager = createInfo.m_ShaderManager;
		iblBakeSchedulerCreateInfo.m_DerivedDataCacheDirectory =
			createInfo.m_IblDerivedDataCacheDirectory;
		m_IBLBakeScheduler = std::make_unique<IBLBakeScheduler>(iblBakeSchedulerCreateInfo);

		CreateCommonBindingLayout();
		InitializeGpuBuffers();

		m_IsInitialized = true;
		return true;
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
		m_AssetUploadScheduler->Finalize();

		m_IBLBakeScheduler.reset();
		m_EnvironmentLightingSystem.reset();
		m_RenderResRegistry.reset();
		m_SamplerRegistry.reset();
		m_PipelineCache.reset();
		m_TransientResourcePool.reset();
		m_AssetUploadScheduler.reset();

		m_SceneCB.reset();
		m_ObjectTable.reset();
		m_MaterialTable.reset();
		m_LightTable.reset();
		m_ObjectSB.reset();
		m_MaterialSB.reset();
		m_LightSB.reset();
		m_ViewSB.reset();
		m_TemporalViewHistory.Invalidate();

		m_RHIContext.reset();

		m_IsInitialized = false;
	}

	Renderer::Frame Renderer::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::BeginFrame called before initialization.");
		GGLAB_ASSERT_MSG(
			!m_HasActiveFrame, "Renderer::BeginFrame called without ending the previous frame.");
		GGLAB_ASSERT_NOT_NULL(m_RHIContext.get());
		const RHIFrameBeginResult beginResult = m_RHIContext->BeginFrame();
		if (!beginResult.IsReady())
		{
			return Frame(beginResult.GetStatus());
		}
		RHIFrameContext* rhiFrame = beginResult.GetFrame();
		GGLAB_ASSERT_NOT_NULL(rhiFrame);

		m_SceneCB->Tick();
		m_ViewSB->Tick();

		m_TransientResourcePool->Tick();
		m_AssetUploadScheduler->Tick();
		m_IBLBakeScheduler->Tick(m_LastSubmittedFencePoint);

		m_HasActiveFrame = true;
		const uint64_t frameSerial = m_NextFrameSerial++;
		GGLAB_ASSERT_MSG(frameSerial != 0, "Renderer frame serial overflowed its valid range.");
		return Frame(this, rhiFrame, frameSerial);
	}

	TemporalFrameTransaction& Renderer::BeginTemporalFrame(Frame& frame,
		const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept
	{
		GGLAB_ASSERT_MSG(frame.m_Renderer == this && frame.m_State == Frame::State::Begun,
			"Temporal frame planning requires the active begun Renderer::Frame.");
		frame.m_TemporalTransaction.Begin(m_TemporalViewHistory, plan, width, height);
		return frame.m_TemporalTransaction;
	}

	void Renderer::Render(
		Frame& frame, RenderGraph& rg, const RenderFrameContext& renderContext) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::Render called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::Render received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State == Frame::State::Begun,
			"Renderer::Render requires an active frame begun by Renderer::BeginFrame.");
		GGLAB_ASSERT(renderContext.m_FrameSlotIndex == frame.m_FrameSlotIndex);
		GGLAB_ASSERT(renderContext.m_BackBufferIndex == frame.m_BackBufferIndex);
		GGLAB_ASSERT(renderContext.m_FrameSerial == frame.m_FrameSerial);

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

		// Wait Structured Buffer upload
		if (renderContext.m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(RHIQueueType::Graphics, renderContext.m_UploadFencePoint);
		}

		RGExecuteContext executeContext{ RGBackendExecuteContext{
			.m_GraphicsCommandContext = &frame.m_RHIFrame->GetGraphicsContext(),
			.m_DirectComputeCommandContext = &frame.m_RHIFrame->GetDirectComputeContext(),
			.m_AsyncComputeCommandContext = nullptr,
		} };
		rg.Execute(executeContext);

		frame.m_State = Frame::State::Recorded;
	}

	RHIFrameEndResult Renderer::EndFrame(Frame& frame) noexcept
	{
		GGLAB_ASSERT_MSG(m_IsInitialized, "Renderer::EndFrame called before initialization.");
		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::EndFrame received a frame created by another Renderer.");
		GGLAB_ASSERT_MSG(m_HasActiveFrame && frame.m_State != Frame::State::Ended,
			"Renderer::EndFrame called without a matching Renderer::BeginFrame.");

		if (frame.m_State != Frame::State::Recorded)
		{
			return RHIFrameEndResult::Fatal(AbortFrame(frame));
		}

		GGLAB_ASSERT_NOT_NULL(frame.m_RHIFrame);
		GGLAB_ASSERT_NOT_NULL(frame.m_RenderGraph);

		const RHIFrameEndResult result = m_RHIContext->EndFrame(*frame.m_RHIFrame);
		const RHIFencePoint submittedFence = result.GetSubmittedFence();
		if (result.IsCompleted() && submittedFence.IsValid())
		{
			frame.m_TemporalTransaction.CommitCompleted();
		}
		else
		{
			frame.m_TemporalTransaction.InvalidateAfterFatal();
			m_TemporalViewHistory.Invalidate();
		}
		if (submittedFence.IsValid())
		{
			m_LastSubmittedFencePoint = submittedFence;
			m_IBLBakeScheduler->OnFrameSubmitted(submittedFence);
		}
		else
		{
			m_IBLBakeScheduler->OnFrameAborted();
		}

		const RHIFencePoint retirementFence =
			submittedFence.IsValid() ? submittedFence : m_LastSubmittedFencePoint;
		if (retirementFence.IsValid())
		{
			RetireSceneGpuAllocations(&frame.m_SceneGpuAllocations, retirementFence);
			frame.m_RenderGraph->Retire(retirementFence);
		}

		EndFrameLifetime(frame);
		return result;
	}

	RHIFencePoint Renderer::AbortFrame(Frame& frame) noexcept
	{
		if (frame.m_State == Frame::State::Ended)
		{
			return {};
		}
		if (m_IBLBakeScheduler)
		{
			m_IBLBakeScheduler->OnFrameAborted();
		}
		frame.m_TemporalTransaction.Abort();

		GGLAB_ASSERT_MSG(frame.m_Renderer == this,
			"Renderer::AbortFrame received a frame created by another Renderer.");

		if (m_RHIContext && frame.m_UploadFencePoint.IsValid())
		{
			m_RHIContext->WaitForFence(RHIQueueType::Graphics, frame.m_UploadFencePoint);
		}

		if (m_RHIContext && frame.m_RHIFrame)
		{
			const RHIFencePoint submittedFence = m_RHIContext->AbortFrame(*frame.m_RHIFrame);
			if (submittedFence.IsValid())
			{
				m_LastSubmittedFencePoint = submittedFence;
			}
			const RHIFencePoint retirementFence =
				submittedFence.IsValid() ? submittedFence : m_LastSubmittedFencePoint;
			if (retirementFence.IsValid())
			{
				RetireSceneGpuAllocations(&frame.m_SceneGpuAllocations, retirementFence);

				if (frame.m_RenderGraph)
				{
					frame.m_RenderGraph->Retire(retirementFence);
				}
			}
			EndFrameLifetime(frame);
			return submittedFence;
		}

		EndFrameLifetime(frame);
		return {};
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
		RenderSceneGpuAllocations* allocations, const RHIFencePoint& fencePoint) noexcept
	{
		if (!allocations || allocations->IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(
			fencePoint.IsValid(), "Scene GPU allocations require a valid graphics fence point.");

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

		AddBindingSlot(
			desc, RHIBindingType::ConstantBuffer, RHIShaderStage::All, 0, 0, 1, 0, "SceneCB");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::All, 1, 0, 1,
			MaxDrawConstantDWORDs * sizeof(uint32_t), "DrawConstants");
		AddBindingSlot(desc, RHIBindingType::PushConstants, RHIShaderStage::All, 2, 0, 1,
			MaxPassConstantDWORDs * sizeof(uint32_t), "PassConstants");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 1, 0, 1, 0,
			"ObjectSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 2, 0, 1, 0,
			"MaterialSB");
		AddBindingSlot(
			desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 3, 0, 1, 0, "ViewSB");
		AddBindingSlot(desc, RHIBindingType::ReadOnlyStorageBuffer, RHIShaderStage::All, 4, 0, 1, 0,
			"LightSB");
		AddBindingSlot(desc, RHIBindingType::BindlessResourceTable, RHIShaderStage::All, 0, 0, 0, 0,
			"BindlessResources");
		AddBindingSlot(desc, RHIBindingType::BindlessSamplerTable, RHIShaderStage::All, 0, 0, 0, 0,
			"BindlessSamplers");
		return desc;
	}

	RenderGraph::CreateInfo Renderer::CreateRenderGraphCreateInfo() const noexcept
	{
		RenderGraph::CreateInfo rgCreateInfo{};
		rgCreateInfo.m_Device = GetDevice();
		rgCreateInfo.m_TransientResourcePool = m_TransientResourcePool.get();

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
		m_TemporalViewHistory.Invalidate();
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
			createInfo.m_CapacityInBytes = static_cast<uint32_t>(sizeof(SceneGPU)) *
				m_RHIContext->GetFrameSlotCount() * 4;
			createInfo.m_DebugName = "Renderer.DynamicConstants";
			m_SceneCB = std::make_unique<DynamicConstantBufferAllocator>(createInfo);
		}

		// Persistent scene tables are replicated per frame slot. Each frame only updates
		// the safe physical version selected by its reusable slot.
		{
			PersistentStructuredBuffer<ObjectGPU>::CreateInfo objectSBCreateInfo{};
			objectSBCreateInfo.m_Device = GetDevice();
			objectSBCreateInfo.m_ElementCapacity = MaxObjectCapacity;
			objectSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			objectSBCreateInfo.m_DebugName = "Renderer.PersistentObjects";
			m_ObjectSB =
				std::make_unique<PersistentStructuredBuffer<ObjectGPU>>(objectSBCreateInfo);
			m_ObjectTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, ObjectGPU>>(
				MaxObjectCapacity, m_RHIContext->GetFrameSlotCount());

			PersistentStructuredBuffer<MaterialGPU>::CreateInfo materialSBCreateInfo{};
			materialSBCreateInfo.m_Device = GetDevice();
			materialSBCreateInfo.m_ElementCapacity = MaxMaterialCapacity;
			materialSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			materialSBCreateInfo.m_DebugName = "Renderer.PersistentMaterials";
			m_MaterialSB =
				std::make_unique<PersistentStructuredBuffer<MaterialGPU>>(materialSBCreateInfo);
			m_MaterialTable =
				std::make_unique<PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>>(
					MaxMaterialCapacity, m_RHIContext->GetFrameSlotCount());

			PersistentStructuredBuffer<LightGPU>::CreateInfo lightSBCreateInfo{};
			lightSBCreateInfo.m_Device = GetDevice();
			lightSBCreateInfo.m_ElementCapacity = MaxLightCapacity;
			lightSBCreateInfo.m_BufferCount = m_RHIContext->GetFrameSlotCount();
			lightSBCreateInfo.m_DebugName = "Renderer.PersistentLights";
			m_LightSB = std::make_unique<PersistentStructuredBuffer<LightGPU>>(lightSBCreateInfo);
			m_LightTable = std::make_unique<PersistentStructuredBufferTable<uint64_t, LightGPU>>(
				MaxLightCapacity, m_RHIContext->GetFrameSlotCount());

			// View data remains a small per-frame dynamic upload allocation.
			DynamicStructuredBufferAllocator<ViewGPU>::CreateInfo viewSBCreateInfo{};
			viewSBCreateInfo.m_Device = GetDevice();
			viewSBCreateInfo.m_ElementCapacity =
				MaxViewCapacity * m_RHIContext->GetFrameSlotCount();
			viewSBCreateInfo.m_DebugName = "Renderer.DynamicViews";
			m_ViewSB =
				std::make_unique<DynamicStructuredBufferAllocator<ViewGPU>>(viewSBCreateInfo);
		}
	}
}
