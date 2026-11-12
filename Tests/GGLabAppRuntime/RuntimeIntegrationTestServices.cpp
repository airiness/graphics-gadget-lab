#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppRuntimeConfig.h"
#include "ApplicationContentRegistration.h"
#include "ApplicationInput.h"
#include "Capture/CaptureJobTypes.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameCaptureReadiness.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoBase.h"
#include "Demo/DemoLoadingShell.h"
#include "Demo/DemoTypes.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Platform/Win/Win32ProcessUtils.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingOverrideBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/IBLBakeTypes.h"
#include "GGLabRuntime/Graphics/IBLPreviewTypes.h"
#include "GGLabRuntime/Graphics/LightingMutationResult.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderCompositionAccess.h"
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphSetupPass.h"
#include "GGLabRuntime/Graphics/RenderPipeline/ForwardPlusFrameValidation.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/WorldLighting.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabTestCore/SelfTest.h"
#include "GpuFaultLifecycleSelfTests.h"
#include "Lab/LabCatalog.h"
#include "Lab/LabParameter.h"
#include "Lab/LabRuntime.h"
#include "Lab/LabSessionBase.h"
#include "Lab/LabTypes.h"
#include "LoadingProgress.h"
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterSourceAdapters.h"
#include "Parameters/SessionParameterControl.h"
#include "RHIFrameSubmission.h"
#include "RHIUploadPool.h"
#include "RuntimeIntegrationTestCases.h"
#include "RuntimeIntegrationTestServices.h"
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"
#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace gglab::runtime_test
{
	static_assert(!std::is_copy_constructible_v<HostViewParameterSource> &&
		!std::is_move_constructible_v<HostViewParameterSource>);

	static_assert(!std::is_copy_constructible_v<LabParameterSource> &&
		!std::is_move_constructible_v<LabParameterSource>);

	static_assert(!std::is_copy_constructible_v<LightingProfileParameterSource> &&
		!std::is_move_constructible_v<LightingProfileParameterSource>);

	static_assert(!std::is_copy_constructible_v<ParameterSourceAdapters> &&
		!std::is_move_constructible_v<ParameterSourceAdapters>);

	template<typename Services>
	concept CarriesWorldLightingAuthoring = requires(Services services) { services.m_WorldLighting; };

	static_assert(!CarriesWorldLightingAuthoring<RenderServices>);

	static_assert(std::is_same_v<decltype(std::declval<RenderCompositionAccess&>().GetWorldLightingControl()),
		WorldLightingControlBase*>);

	static_assert(std::is_same_v<decltype(std::declval<GGLabAppRuntime&>().GetParameterControl()),
		const SessionParameterControl*>);

	static_assert(std::is_same_v<decltype(std::declval<GGLabAppRuntime&>().GetFrameSequenceCoordinator()),
		const FrameSequenceCoordinator*>);

	void EnqueueFaultFrameCommits(RGExecuteContext& execute, LifetimeProbe& probe, uint64_t serial)
	{
		if (!probe.m_EnableDeferredCommits)
		{
			return;
		}
		if (probe.m_CommitEnqueueFault != CommitEnqueueFault::None)
		{
			// Deliberate contract violation: retain a value copy, never a dangling stack reference.
			probe.m_RetainedExecuteContext.emplace(execute);
		}
		struct Payload
		{
			LifetimeProbe* m_Probe;
			~Payload() noexcept { ++m_Probe->m_CommitPayloadsDestroyed; }
		};
		for (uint32_t order : { 1u, 2u })
		{
			execute.DeferCommit([&probe, serial, order, payload = std::make_unique<Payload>(&probe)]
				(const RGCommitContext& commit) noexcept
				{
					const uint32_t index = probe.m_CommitsRun++;
					if (index < probe.m_CommitObservations.size())
					{
						probe.m_CommitObservations[index] = { commit.m_FrameSerial, commit.m_Fence, order };
					}
					probe.m_CommitIdentityMatches &= index < probe.m_CommitObservations.size() &&
						commit.m_FrameSerial == serial &&
						probe.m_Presentation->GetLastSubmittedFencePoint() == commit.m_Fence;
					if (order == 1 && probe.m_CommitEnqueueFault == CommitEnqueueFault::DuringSettlement)
					{
						probe.m_RetainedExecuteContext->DeferCommit([&probe](const RGCommitContext&) noexcept
							{
								++probe.m_LateCommitsRun;
							});
					}
				});
		}
	}

	class FaultDevice final : public RHIDevice
	{
		struct Buffer
		{
			RHIBufferDesc m_Desc;
			std::vector<std::byte> m_Bytes;
		};
	public:
		explicit FaultDevice(LifetimeProbe& probe) : m_Probe(probe) {}
		RHIBackendType GetBackendType() const noexcept override { return RHIBackendType::DX12; }
		std::string_view GetAdapterCompatibilityIdentity() const noexcept override { return "FaultLifecycle.SimulatedBackend"; }
		RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override { return {}; }
		RHITextureSupportResult QueryTextureSupport(const RHITextureDesc&) const noexcept override { return { .m_Supported = true }; }
		RHITextureSupportResult QueryTextureViewSupport(const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override { return { .m_Supported = true }; }
		RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&, const RHIResourceDebugIdentityDesc&) noexcept override
		{
			const RHITextureHandle handle{ m_Next++, 1 };
			m_Textures.insert(handle);
			return handle;
		}
		RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override
		{
			const RHIBufferHandle handle{ m_Next++, 1 };
			m_Buffers.emplace(handle, Buffer{ desc,{} });
			m_Probe.m_LiveReadbackBuffers += desc.m_MemoryUsage == RHIMemoryUsage::GpuToCpu;
			return handle;
		}
		RHITextureViewHandle CreateTextureView(RHITextureHandle, const RHITextureViewDesc&) noexcept override { return { m_Next++, 1 }; }
		RHIBufferViewHandle CreateBufferView(RHIBufferHandle, const RHIBufferViewDesc&) noexcept override { return { m_Next++, 1 }; }
		RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override { return { m_Next++, 1 }; }
		void DestroyTexture(RHITextureHandle handle) noexcept override
		{
			m_Probe.Destroy();
			m_Textures.erase(handle);
		}
		void DestroyBuffer(RHIBufferHandle handle) noexcept override
		{
			m_Probe.Destroy();
			const auto found = m_Buffers.find(handle);
			if (found != m_Buffers.end() && found->second.m_Desc.m_MemoryUsage == RHIMemoryUsage::GpuToCpu)
			{
				--m_Probe.m_LiveReadbackBuffers;
			}
			m_Buffers.erase(handle);
		}
		void DestroyTextureView(RHITextureViewHandle) noexcept override { m_Probe.Destroy(); }
		void DestroyBufferView(RHIBufferViewHandle) noexcept override { m_Probe.Destroy(); }
		void DestroySampler(RHISamplerHandle) noexcept override { m_Probe.Destroy(); }
		void SetTextureDebugBinding(RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override {}
		void SetBufferDebugBinding(RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override {}
		std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override { return {}; }
		std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override { return {}; }
		void* MapBuffer(RHIBufferHandle handle, RHIMappedBufferRange) noexcept override
		{
			auto& buffer = m_Buffers.at(handle);
			if (buffer.m_Desc.m_MemoryUsage == RHIMemoryUsage::GpuToCpu)
			{
				++m_Probe.m_ReadbackMaps;
				m_Probe.Record("readback_map");
			}
			buffer.m_Bytes.resize(static_cast<size_t>(buffer.m_Desc.m_SizeInBytes));
			return buffer.m_Bytes.data();
		}
		void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override {}
		bool FlushMappedBufferWrites(RHIBufferHandle, RHIMappedBufferRange) noexcept override { return true; }
		uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 256; }
		RHIUploadLimits GetUploadLimits() const noexcept override { return { 256, 65536, 16, UINT32_MAX }; }
		bool IsUploadStorage(RHIBufferHandle handle) const noexcept override
		{
			const auto found = m_Buffers.find(handle);
			return found != m_Buffers.end() && Test(found->second.m_Desc.m_CreateFlags, RHIBufferCreateFlags::UploadStorage);
		}
		bool IsAlive(RHITextureHandle handle) const noexcept override
		{
			// The simulated swap chain owns its back buffer for the context's lifetime.
			return handle == RHITextureHandle{ 100000, 1 } || m_Textures.contains(handle);
		}
		bool IsAlive(RHIBufferHandle handle) const noexcept override { return m_Buffers.contains(handle); }
		bool IsAlive(RHISamplerHandle handle) const noexcept override { return handle.IsValid(); }
		void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
		void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle handle) const noexcept override { return { .m_HeapType = RHIDescriptorHeapType::CbvSrvUav, .m_Index = handle.Index() }; }
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle handle) const noexcept override { return { .m_HeapType = RHIDescriptorHeapType::CbvSrvUav, .m_Index = handle.Index() }; }
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle handle) const noexcept override { return { .m_HeapType = RHIDescriptorHeapType::Sampler, .m_Index = handle.Index() }; }
		void RetireCompletedWork() noexcept override {}
		void LoseDevice() noexcept { ObserveFenceQueryResult(RHIFenceQueryResult::DeviceLost); }
	private:
		RHIFenceQueryResult QueryFencePointImpl(const RHIFencePoint& point) const noexcept override
		{
			if (point == m_Probe.m_ForwardPlusFence && m_Probe.m_ForwardPlusQuery &&
				!m_Probe.m_SafeToRelease)
			{
				const auto result = *m_Probe.m_ForwardPlusQuery;
				m_Probe.m_Faulted = result != RHIFenceQueryResult::Completed;
				m_Probe.m_SafeToRelease = result == RHIFenceQueryResult::DeviceLost;
				return m_Probe.m_ObserveForwardPlusQuery ? ObserveFenceQueryResult(result) : result;
			}
			if (HasObservedDeviceLoss())
			{
				return RHIFenceQueryResult::DeviceLost;
			}
			const bool pending = point == m_Probe.m_ReadbackFence ||
				(m_Probe.m_HoldCaptureFence && point == m_Probe.m_LastEndFence) ||
				(m_Probe.m_HoldUploads && point.m_Fence == RHIFenceHandle{ 2, 1 });
			return pending ? RHIFenceQueryResult::Pending : RHIFenceQueryResult::Completed;
		}
		LifetimeProbe& m_Probe;
		uint32_t m_Next = 0;
		std::map<RHIBufferHandle, Buffer> m_Buffers;
		std::set<RHITextureHandle> m_Textures;
	};

	template <typename Base>
	class FaultCommandsBase : public Base
	{
	public:
		explicit FaultCommandsBase(LifetimeProbe& probe) : m_Probe(probe) {}
		void SetWriter(RHIUploadWriter* writer) noexcept { this->BindUploadWriter(writer); }
		RHICommandContextHandle GetHandle() const noexcept override { return { 1, 1 }; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
		void TrackTextureUse(RHITextureHandle) noexcept override {}
		void TrackBufferUse(RHIBufferHandle) noexcept override {}
		void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
		void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
		void FlushBarriers() noexcept override {}
		void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override {}
		void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override { ++m_Probe.m_CaptureCopies; }
	protected:
		LifetimeProbe& m_Probe;
	};

	template <typename Base>
	class FaultBoundCommandsBase : public FaultCommandsBase<Base>
	{
	public:
		using FaultCommandsBase<Base>::FaultCommandsBase;
		void SetPipeline(RHIPipelineHandle) noexcept override {}
		void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
		bool SetConstantBuffer(uint32_t, const RHIConstantBufferBinding& binding) noexcept override { return this->ValidateConstantBinding(binding); }
		void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
		void SetInlineConstantWords(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override {}
	protected:
		bool SetReadOnlyBufferBinding(uint32_t, const RHIReadOnlyBufferBinding&) noexcept override { return true; }
	};

	class FaultGraphics final : public FaultBoundCommandsBase<RHIGraphicsCommandContext>
	{
	public:
		using FaultBoundCommandsBase::FaultBoundCommandsBase;
		void BeginRendering(const RHIRenderingInfo&) noexcept override { m_Rendering = true; }
		void EndRendering() noexcept override { m_Rendering = false; }
		bool IsRendering() const noexcept override { return m_Rendering; }
		void ClearColorAttachment(uint32_t, const std::array<float, 4>&) noexcept override {}
		void ClearDepthAttachment(float, std::optional<uint8_t>) noexcept override {}
		void SetViewport(const RHIViewport&) noexcept override {}
		void SetScissorRect(const RHIScissorRect&) noexcept override {}
		void SetPrimitiveTopology(RHIPrimitiveTopology) noexcept override {}
		void SetVertexBuffers(uint32_t, std::span<const RHIVertexBufferBinding>) noexcept override {}
		void SetIndexBuffer(const RHIIndexBufferBinding&) noexcept override {}
		void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) noexcept override {}
		void Draw(uint32_t, uint32_t, uint32_t, uint32_t) noexcept override {}
	private:
		bool m_Rendering = false;
	};

	class FaultCompute final : public FaultBoundCommandsBase<RHIComputeCommandContext>
	{
	public:
		using FaultBoundCommandsBase::FaultBoundCommandsBase;
		void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
		void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override {}
	};

	class FaultTransfer final : public FaultCommandsBase<RHITransferContext>
	{
	public:
		FaultTransfer(LifetimeProbe& probe, FaultDevice& device) : FaultCommandsBase(probe), m_Device(device) {}
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Transfer; }
		void Begin() noexcept override { m_ReadbackBatch = false; }
		RHIFencePoint Submit(bool) noexcept override
		{
			if (m_Probe.m_Faulted)
			{
				++m_Probe.m_LateSubmits;
				m_Probe.Record("late_submit");
			}
			const RHIFencePoint fence{ RHIFenceHandle{ 2, 1 }, ++m_NextFence };
			if (m_ReadbackBatch)
			{
				m_Probe.m_ReadbackFence = fence;
			}
			return fence;
		}
		void Abort() noexcept override {}
		void ReclaimCompleted() noexcept override {}
		bool UploadBuffer(const void*, uint64_t, RHIBufferHandle, uint64_t) noexcept override { return true; }
		bool UploadTexture(const RHITextureUploadData&, RHITextureHandle) noexcept override { return true; }
		RHITextureReadbackRequest ReadbackTexture(RHITextureHandle, const RHITextureDesc& desc) noexcept override
		{
			RHITextureReadbackRequest request;
			request.m_TextureDesc = desc;
			for (uint32_t slice = 0; slice < desc.m_ArraySize; ++slice)
			{
				for (uint32_t mip = 0; mip < desc.m_MipLevels; ++mip)
				{
					const auto footprint = ComputeRHITextureCopyFootprint(desc.m_Format,
						std::max(1u, desc.m_Extent.m_Width >> mip), std::max(1u, desc.m_Extent.m_Height >> mip));
					request.m_Subresources.push_back({ .m_BufferOffset = request.m_BufferSizeInBytes,
						.m_RowPitch = footprint.m_RowPitch, .m_RowSizeInBytes = footprint.m_RowSizeInBytes,
						.m_SlicePitch = footprint.m_SizeInBytes, .m_RowCount = footprint.m_Height,
						.m_Width = footprint.m_Width, .m_Height = footprint.m_Height,
						.m_MipLevel = mip, .m_ArraySlice = slice });
					request.m_BufferSizeInBytes += footprint.m_SizeInBytes;
				}
			}
			request.m_Buffer = RHIBufferOwner(&m_Device, m_Device.CreateBuffer({
				.m_SizeInBytes = request.m_BufferSizeInBytes, .m_Usage = RHIBufferUsage::CopyDest,
				.m_MemoryUsage = RHIMemoryUsage::GpuToCpu }, {}));
			++m_Probe.m_Readbacks;
			m_ReadbackBatch = true;
			return request;
		}
		// Transfer encoders do not expose graphics/compute binding operations.
	private:
		FaultDevice& m_Device;
		uint64_t m_NextFence = 0;
		bool m_ReadbackBatch = false;
	};

	class FaultPipelines final : public RHIPipelineSystem
	{
	public:
		RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc& desc) noexcept override
		{
			m_Layouts.push_back(desc);
			return { static_cast<uint32_t>(m_Layouts.size() - 1), 1 };
		}
		bool GetBindingLayoutDesc(RHIBindingLayoutHandle handle, RHIBindingLayoutDesc& desc) const noexcept override
		{
			desc = m_Layouts.at(handle.Index());
			return true;
		}
		RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return { m_Next++, 1 }; }
		RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return { m_Next++, 1 }; }
		bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
		bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
		uint64_t GetRevision() const noexcept override { return 1; }
		void Clear() noexcept override {}
	private:
		uint32_t m_Next = 0;
		std::vector<RHIBindingLayoutDesc> m_Layouts;
	};

	class FaultSwapChain final : public RHISwapChain
	{
	public:
		bool IsValid() const noexcept override { return true; }
		uint32_t GetBufferCount() const noexcept override { return 1; }
		uint32_t GetBufferWidth() const noexcept override { return 32; }
		uint32_t GetBufferHeight() const noexcept override { return 32; }
		RHIFormat GetFormat() const noexcept override { return RHIFormat::R8G8B8A8Unorm; }
		RHITextureUsage GetBackBufferUsage() const noexcept override { return RHITextureUsage::RenderTarget | RHITextureUsage::CopySource; }
		RHITextureHandle GetBackBufferHandle(uint32_t) const noexcept override { return { 100000, 1 }; }
		RHIResourceState GetBackBufferInitialState(uint32_t) const noexcept override { return {}; }
	};

	class FaultFrame final : public RHIFrameContext
	{
	public:
		explicit FaultFrame(LifetimeProbe& probe) : m_Graphics(probe), m_Compute(probe) {}
		uint32_t GetFrameSlotIndex() const noexcept override { return 0; }
		uint32_t GetBackBufferIndex() const noexcept override { return 0; }
		RHITextureHandle GetBackBuffer() const noexcept override { return { 100000, 1 }; }
		RHIGraphicsCommandContext& GetGraphicsContext() noexcept override { return m_Graphics; }
		RHIComputeCommandContext& GetDirectComputeContext() noexcept override { return m_Compute; }
		RHIComputeCommandContext* GetComputeContext() noexcept override { return nullptr; }
		RHIFrameUploads GetUploads() noexcept override { return m_Writer.GetFrameUploads(); }
		FaultGraphics m_Graphics;
		FaultCompute m_Compute;
		RHIUploadWriter m_Writer;
		std::unique_ptr<RHIUploadBatch> m_Batch;
	};

	class FaultContext final : public RHIContext
	{
	public:
		explicit FaultContext(LifetimeProbe& probe) : m_Probe(probe), m_Device(probe),
			m_Transfer(std::make_unique<FaultTransfer>(probe, m_Device)), m_Uploads(m_Device), m_Frame(probe)
		{
		}
		~FaultContext() override
		{
			++m_Probe.m_ContextDestructions;
			m_Probe.Record("context_destroyed");
		}
		RHIDevice& GetDevice() noexcept override { return m_Device; }
		const RHIDevice& GetDevice() const noexcept override { return m_Device; }
		RHISwapChain& GetSwapChain() noexcept override { return m_SwapChain; }
		const RHISwapChain& GetSwapChain() const noexcept override { return m_SwapChain; }
		TransferManager& GetTransferManager() noexcept override { return m_Transfer; }
		RHIPipelineSystem& GetPipelineSystem() noexcept override { return m_Pipelines; }
		GpuProfiler* GetGpuProfiler() noexcept override { return nullptr; }
		RHIFrameBeginResult BeginFrame() noexcept override
		{
			if (IsPoisoned())
			{
				return RHIFrameBeginResult::Fatal();
			}
			if (m_Probe.m_FrameBeginUnavailable)
			{
				return RHIFrameBeginResult::Unavailable();
			}
			m_Frame.m_Batch = m_Uploads.BeginBatch();
			m_Frame.m_Batch->Attach(m_Frame.m_Writer);
			m_Frame.m_Graphics.SetWriter(&m_Frame.m_Writer);
			m_Frame.m_Compute.SetWriter(&m_Frame.m_Writer);
			++m_Probe.m_FrameBegins;
			return RHIFrameBeginResult::Ready(m_Frame);
		}
		RHIFrameEndResult EndFrame(RHIFrameContext&) noexcept override
		{
			++m_Probe.m_FrameEnds;
			if (m_Probe.m_FrameEndInjection == FrameEndInjection::RecordingFailure)
			{
				return AbortFrame(m_Frame);
			}
			const bool published = m_Frame.m_Batch->Publish();
			GGLAB_ASSERT(published);
			if (!published)
			{
				return AbortFrame(m_Frame);
			}
			++m_Probe.m_FrameSubmits;
			const bool verifiedIdleFailure = m_Probe.m_FrameEndInjection == FrameEndInjection::FatalUnfenced;
			const RHISubmittedStream stream{ RHISubmitEffect::Accepted,
				m_Probe.m_Inject || verifiedIdleFailure ?
					RHIFencePoint{} : RHIFencePoint{ RHIFenceHandle{ 1, 1 }, ++m_NextGraphicsFence } };
			if (m_Probe.m_Inject)
			{
				m_Probe.m_Faulted = true;
				m_Probe.m_SafeToRelease = false;
				m_Probe.Record("accepted_without_signal");
			}
			const RHISubmissionVerification verification{
				.m_WaitForFence = [&](const RHIFencePoint&)
					{
						m_Probe.Record("invalid_fence_wait");
						return RHIGpuWaitResult::Failed;
					},
				.m_WaitIdle = [&]()
					{
						if (verifiedIdleFailure)
						{
							++m_Probe.m_IdleCalls;
							m_Uploads.ReleaseQuarantinedAfterVerifiedIdle();
							return RHIGpuWaitResult::Completed;
						}
						return WaitIdleFor(RHIFrameVerificationTimeoutMilliseconds);
					} };
			const auto resolved = ResolveFrameSubmission(
				std::span(&stream, 1), *m_Frame.m_Batch, verification);
			m_Probe.m_QuarantinedPages = static_cast<uint32_t>(m_Uploads.GetQuarantinedPageCount());
			FinishFrame();
			m_Probe.m_LastEndFence = resolved.m_Fence;
			if (!m_Probe.m_Inject && m_Probe.m_FrameEndInjection != FrameEndInjection::Completed)
			{
				return RHIFrameEndResult::Fatal(resolved.m_Fence);
			}
			if (m_Probe.m_CommitEnqueueFault == CommitEnqueueFault::AfterSubmission &&
				m_Probe.m_RetainedExecuteContext)
			{
				// Renderer checked the list before this call, so the failure surfaces only
				// when settlement reaches the commits.
				m_Probe.m_RetainedExecuteContext->DeferCommit(
					[&late = m_Probe.m_LateCommitsRun](const RGCommitContext&) noexcept
					{
						++late;
					});
			}
			return resolved.m_State == RHIFrameSubmissionState::CompletionUnknown ?
				RHIFrameEndResult::FatalCompletionUnknown() :
				RHIFrameEndResult::Completed(resolved.m_Fence);
		}
		RHIFrameEndResult AbortFrame(RHIFrameContext&) noexcept override
		{
			++m_Probe.m_FrameAborts;
			RHIFrameEndResult result = RHIFrameEndResult::Fatal();
			if (m_Probe.m_AbortInjection == AbortInjection::CompletionUnknown)
			{
				// The abort's retirement signal is accepted without a verifiable fence.
				// The frame's recorded graphics/compute command lists are not executed.
				m_Probe.m_Faulted = true;
				m_Probe.m_SafeToRelease = false;
				const RHISubmittedStream retirement{ RHISubmitEffect::Accepted, {} };
				const auto resolved = ResolveFrameSubmission(std::span(&retirement, 1),
					*m_Frame.m_Batch, { .m_WaitIdle = [&]()
						{
							return WaitIdleFor(RHIFrameVerificationTimeoutMilliseconds);
						} });
				GGLAB_ASSERT(resolved.m_State == RHIFrameSubmissionState::CompletionUnknown);
				result = RHIFrameEndResult::FatalCompletionUnknown();
			}
			else
			{
				m_Frame.m_Batch->Cancel();
				if (m_Probe.m_AbortInjection == AbortInjection::Fenced)
				{
					m_Probe.m_LastAbortFence = { RHIFenceHandle{ 1, 1 },
						++m_NextGraphicsFence };
					result = RHIFrameEndResult::Fatal(m_Probe.m_LastAbortFence);
				}
			}
			FinishFrame();
			return result;
		}
		bool WaitForFence(RHIQueueType, const RHIFencePoint&) noexcept override
		{
			if (m_Probe.m_RejectUploadWait)
			{
				++m_Probe.m_RejectedUploadWaits;
				return false;
			}
			if (m_Probe.m_Faulted)
			{
				++m_Probe.m_FenceWaitsAfterFault;
				m_Probe.Record("invalid_fence_wait");
			}
			return true;
		}
		void Resize(uint32_t, uint32_t) noexcept override {}
		RHIGpuWaitResult WaitIdleFor(uint32_t timeout) noexcept override
		{
			++m_Probe.m_IdleCalls;
			m_Probe.Record(timeout == RHIInfiniteWaitMilliseconds ? "infinite_wait" : "bounded_idle_wait");
			if (m_Probe.m_IdleCalls < 3 || m_Probe.m_Outcome == TeardownOutcome::Timeout ||
				m_Probe.m_Outcome == TeardownOutcome::Failed)
			{
				return m_Probe.m_Outcome == TeardownOutcome::Failed ?
					RHIGpuWaitResult::Failed :
					RHIGpuWaitResult::Timeout;
			}
			m_Probe.m_SafeToRelease = true;
			if (m_Probe.m_Outcome == TeardownOutcome::DeviceLost)
			{
				m_Device.LoseDevice();
				m_Uploads.ReleaseQuarantinedAfterDeviceLoss();
				m_Probe.Record("native_device_loss");
				return RHIGpuWaitResult::DeviceLost;
			}
			m_Uploads.ReleaseQuarantinedAfterVerifiedIdle();
			m_Probe.Record("verified_idle");
			return RHIGpuWaitResult::Completed;
		}
		void RetireCompletedWork() noexcept override { m_Uploads.ReclaimCompleted(); }
		bool IsPoisoned() const noexcept override { return m_Probe.m_Faulted; }
		bool IsDeviceLost() const noexcept override { return m_Device.HasObservedDeviceLoss(); }
		uint32_t GetFrameSlotCount() const noexcept override { return 1; }
		RHIUploadStatistics GetUploadStatistics() const noexcept override { return m_Uploads.GetStatistics(); }
		LifetimeProbe& m_Probe;
	private:
		void FinishFrame()
		{
			m_Frame.m_Graphics.SetWriter(nullptr);
			m_Frame.m_Compute.SetWriter(nullptr);
			m_Frame.m_Batch->Detach(m_Frame.m_Writer);
			m_Frame.m_Batch.reset();
		}
		FaultDevice m_Device;
		TransferManager m_Transfer;
		FaultPipelines m_Pipelines;
		FaultSwapChain m_SwapChain;
		RHIUploadPool m_Uploads;
		FaultFrame m_Frame;
		uint64_t m_NextGraphicsFence = 0;
	};

	class FaultPipeline final : public RenderPipelineBase
	{
	public:
		explicit FaultPipeline(LifetimeProbe& probe) : m_Probe(probe) {}
		std::string_view GetName() const noexcept override { return "FaultLifecycle.Pipeline"; }
		ResolvedTemporalFramePlan ResolveTemporalFramePlan(TemporalFramePlanResolveInfo info) const noexcept override
		{
			m_Probe.m_PlannedContentIdentity = info.m_SessionIdentity;
			auto plan = RenderPipelineBase::ResolveTemporalFramePlan(info);
			if (m_Probe.m_EnableTemporalHistory)
			{
				// A synthetic history producer exercises the real temporal owner without a shader.
				plan.m_Services |= TemporalService::ColorDepthHistory | TemporalService::FrameContinuity;
			}
			return plan;
		}
		RenderFrameValidationResult ValidateRenderFrame(
			const RenderFrameContext& frame, const RenderServices& services) noexcept override
		{
			m_Probe.m_ParameterValidationView = frame.GetDisplayViewRenderSettings();
			if (std::exchange(m_Probe.m_SkipParameterFrame, false))
			{
				return RenderFrameValidationResult::Skip("Parameter retry fixture");
			}
			if (!m_Probe.m_EnableForwardPlusReadback)
			{
				return RenderFrameValidationResult::Ready();
			}
			// The production recipe needs native shader preparation. This harness uses
			// its actual slot owner and classifier to drive the production host boundary.
			const auto completion = m_Readback->ConsumeCompletedSlot(frame.m_FrameSlotIndex);
			m_Probe.m_LastValidation = ClassifyForwardPlusFrame({
				.m_ReadbackValidation = completion,
				.m_PresentationAvailable = true,
				.m_DisplayExtentMatchesPresentation = !m_Probe.m_ResizeValidation,
				.m_RenderSceneReady = true,
				.m_DepthCoverageValid = true,
				});
			if (m_Probe.m_LastValidation.IsReady())
			{
				++m_Probe.m_ValidationPreparations;
				m_Readback->Initialize(*services.m_Presentation->GetDevice(), 1);
				m_Probe.m_ForwardPlusReadback = m_Readback.get();
			}
			return m_Probe.m_LastValidation;
		}
		void BuildRenderGraph(RenderGraph& graph, const RenderFrameContext& frame, const RenderServices& services) noexcept override
		{
			++m_Probe.m_GraphBuilds;
			m_Probe.m_ParameterBuiltView = frame.GetDisplayViewRenderSettings();
			if (m_Probe.m_ParameterRuntime)
			{
				m_Probe.m_ParameterBuiltRadius = frame.GetDisplayViewRenderSettings().m_Lighting.m_GTAO.m_Radius;
				m_Probe.m_ParameterBuiltSerial = frame.m_FrameSerial;
				if (std::exchange(m_Probe.m_RebakeAfterParameterBuild, false))
				{
					m_Probe.m_ParameterEnvironment->RequestRebake();
				}
				if (std::exchange(m_Probe.m_ChangeParameterAfterBuild, false))
				{
					auto* session = m_Probe.m_ParameterRuntime->GetActiveSession();
					GGLAB_UNUSED(session->SetParameter(LabParameterId("radius"), 3.5f));
					session->ApplyParameterChanges(LabChangeImpact::Immediate);
				}
			}
			struct Data
			{
				RGTextureId m_Texture;
				RGTextureViewId m_View;
			};
			graph.AddPass<Data>("FaultLifecycle.Transient",
				[&](RenderGraph::RGBuilder& builder, Data& data)
				{
					builder.SideEffect();
					RHITextureDesc desc{ .m_Format = RHIFormat::R8G8B8A8Unorm,
						.m_Extent = { 32, 32, 1 } };
					data.m_Texture = builder.Write(builder.CreateTexture("FaultLifecycle.Color", desc), RGTextureAccess::RenderTarget);
					data.m_View = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_Texture);
					builder.GetBlackboard().GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName)
						.GetViewTargets(frame.GetDisplayViewId()).m_BackBuffer = data.m_Texture;
				},
				[this, serial = frame.m_FrameSerial](RGExecuteContext& execute, Data& data)
				{
					++m_Probe.m_GraphExecutions;
					const RHIRenderingAttachment attachment{ .m_View = execute.GetViewHandle(data.m_View) };
					auto* commands = execute.GetGraphicsCommandContext();
					commands->BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
					commands->ClearColorAttachment(0, { 0, 0, 0, 1 });
					commands->EndRendering();
					EnqueueFaultFrameCommits(execute, m_Probe, serial);
				});
			if (m_Probe.m_EnableForwardPlusReadback)
			{
				const auto generation = m_Readback->GetCurrentGeneration();
				graph.AddTrivialSideEffectPass("FaultLifecycle.ForwardPlusReadback",
					[this, slot = frame.m_FrameSlotIndex, generation](RGExecuteContext& execute)
					{
						execute.DeferCommit([this, slot, generation](const RGCommitContext& commit) noexcept
							{
								m_Readback->MarkScheduled(slot, commit.m_FrameSerial, commit.m_Fence,
									generation, MakeForwardPlusTileGrid(32, 32), 0, 0);
								m_Probe.m_ForwardPlusFence = commit.m_Fence;
							});
					});
			}
			if (m_Probe.m_EnableLightingProfiles)
			{
				// Synthetic LUT/IBL producers exercise the real scheduler, publication
				// fence and receipt owners. Native suites qualify the shader products.
				const auto stage = services.m_Environment->GetStageForRecording();
				const auto generation = services.m_Environment->GetBakingGeneration();
				auto* atmosphere = services.m_BakeAtmosphere;
				uint32_t dirty = 0;
				if (stage == IBLBakeStage::Environment)
				{
					const auto* inputs = services.m_Environment->GetBakingAtmosphereParameters();
					if (inputs && atmosphere->Begin(*inputs, { 1, 1, 1 }))
					{
						dirty = atmosphere->GetDiagnostics().m_DirtyMask;
					}
				}
				if (services.m_Environment->ShouldInitializeBakeResources() ||
					(stage != IBLBakeStage::Idle && stage != IBLBakeStage::Ready))
				{
					graph.AddTrivialSideEffectPass("FaultLifecycle.LightingProducer",
						[environment = services.m_Environment, atmosphere, dirty, stage, generation](RGExecuteContext&)
						{
							for (uint32_t index = 0; index < 3; ++index)
							{
								if (dirty & (1u << index))
								{
									atmosphere->NotifyExecuted(index);
								}
							}
							environment->NotifyBakeResourcesInitialized(generation);
							environment->NotifyStageExecuted(stage, generation);
						});
				}
			}
			else if (!m_Probe.m_HoldLightingStages)
			{
				m_IBL->AddPass(graph, frame, services);
			}
			if (m_Probe.m_EnablePreviewCommits)
			{
				// The simulated producer uses the production registry and settlement APIs.
				// Native captures separately cover the actual preview shader passes.
				auto* resources = services.m_Resources;
				const auto post = resources->ObservePostProcessPreviewRequest();
				const auto selection = resources->GetPostProcessPreviewSelection();
				const auto ibl = resources->ObserveIBLPreviewRequest(IBLPreviewType::Environment);
				const auto layout = resources->GetIBLEnvironmentPreviewLayout();
				const auto mip = resources->GetIBLEnvironmentPreviewMip();
				graph.AddTrivialSideEffectPass("FaultLifecycle.Preview",
					[resources, post, selection, ibl, layout, mip](RGExecuteContext& execute)
					{
						if (post.m_Requested)
						{
							execute.DeferCommit([resources, post, selection](const RGCommitContext& commit) noexcept
								{
									resources->PublishPostProcessPreview(selection,
										PostProcessPreviewChannel::PostProcessing, commit.m_FrameSerial, post);
								});
						}
						if (ibl.m_Requested)
						{
							execute.DeferCommit([resources, ibl, layout, mip](const RGCommitContext&) noexcept
								{
									resources->PublishIBLPreview(IBLPreviewType::Environment, ibl, layout, mip);
								});
						}
					});
			}
			AddFrameCaptureTapPass(graph, frame, services, FrameCaptureSource::Scene);
			AddFrameCaptureTapPass(graph, frame, services, FrameCaptureSource::Composited);
			if (!m_Probe.m_EnableLightingProfiles && !m_Probe.m_HoldLightingStages)
			{
				m_IBL->AddFinishPass(graph);
			}
			if (m_Probe.m_EnableTemporalHistory)
			{
				struct HistoryData
				{
					TemporalHistoryRenderGraphResources m_History;
				};
				graph.AddPass<HistoryData>("FaultLifecycle.History",
					[&](RenderGraph::RGBuilder& builder, HistoryData& data)
					{
						auto& transaction = *frame.m_TemporalFrameTransaction;
						const bool imported = transaction.ImportHistoryResources(builder, data.m_History);
						GGLAB_ASSERT(imported);
						if (!imported)
						{
							return;
						}
						builder.WriteInPlace(data.m_History.m_NextColor, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextDepth, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextReliability, RGTextureAccess::StorageWrite);
						const bool exported = transaction.ExportHistoryResources(builder, data.m_History);
						GGLAB_ASSERT(exported);
						GGLAB_UNUSED(exported);
					});
			}
			if (m_Probe.m_CancelJobDuringBuild != 0)
			{
				m_Probe.m_CancellationResult = m_Probe.m_AppRuntime->CancelCaptureJob(m_Probe.m_CancelJobDuringBuild);
				m_Probe.m_CancelJobDuringBuild = 0;
			}
			if (m_Probe.m_RequiredPlanDeferrals != 0 &&
				m_Probe.m_CaptureControl->GetUnfinishedRequestCount() >= 2)
			{
				// Inject the real late-admission deferral after image binding. The
				// production coordinator and RAII host must abort the whole plan.
				--m_Probe.m_RequiredPlanDeferrals;
				m_Probe.m_CaptureControl->DeferFrameCaptures(frame.m_FrameSerial);
			}
			if (m_Probe.m_QueueExport)
			{
				m_Probe.m_QueueExport = false;
				m_Probe.m_ExportRequest = m_Probe.m_CaptureControl->RequestResourceExport(
					{ { .m_Resource = "FaultLifecycle.Color" } }, 0, true);
			}
		}
	private:
		LifetimeProbe& m_Probe;
		std::unique_ptr<IBLGraphSetupPass> m_IBL = CreateIBLGraphSetupPass();
		std::unique_ptr<ForwardPlusDebugReadback> m_Readback = std::make_unique<ForwardPlusDebugReadback>();
	};

	class ParameterTestLab final : public LabSessionBase
	{
	public:
		explicit ParameterTestLab(const LabSessionCreateInfo& info) noexcept :
			LabSessionBase({ .m_Id = LabId("gglab.lab.test.parameters"),
				.m_DisplayName = "Parameter frame fixture" }, info,
				std::make_unique<FaultPipeline>(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)),
			m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)
		{
			++m_Probe.m_ParameterSessionCreations;
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("radius"),
				.m_Name = "Radius", .m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
				.m_MinValue = LabValue(0.0f), .m_MaxValue = LabValue(4.0f) }));
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("layout"),
				.m_Name = "Layout", .m_Type = LabParameterType::UInt,
				.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = uint32_t(1) }));
			for (const auto& [name, impact] : std::array{
				std::pair{ "scene-layout", LabChangeImpact::RebuildScene },
				std::pair{ "pipeline-layout", LabChangeImpact::RecreatePipeline } })
			{
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId(name),
					.m_Name = name, .m_Type = LabParameterType::UInt,
					.m_Impact = impact, .m_DefaultValue = uint32_t(1) }));
			}
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("deferred"),
				.m_Name = "Deferred", .m_Type = LabParameterType::Bool, .m_DefaultValue = false,
				.m_EffectClass = ParameterEffectClass::Deferred }));
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("unprojected"),
				.m_Name = "Unprojected", .m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f }));
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("unbound-rebuild"),
				.m_Name = "Unbound Rebuild", .m_Type = LabParameterType::Bool, .m_DefaultValue = false,
				.m_EffectClass = ParameterEffectClass::Rebuild }));
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("rebake"),
				.m_Name = "Misdeclared rebake", .m_Type = LabParameterType::Bool, .m_DefaultValue = false }));
			if (m_Probe.m_ProfileNamedLabBool)
			{
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("environment.profile"),
					.m_Name = "Lab-owned boolean", .m_Type = LabParameterType::Bool, .m_DefaultValue = false }));
			}
			if (m_Probe.m_EnableLightingProfiles)
			{
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("sun-input"),
					.m_Name = "Prepared sun input", .m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
					.m_MinValue = LabValue(0.1f), .m_MaxValue = LabValue(4.0f),
					.m_EffectClass = ParameterEffectClass::Deferred }));
			}
			for (const auto id : { "lighting", "broken-lighting" })
			{
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId(id),
					.m_Name = id, .m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(1),
					.m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(4096)),
					.m_EffectClass = ParameterEffectClass::Deferred }));
			}
		}
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			if (m_Probe.m_FailParameterPreparation)
			{
				return { .m_Status = LoadingStatus::Failed, .m_Detail = "Injected Lab preparation failure." };
			}
			if (m_Probe.m_HoldParameterPreparation)
			{
				return { .m_Status = LoadingStatus::Preparing };
			}
			return LoadingProgress::Ready();
		}
		bool SupportsImmediateParameter(const LabParameterId& id) const noexcept override
		{
			return id == LabParameterId("radius") || id == LabParameterId("rebake") ||
				id == LabParameterId("environment.profile");
		}
		std::optional<LabValue> ReadImmediateParameterFrameValue(
			const LabParameterId& id, const LabParameterFrameContext& frame) const noexcept override
		{
			if (id == LabParameterId("radius"))
			{
				return LabValue(frame.m_ViewSettings.m_Lighting.m_GTAO.m_Radius);
			}
			if (id == LabParameterId("rebake"))
			{
				return LabValue(frame.m_ViewSettings.m_PostProcess.m_Bloom.m_Enabled);
			}
			if (id == LabParameterId("environment.profile"))
			{
				return LabValue(frame.m_ViewSettings.m_Lighting.m_EnableAerialPerspective);
			}
			return std::nullopt;
		}
		bool SupportsWorldLightingParameter(const LabParameterId& id) const noexcept override
		{
			return m_Probe.m_EnableLightingProfiles && id == LabParameterId("sun-input");
		}
		std::unique_ptr<PreparedWorldLightingMutationBase> PrepareWorldLightingParameter(
			const LabParameterId& id, const LabValue& value, WorldLightingControlBase& control,
			uint64_t contentIdentity, std::string& error) noexcept override
		{
			const auto publication = control.GetWorldLightingPublication();
			if (auto callback = std::exchange(m_Probe.m_ParameterPrepareCallback, std::function<void()>{}))
			{
				callback();
			}
			if (!SupportsWorldLightingParameter(id) || !publication.m_Requested)
			{
				error = "LightingPreparationFailed: the fixture lost its authored sun.";
				return {};
			}
			return control.PrepareWorldSunDirection(m_World, contentIdentity,
				Vector3(-0.5f, -std::get<float>(value), 1.0f), error);
		}
		bool SupportsDeferredLightingParameter(const LabParameterId& id) const noexcept override
		{
			return m_LightingOverride &&
				(id == LabParameterId("lighting") || id == LabParameterId("broken-lighting"));
		}
		LightingMutationResult ApplyDeferredLightingParameter(const LabParameterId& id) noexcept override
		{
			if (!SupportsDeferredLightingParameter(id))
			{
				return {};
			}
			++m_Probe.m_LightingParameterApplications;
			if (std::exchange(m_Probe.m_ReturnStaleLightingEvidence, false))
			{
				return m_PreviousLightingEffect;
			}
			if (std::exchange(m_Probe.m_ReturnMissingLightingEvidence, false))
			{
				return {};
			}
			auto result = m_LightingOverride->ApplyPrefilteredSpecularSampleCount(
				GetParameters().Get(id, uint32_t(1)));
			if (std::exchange(m_Probe.m_RequestLightingTwice, false))
			{
				// This owning operation deliberately issues two real requests.
				m_Services.m_EnvironmentLightingControl->RequestRebake();
				result.m_Generation = m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
			}
			if (id == LabParameterId("broken-lighting"))
			{
				// Deliberately misreport owner evidence; capability is still valid.
				result.m_PreviousGeneration = 0;
			}
			if (result.m_Outcome == LightingMutationOutcome::Requested)
			{
				m_PreviousLightingEffect = result;
			}
			return result;
		}
		void OnEnter() noexcept override
		{
			m_LightingOverride = m_Services.m_EnvironmentLightingControl->AcquireOverride();
			GGLAB_ASSERT_NOT_NULL(m_LightingOverride.get());
		}
		void OnExit() noexcept override { m_LightingOverride.reset(); }

		void Update(float) noexcept override { GetCamera().Update(); }

	private:
		void ApplyImmediateParameters() noexcept override
		{
			++m_Probe.m_ParameterApplications;
			if (auto callback = std::exchange(m_Probe.m_ParameterEffectCallback, {}))
			{
				callback();
			}
			if (std::exchange(m_Probe.m_ObserveParameterCommit, false))
			{
				const auto* control = m_Probe.m_ParameterControl;
				const auto* batch = control ? control->GetParameterBatchStatus() : nullptr;
				m_Probe.m_ParameterCommitWasComplete = batch && batch->m_State == ParameterBatchState::Applying &&
					batch->m_Items.size() == 4 && batch->m_Revision == control->GetParameterRevision() &&
					control->GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == 11.0f &&
					control->GetHostParameters().Get(LabParameterId("environment.profile"), int32_t(-1)) == 1 &&
					control->GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 1.0f &&
					GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f &&
					m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration() == m_Probe.m_ExpectedCommitGeneration;
			}
			GetMutableViewRenderProfile().m_Lighting.m_GTAO.m_Radius =
				GetParameters().Get(LabParameterId("radius"), 1.0f);
			const bool rebake = GetParameters().Get(LabParameterId("rebake"), false);
			GetMutableViewRenderProfile().m_PostProcess.m_Bloom.m_Enabled = rebake;
			if (m_Probe.m_ProfileNamedLabBool)
			{
				GetMutableViewRenderProfile().m_Lighting.m_EnableAerialPerspective =
					GetParameters().Get(LabParameterId("environment.profile"), false);
			}
			if (rebake != m_Rebake)
			{
				m_Rebake = rebake;
				m_Services.m_EnvironmentLightingControl->RequestRebake();
			}
		}
		LifetimeProbe& m_Probe;
		bool m_Rebake = false;
		std::unique_ptr<EnvironmentLightingOverrideBase> m_LightingOverride;
		LightingMutationResult m_PreviousLightingEffect{};
	};

	std::unique_ptr<LabSessionBase> CreateParameterTestLab(const LabSessionCreateInfo& info) noexcept
	{
		if (static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe.m_FailParameterCreation)
		{
			return {};
		}
		return std::make_unique<ParameterTestLab>(info);
	}

	class InheritedLightingTestLab final : public LabSessionBase
	{
	public:
		explicit InheritedLightingTestLab(const LabSessionCreateInfo& info) noexcept :
			LabSessionBase({ .m_Id = LabId("gglab.lab.test.inherited-lighting"),
				.m_DisplayName = "Inherited lighting fixture" }, info,
				std::make_unique<FaultPipeline>(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)),
			m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)
		{
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("samples"),
				.m_Name = "Samples", .m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(512),
				.m_InheritOnEnter = true }));
			GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("toggle"),
				.m_Name = "Toggle", .m_Type = LabParameterType::Bool, .m_DefaultValue = false }));
		}
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_Probe.m_HoldLightingPreparation ?
				LoadingProgress{ .m_Status = LoadingStatus::Preparing } : LoadingProgress::Ready();
		}
		void OnEnter() noexcept override
		{
			m_Override = m_Services.m_EnvironmentLightingControl->AcquireOverride();
			GGLAB_ASSERT_NOT_NULL(m_Override.get());
			const uint32_t inherited = m_Override->GetInheritedSettings().m_BakeConfig.m_PrefilteredSpecularSampleCount;
			GGLAB_UNUSED(GetMutableParameters().BindInheritedDefault(LabParameterId("samples"), inherited));
			ApplyImmediateParameters();
		}
		void OnExit() noexcept override { m_Override.reset(); }
		void Update(float) noexcept override {}
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& info) noexcept
		{
			return std::make_unique<InheritedLightingTestLab>(info);
		}
	private:
		bool RefreshInheritedParameters() noexcept override
		{
			return m_Override && !m_Override->OwnsBakeConfiguration() &&
				GetMutableParameters().FollowInheritedValue(LabParameterId("samples"),
					m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings()
						.m_BakeConfig.m_PrefilteredSpecularSampleCount);
		}
		void ApplyImmediateParameters() noexcept override
		{
			if (m_Override && GetParameters().Find(LabParameterId("samples"))->m_Dirty)
			{
				m_Override->Apply({ .m_PrefilteredSpecularSampleCount =
					GetParameters().Get(LabParameterId("samples"), uint32_t(512)) });
			}
		}
		LifetimeProbe& m_Probe;
		std::unique_ptr<EnvironmentLightingOverrideBase> m_Override;
	};

	class RetirementTestLab final : public LabSessionBase
	{
	public:
		explicit RetirementTestLab(const LabSessionCreateInfo& info) noexcept :
			LabSessionBase({ .m_Id = LabId("gglab.lab.test.retirement"),
				.m_DisplayName = "Retirement fixture" }, info,
				std::make_unique<FaultPipeline>(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)),
			m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe)
		{
		}
		~RetirementTestLab() override
		{
			++m_Probe.m_LabSessionDestructions;
		}
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_Probe.m_HoldRetirementPreparation ?
				LoadingProgress{ .m_Status = LoadingStatus::Preparing } :
				LoadingProgress::Ready();
		}
		void Update(float) noexcept override {}
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& info) noexcept
		{
			return std::make_unique<RetirementTestLab>(info);
		}
	private:
		LifetimeProbe& m_Probe;
	};

	class FaultDemo final : public DemoBase
	{
	public:
		explicit FaultDemo(const DemoCreateInfo& info) : m_CameraOwner(info),
			m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe),
			m_Pipeline(m_Probe)
		{
			m_Probe.m_ParameterEnvironment = info.m_Services.m_EnvironmentLightingControl;
			m_Probe.m_ContentServices = info.m_Services;
			if (m_Probe.m_EnableParameterLab)
			{
				m_Lab = std::make_unique<LabRuntime>(LabSessionCreateInfo{
					.m_Services = info.m_Services, .m_WindowWidth = info.m_WindowWidth,
					.m_WindowHeight = info.m_WindowHeight, .m_RunConfig = { .m_WarmupFrames = 0 } });
				GGLAB_UNUSED(m_Lab->RegisterLab({ .m_Id = LabId("gglab.lab.test.parameters"),
					.m_DisplayName = "Parameter frame fixture" }, &CreateParameterTestLab));
				GGLAB_UNUSED(m_Lab->Initialize(LabId("gglab.lab.test.parameters")));
				m_Lab->TickTransitions();
				m_Probe.m_ParameterRuntime = m_Lab.get();
				m_Probe.m_ParameterEnvironment = info.m_Services.m_EnvironmentLightingControl;
			}
		}
		~FaultDemo() override
		{
			if (m_Probe.m_CheckServiceCleanup)
			{
				GGLAB_UNUSED(m_Probe.m_ContentServices.m_RenderServices.m_Environment->GetBakeRequestGeneration());
				m_Probe.m_ContainedCleanupAfterRevocation =
					!m_Probe.m_SavedServiceView.IsValid() &&
					m_Probe.m_ContentServices.m_RenderServices.HasRequiredServices();
			}
			++m_Probe.m_DemoDestructions;
		}
		std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept override
		{
			++m_Probe.m_LightingProfileQueries;
			static const std::array invalid{ WorldLightingProfile{} };
			static const std::array equivalent
			{
				WorldLightingProfile{ .m_Id = "test.white" },
				WorldLightingProfile{ .m_Id = "test.gray",
					.m_Inputs = { .m_Sun = { .m_Chromaticity = Vector3(0.5f, 0.5f, 0.5f) } } },
			};
			if (m_Probe.m_InvalidLightingProfiles)
			{
				return invalid;
			}
			if (m_Probe.m_EquivalentLightingProfiles)
			{
				return equivalent;
			}
			static const std::array profiles
			{
				WorldLightingProfile{ .m_Id = "test.baseline" },
				WorldLightingProfile{ .m_Id = "test.oblique", .m_Inputs = { .m_SunDirection = Vector3(1.0f, -1.0f, 0.0f) },
					.m_RecommendedEV100 = 17.0f },
			};
			return m_Probe.m_EnableLightingProfiles ? std::span<const WorldLightingProfile>(profiles) :
				std::span<const WorldLightingProfile>{};
		}
		std::string_view GetName() const noexcept override { return "FaultLifecycle.Demo"; }
		void OnEnter() noexcept override
		{
			if (m_Probe.m_EnableLightingProfiles)
			{
				auto& world = GetWorld();
				auto& registry = world.GetRegistry();
				const auto sun = registry.create();
				registry.emplace<components::TransformComponent>(sun);
				components::LightComponent light;
				light.m_Type = LightType::Directional;
				light.m_WorldSun.emplace();
				registry.emplace<components::LightComponent>(sun, light);
				GGLAB_UNUSED(ApplyWorldLightingInputsToWorld(world, GetLightingProfiles().front().m_Inputs));
				m_Probe.m_ParameterEnvironment->SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
			}
			if (m_Lab)
			{
				m_Lab->OnEnter();
			}
		}
		void OnExit() noexcept override
		{
			if (const auto id = std::exchange(m_Probe.m_ExpectedReleasedBatch, uint64_t(0)))
			{
				const auto* control = m_Probe.m_ParameterControl;
				const auto* batch = control ? control->GetParameterBatchStatus() : nullptr;
				const std::array values{ LabParameterValue{ LabParameterId("radius"), 2.5f } };
				std::string error;
				m_Probe.m_BindingReleasedBeforeExit = control && batch && batch->m_BatchId == id &&
					batch->IsTerminal() && control->GetContentIdentity() == 0 && !control->GetLabParameters() &&
					control->GetHostParameters().CaptureValues().empty() && m_Lab &&
					!m_Lab->PrepareParameterBatch(id, values, error);
			}
			if (m_Lab)
			{
				m_Lab->OnExit();
			}
		}
		void Update() noexcept override
		{
			if (m_Lab)
			{
				m_Lab->ProcessPendingCommands();
				m_Lab->Update();
			}
			else
			{
				m_CameraOwner.Update();
			}
		}
		LabRuntime* GetParameterLabRuntime() noexcept override { return m_Lab.get(); }
		void AppendCaptureReadiness(FrameCaptureReadiness& readiness) const noexcept override
		{
			DemoBase::AppendCaptureReadiness(readiness);
			if (!m_Probe.m_ExtraPendingGate.empty())
			{
				readiness.Add(m_Probe.m_ExtraPendingGate, FrameCaptureGateState::Pending,
					"Unrelated readiness fixture");
			}
		}
		uint32_t GetTemporalSessionSerial() const noexcept override
		{
			// Match the production Lab host: frame build and Session parameter
			// preparation must resolve lighting under the same content identity.
			return (m_Lab ? m_Lab->GetTemporalSessionSerial() : 0) + m_Probe.m_ContentSerialOffset;
		}
		void OnFrameReady(uint64_t serial) noexcept override
		{
			if (m_Lab)
			{
				GGLAB_UNUSED(serial);
				if (std::exchange(m_Probe.m_UnrelatedLightingChange, false))
				{
					m_Probe.m_ParameterEnvironment->RequestRebake();
				}
			}
		}
		void OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration) noexcept override
		{
			if (m_Probe.m_ParameterBuiltCallback)
			{
				m_Probe.m_ParameterBuiltCallback();
			}
			if (m_Lab || m_Probe.m_EnableLightingProfiles)
			{
				GGLAB_UNUSED(serial);
				GGLAB_UNUSED(lightingGeneration);
				const auto* batch = m_Probe.m_ParameterControl ? m_Probe.m_ParameterControl->GetParameterBatchStatus() : nullptr;
				if (batch && m_Probe.m_SkipLightingPublication && std::ranges::any_of(batch->m_Items,
					[](const auto& item)
					{
						return item.m_DeclaredClass == ParameterEffectClass::Deferred && item.m_Published.has_value();
					}))
				{
					m_Probe.m_SkipLightingPublication = false;
					m_Probe.m_SkipParameterFrame = true;
				}
			}
		}
		World& GetWorld() noexcept override
		{
			return m_Lab ? m_Lab->GetWorld() : m_CameraOwner.GetWorld();
		}
		Camera& GetCamera() noexcept override
		{
			return m_Lab ? m_Lab->GetCamera() : m_CameraOwner.GetCamera();
		}
		CameraController& GetCameraController() noexcept override
		{
			return m_Lab ? m_Lab->GetCameraController() : m_CameraOwner.GetCameraController();
		}
		CameraRig& GetCameraRig() noexcept override
		{
			return m_Lab ? m_Lab->GetCameraRig() : m_CameraOwner.GetCameraRig();
		}
		const ViewRenderProfile& GetViewRenderProfile() const noexcept override
		{
			return m_Lab ? m_Lab->GetViewRenderProfile() : m_CameraOwner.GetViewRenderProfile();
		}
		RenderPipelineBase& GetRenderPipeline() noexcept override
		{
			return m_Lab ? m_Lab->GetRenderPipeline() : m_Pipeline;
		}
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override
		{
			++m_Probe.m_SubmittedCallbacks;
			if (m_Lab)
			{
				m_Lab->OnFrameSubmitted(feedback);
			}
		}
	private:
		DemoLoadingShell m_CameraOwner;
		LifetimeProbe& m_Probe;
		FaultPipeline m_Pipeline;
		std::unique_ptr<LabRuntime> m_Lab;
	};

	std::unique_ptr<DemoBase> CreateFaultDemo(const DemoCreateInfo& info, const LabStartupSelection&, std::span<const LabRegistration>) noexcept { return std::make_unique<FaultDemo>(info); }

	bool WriteBytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return file.good();
	}

	ShaderProgramRegistryArtifactRef WriteShaderFixtures(const std::filesystem::path& root)
	{
		std::vector<ShaderProgramRegistryEntry> entries;
		for (const auto& program : shader_programs::GetRendererInitialShaderProgramDemand())
		{
			ShaderRuntimeArtifact artifact;
			artifact.m_Manifest.m_BinaryFormat = ShaderBinaryFormat::Dxil;
			artifact.m_Manifest.m_Stage = program.m_Stage;
			artifact.m_Manifest.m_EntryPoint = "Main";
			artifact.m_Binary = ShaderBinary(20);
			std::memset(artifact.m_Binary.Data(), 0, 20);
			std::memcpy(artifact.m_Binary.Data(), "DXBC", 4);
			const auto bytes = std::span(static_cast<const std::byte*>(artifact.m_Binary.Data()), 20);
			artifact.m_Manifest.m_BinaryContentDigest.m_Digest = ComputeSha256(bytes);
			artifact.m_Manifest.m_ArtifactId = ComputeShaderArtifactId(artifact.m_Manifest);
			const ShaderArtifactRef ref{ .m_ArtifactId = artifact.m_Manifest.m_ArtifactId };
			const auto paths = ShaderLooseArtifactLocator(root).GetPaths(ref);
			if (!WriteBytes(paths.m_BinaryPath, bytes) ||
				!WriteBytes(paths.m_ManifestPath, SerializeShaderRuntimeArtifactManifest(artifact.m_Manifest)))
			{
				return {};
			}
			entries.push_back({ .m_ProgramRef = program, .m_ArtifactRef = ref });
		}
		const auto registry = BuildShaderProgramRegistryArtifact(entries);
		const ShaderProgramRegistryArtifactRef ref{ .m_RegistryId = registry.m_Artifact.m_RegistryId };
		const auto path = ShaderLooseProgramRegistryArtifactLocator(root).GetPath(ref);
		return registry.IsSuccess() &&
			WriteBytes(path.m_Path, SerializeShaderProgramRegistryArtifact(registry.m_Artifact)) ?
			ref :
			ShaderProgramRegistryArtifactRef{};
	}

	bool InitializeOutcomeRuntime(SelfTestContext& checks, GGLabAppRuntime& runtime,
		FaultFactory& factory, ApplicationInput& input, LifetimeProbe& probe,
		const std::filesystem::path& root, FrameCaptureImageEncoder imageEncoder,
		CaptureJobLimits jobLimits)
	{
		probe.m_EnableTemporalHistory = true;
		const GGLabAppRuntimeCreateInfo info{
			.m_Config = { .m_RhiBackend = AppRuntimeRHIBackend::DX12,
				.m_StartupDemoId = "FaultLifecycle.Demo", .m_InitialExtent = { 32, 32 } },
			.m_Paths = { .m_RuntimeRoot = root, .m_AssetRoot = root / "Assets",
				.m_ShaderArtifactRoot = root / "Shaders", .m_IblDerivedDataRoot = root / "IBL",
				.m_TextureDerivedDataRoot = root / "Texture", .m_EnvironmentAssetRoot = root / "Environment",
				.m_SettingsRoot = root / "Settings", .m_CaptureRoot = root / "Captures" },
			.m_HostServices = { .m_FrameCaptureImageEncoder = std::move(imageEncoder) },
			.m_CaptureJobLimits = jobLimits };
		ApplicationContentRegistration content;
		content.m_Demos.push_back({ .m_Id = "FaultLifecycle.Demo", .m_Factory = &CreateFaultDemo });
		if (probe.m_EnableLightingProfiles)
		{
			content.m_Demos.push_back({ .m_Id = "FaultLifecycle.Replacement", .m_Factory = &CreateFaultDemo });
		}
		const auto registry = WriteShaderFixtures(info.m_Paths.m_ShaderArtifactRoot);
		const bool initialized = runtime.Initialize(info) == AppRuntimeInitializeResult::Succeeded &&
			runtime.InitializeServices({ .m_RHIContextFactory = &factory, .m_Input = &input,
				.m_ContentRegistration = std::move(content), .m_WindowWidth = 32, .m_WindowHeight = 32,
				.m_ShaderArtifactRoot = info.m_Paths.m_ShaderArtifactRoot,
				.m_ActiveShaderRegistry = registry }) == AppRuntimeServiceInitializeResult::Succeeded;
		checks.Check(initialized, "Frame outcome harness initializes production AppRuntime services");
		if (!initialized)
		{
			return false;
		}
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!runtime.GetShaderManager()->GetPreloadStatus().IsReady() &&
			std::chrono::steady_clock::now() < deadline)
		{
			runtime.GetTaskSystem()->PumpCompletions();
			std::this_thread::yield();
		}
		while (probe.m_GraphExecutions == 0 && std::chrono::steady_clock::now() < deadline)
		{
			if (runtime.Tick() != AppRuntimeTickResult::Continue)
			{
				break;
			}
			std::this_thread::yield();
		}
		probe.m_CaptureControl = runtime.GetRenderHost()->GetFrameCaptureControl();
		probe.m_Presentation = probe.m_ContentServices.m_RenderServices.m_Presentation;
		probe.m_EnableDeferredCommits = true;
		checks.Check(probe.m_GraphExecutions > 0 && probe.m_SubmittedCallbacks > 0,
			"The outcome harness reaches its production content and submission hook before injection");
		return probe.m_GraphExecutions > 0;
	}

	std::unique_ptr<RHIContext> FaultFactory::CreateContext(const RHIContextDesc&) const noexcept
	{
		return std::make_unique<FaultContext>(m_Probe);
	}

	std::unique_ptr<LabSessionBase> CreateInheritedLightingTestLab(const LabSessionCreateInfo& info) noexcept
	{
		return InheritedLightingTestLab::Create(info);
	}

	std::unique_ptr<LabSessionBase> CreateRetirementTestLab(const LabSessionCreateInfo& info) noexcept
	{
		return RetirementTestLab::Create(info);
	}

	RuntimeTestDirectory::RuntimeTestDirectory(std::string_view name) :
		m_Path(std::filesystem::temp_directory_path() /
			("gglab-runtime-" + std::string(name) + "-" + std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
	{
		std::filesystem::create_directories(m_Path);
	}

	RuntimeTestDirectory::~RuntimeTestDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(m_Path, error);
	}
}
