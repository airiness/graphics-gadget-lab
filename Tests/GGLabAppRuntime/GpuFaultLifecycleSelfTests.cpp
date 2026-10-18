#include "GpuFaultLifecycleSelfTests.h"
#include "ApplicationInput.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Demo/DemoLoadingShell.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphSetupPass.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Graphics/TransferBatch.h"
#include "RHIFrameSubmission.h"
#include "RHIUploadPool.h"
#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"
#include "ShaderArtifactRuntime/ShaderLooseArtifactIO.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace gglab
{
	namespace
	{
		enum class TeardownOutcome
		{
			Completed,
			DeviceLost,
			Timeout,
			Failed,
		};

		// Only the backend is simulated. Renderer, graph, capture, IBL, upload scheduler,
		// AppRuntime::Tick/Shutdown and their owners are the production implementations.
		struct LifetimeProbe
		{
			TeardownOutcome m_Outcome = TeardownOutcome::Completed;
			std::filesystem::path m_Receipt;
			bool m_Inject = false;
			bool m_Faulted = false;
			bool m_SafeToRelease = true;
			bool m_HoldUploads = false;
			uint32_t m_EarlyDestructions = 0;
			uint32_t m_ReadbackMaps = 0;
			uint32_t m_Readbacks = 0;
			uint32_t m_CaptureCopies = 0;
			uint32_t m_LateSubmits = 0;
			uint32_t m_FenceWaitsAfterFault = 0;
			uint32_t m_FrameSubmits = 0;
			uint32_t m_IdleCalls = 0;
			uint32_t m_ContextDestructions = 0;
			uint32_t m_ReadyExecutions = 0;
			uint32_t m_UploadCallbacks = 0;
			bool m_UploadFailedSafely = false;
			std::atomic<uint32_t> m_ImageEncodes = 0;
			uint32_t m_QuarantinedPages = 0;
			RHIFencePoint m_ReadbackFence{};

			void Record(std::string_view event) const
			{
				if (!m_Receipt.empty())
				{
					std::ofstream stream(m_Receipt, std::ios::app);
					stream << event << '\n';
				}
			}
			void Destroy()
			{
				if (m_Faulted && !m_SafeToRelease)
				{
					++m_EarlyDestructions;
					Record("early_destroy");
				}
			}
		};

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
			bool IsAlive(RHITextureHandle handle) const noexcept override { return m_Textures.contains(handle); }
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
				if (HasObservedDeviceLoss())
				{
					return RHIFenceQueryResult::DeviceLost;
				}
				const bool pending = point == m_Probe.m_ReadbackFence ||
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
			void SetPushConstants(uint32_t, std::span<const uint32_t>, uint32_t) noexcept override {}
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
				m_Frame.m_Batch = m_Uploads.BeginBatch();
				m_Frame.m_Batch->Attach(m_Frame.m_Writer);
				m_Frame.m_Graphics.SetWriter(&m_Frame.m_Writer);
				m_Frame.m_Compute.SetWriter(&m_Frame.m_Writer);
				return RHIFrameBeginResult::Ready(m_Frame);
			}
			RHIFrameEndResult EndFrame(RHIFrameContext&) noexcept override
			{
				const bool published = m_Frame.m_Batch->Publish();
				GGLAB_ASSERT(published);
				if (!published)
				{
					return AbortFrame(m_Frame);
				}
				++m_Probe.m_FrameSubmits;
				const RHISubmittedStream stream{ RHISubmitEffect::Accepted,
					m_Probe.m_Inject ? RHIFencePoint{} : RHIFencePoint{ RHIFenceHandle{ 1, 1 }, m_Probe.m_FrameSubmits } };
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
					.m_WaitIdle = [&]() { return WaitIdleFor(RHIFrameVerificationTimeoutMilliseconds); } };
				const auto resolved = ResolveFrameSubmission(
					std::span(&stream, 1), *m_Frame.m_Batch, verification);
				m_Probe.m_QuarantinedPages = static_cast<uint32_t>(m_Uploads.GetQuarantinedPageCount());
				FinishFrame();
				return resolved.m_State == RHIFrameSubmissionState::CompletionUnknown ?
					RHIFrameEndResult::FatalCompletionUnknown() :
					RHIFrameEndResult::Completed(resolved.m_Fence);
			}
			RHIFrameEndResult AbortFrame(RHIFrameContext&) noexcept override
			{
				m_Frame.m_Batch->Cancel();
				FinishFrame();
				return RHIFrameEndResult::Fatal();
			}
			bool WaitForFence(RHIQueueType, const RHIFencePoint&) noexcept override
			{
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
		};
		class FaultFactory final : public RHIContextFactoryBase
		{
		public:
			explicit FaultFactory(LifetimeProbe& probe) : m_Probe(probe) {}
			std::unique_ptr<RHIContext> CreateContext(const RHIContextDesc&) const noexcept override { return std::make_unique<FaultContext>(m_Probe); }
		private:
			LifetimeProbe& m_Probe;
		};

		class FaultPipeline final : public RenderPipelineBase
		{
		public:
			std::string_view GetName() const noexcept override { return "FaultLifecycle.Pipeline"; }
			void BuildRenderGraph(RenderGraph& graph, const RenderFrameContext& frame, const RenderServices& services) noexcept override
			{
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
					[](RGExecuteContext& execute, Data& data)
					{
						const RHIRenderingAttachment attachment{ .m_View = execute.GetViewHandle(data.m_View) };
						auto* commands = execute.GetGraphicsCommandContext();
						commands->BeginRendering({ .m_ColorAttachments = std::span(&attachment, 1) });
						commands->ClearColorAttachment(0, { 0, 0, 0, 1 });
						commands->EndRendering();
					});
				m_IBL->AddPass(graph, frame, services);
				AddFrameCaptureTapPass(graph, frame, services, FrameCaptureSource::Scene);
				m_IBL->AddFinishPass(graph);
			}
		private:
			std::unique_ptr<IBLGraphSetupPass> m_IBL = CreateIBLGraphSetupPass();
		};
		class FaultDemo final : public DemoBase
		{
		public:
			explicit FaultDemo(const DemoCreateInfo& info) : m_CameraOwner(info) {}
			std::string_view GetName() const noexcept override { return "FaultLifecycle.Demo"; }
			void Update() noexcept override { m_CameraOwner.Update(); }
			World& GetWorld() noexcept override { return m_CameraOwner.GetWorld(); }
			Camera& GetCamera() noexcept override { return m_CameraOwner.GetCamera(); }
			CameraController& GetCameraController() noexcept override { return m_CameraOwner.GetCameraController(); }
			CameraRig& GetCameraRig() noexcept override { return m_CameraOwner.GetCameraRig(); }
			const ViewRenderProfile& GetViewRenderProfile() const noexcept override { return m_CameraOwner.GetViewRenderProfile(); }
			RenderPipelineBase& GetRenderPipeline() noexcept override { return m_Pipeline; }
		private:
			DemoLoadingShell m_CameraOwner;
			FaultPipeline m_Pipeline;
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

		void RunScenario(SelfTestContext& checks, LifetimeProbe& probe, const std::filesystem::path& root)
		{
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			const GGLabAppRuntimeCreateInfo info{
				.m_Config = { .m_RhiBackend = AppRuntimeRHIBackend::DX12,
					.m_StartupDemoId = "FaultLifecycle.Demo", .m_InitialExtent = { 32, 32 } },
				.m_Paths = { .m_RuntimeRoot = root, .m_AssetRoot = root / "Assets",
					.m_ShaderArtifactRoot = root / "Shaders", .m_IblDerivedDataRoot = root / "IBL",
					.m_TextureDerivedDataRoot = root / "Texture", .m_EnvironmentAssetRoot = root / "Environment",
					.m_SettingsRoot = root / "Settings", .m_CaptureRoot = root / "Captures" },
				.m_HostServices = { .m_FrameCaptureImageEncoder = [&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
					{
						++probe.m_ImageEncodes;
						return std::nullopt;
					} } };
			ApplicationContentRegistration content;
			content.m_Demos.push_back({ .m_Id = "FaultLifecycle.Demo", .m_Factory = &CreateFaultDemo });
			const auto registry = WriteShaderFixtures(info.m_Paths.m_ShaderArtifactRoot);
			const bool initialized = runtime.Initialize(info) == AppRuntimeInitializeResult::Succeeded &&
				runtime.InitializeServices({ .m_RHIContextFactory = &factory, .m_Input = &input,
					.m_ContentRegistration = std::move(content), .m_WindowWidth = 32, .m_WindowHeight = 32,
					.m_ShaderArtifactRoot = info.m_Paths.m_ShaderArtifactRoot, .m_ActiveShaderRegistry = registry }) == AppRuntimeServiceInitializeResult::Succeeded;
			checks.Check(initialized, "Fault lifecycle composes production AppRuntime, Renderer and artifact-only services");
			if (!initialized)
			{
				return;
			}
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!runtime.GetShaderManager()->GetPreloadStatus().IsReady() && std::chrono::steady_clock::now() < deadline)
			{
				runtime.GetTaskSystem()->PumpCompletions();
				std::this_thread::yield();
			}
			for (uint32_t frame = 0; frame < 100 && probe.m_Readbacks == 0; ++frame)
			{
				if (runtime.Tick() != AppRuntimeTickResult::Continue)
				{
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			checks.Check(probe.m_Readbacks > 0 && probe.m_ReadbackMaps == 0,
				"Production IBL records a pending readback before the injected frame failure");
			probe.m_HoldUploads = true;
			RHIDevice& device = runtime.GetRenderHost()->GetRHIContext()->GetDevice();
			auto uploadedBuffer = std::make_shared<RHIBufferOwner>(&device, device.CreateBuffer({
				.m_SizeInBytes = 64, .m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest }, {}));
			std::weak_ptr<RHIBufferOwner> pendingOwner = uploadedBuffer;
			auto* scheduler = runtime.GetRenderComposition()->GetAssetUploadScheduler();
			scheduler->EnqueueUploadRecording({ .m_Name = "Pending native upload" },
				[&, owner = std::move(uploadedBuffer)]()
				{
					GGLAB_UNUSED(scheduler->RecordUpload({ .m_Name = "Pending native upload" },
						[owner](TransferBatch& batch)
						{
							const std::array<uint32_t, 16> data{};
							return batch.UploadBuffer(owner->Get(), 0, data.data(), sizeof(data));
						},
						[&, owner](const AssetUploadCompletionInfo& completion)
						{
							++probe.m_UploadCallbacks;
							probe.m_UploadFailedSafely = probe.m_SafeToRelease &&
								completion.m_Status == AssetUploadStatus::Failed;
						}));
				});
			const uint64_t capture = runtime.GetFrameCaptureCoordinator()->Submit({});
			probe.m_Inject = true;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_Faulted && probe.m_QuarantinedPages > 0,
				"An accepted submission without a signal quarantines actual frame uploads and fails the production runtime");
			checks.Check(probe.m_CaptureCopies > 0 && probe.m_ReadbackMaps == 0 && probe.m_EarlyDestructions == 0,
				"The retained production frame keeps graph, capture and IBL resources alive without reading GPU contents");
			TransientResourcePoolSnapshot pool;
			BuildTransientResourcePoolSnapshot(*runtime.GetRenderHost()->CreateRenderGraphCreateInfo().m_TransientResourcePool, pool);
			const bool graphRetained = std::ranges::any_of(pool.m_Textures,
				[](const TransientTextureSlotSnapshot& slot)
				{
					return slot.m_LogicalName == "FaultLifecycle.Color" && slot.m_State == TransientPoolSlotState::Leased;
				});
			checks.Check(graphRetained && probe.m_UploadCallbacks == 0 && !pendingOwner.expired(),
				"Unknown completion leaves the actual graph texture leased and the submitted asset upload owned, without retirement or publication");
			const uint32_t frames = probe.m_FrameSubmits;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
				runtime.GetRenderHost()->BeginFrame().IsFatal() && probe.m_FrameSubmits == frames,
				"Both AppRuntime and the production Renderer refuse another frame after CompletionUnknown");
			struct RetainedPayload
			{
				LifetimeProbe& m_Probe;
				~RetainedPayload() { m_Probe.Destroy(); }
			};
			auto payload = std::make_shared<RetainedPayload>(probe);
			std::weak_ptr<RetainedPayload> held = payload;
			runtime.GetRenderComposition()->GetAssetUploadScheduler()->EnqueueUploadRecording({ .m_Name = "Retained ready upload" },
				[&, owner = std::move(payload)]() { ++probe.m_ReadyExecutions; });
			probe.Record("before_shutdown");
			if (!checks.GetSummary().Succeeded())
			{
				probe.Record("setup_failed");
			}
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_SafeToRelease && held.expired() &&
				probe.m_ReadyExecutions == 0 && probe.m_LateSubmits == 0,
				"Production shutdown cancels retained ready work only after safe quiescence and finalizes the Renderer once");
			checks.Check(probe.m_EarlyDestructions == 0 && probe.m_ReadbackMaps == 0,
				"Shutdown neither destroys resources early nor maps failed frame or IBL readbacks");
			checks.Check(probe.m_UploadCallbacks == 1 && probe.m_UploadFailedSafely && pendingOwner.expired() && probe.m_ImageEncodes == 0,
				"The in-flight asset reports failure once after quiescence, releases its owner and never invokes the capture encoder");
			checks.Check(probe.m_IdleCalls == (probe.m_Outcome == TeardownOutcome::DeviceLost ? 3u : 4u) &&
				probe.m_FenceWaitsAfterFault == 0,
				"Renderer finalization verifies idle again on a live device and never waits after confirmed loss");
			bool iblCachePublished = false;
			if (std::filesystem::exists(info.m_Paths.m_IblDerivedDataRoot))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(info.m_Paths.m_IblDerivedDataRoot))
				{
					iblCachePublished = iblCachePublished || entry.is_regular_file();
				}
			}
			checks.Check(!iblCachePublished,
				"The pending IBL readback never publishes GPU contents to derived data");
			std::vector<FrameCaptureRequestResult> results;
			runtime.ConsumeFrameCaptureResults(results);
			checks.Check(results.size() == 1 && results[0].m_RequestId == capture &&
				results[0].m_Status == FrameCaptureRequestStatus::Failed && results[0].m_ImagePath.empty(),
				"The failed production capture completes exactly once without an image");
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && runtime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Repeated production shutdown preserves the fatal state and does not finalize twice");
		}
	}

	int RunGpuFaultLifecycleChild(std::string_view mode, const std::filesystem::path& receipt) noexcept
	{
		LifetimeProbe probe;
		probe.m_Outcome = mode == "timeout" ? TeardownOutcome::Timeout : TeardownOutcome::Failed;
		probe.m_Receipt = receipt;
		ConsoleSelfTestReporter reporter;
		SelfTestContext checks(reporter);
		RunScenario(checks, probe, receipt.parent_path() / "State");
		probe.Record("unexpected_shutdown_return");
		return 2;
	}

	void RunGpuFaultLifecycleSelfTests(SelfTestContext& context) noexcept
	{
		const auto root = std::filesystem::temp_directory_path() /
			("gglab-fault-lifecycle-" + std::to_string(GetCurrentProcessId()) + "-" +
				std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		for (const auto outcome : { TeardownOutcome::Completed, TeardownOutcome::DeviceLost })
		{
			LifetimeProbe probe;
			probe.m_Outcome = outcome;
			RunScenario(context, probe, root / (outcome == TeardownOutcome::Completed ? "idle" : "loss"));
		}
		for (const std::wstring mode : { L"timeout", L"failed" })
		{
			const auto receipt = root / mode / "receipt.txt";
			std::filesystem::create_directories(receipt.parent_path());
			std::ofstream(receipt, std::ios::trunc).close();
			std::array<wchar_t, 32768> executable{};
			GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
			std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --gpu-fault-child " + mode + L" \"" + receipt.wstring() + L"\"";
			STARTUPINFOW startup{ sizeof(startup) };
			PROCESS_INFORMATION process{};
			const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
				CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE;
			context.Check(started, "Unverified production teardown runs in an isolated child process");
			if (!started)
			{
				continue;
			}
			const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
			if (wait != WAIT_OBJECT_0)
			{
				TerminateProcess(process.hProcess, 3);
				WaitForSingleObject(process.hProcess, 5000);
			}
			DWORD exitCode = 0;
			GetExitCodeProcess(process.hProcess, &exitCode);
			CloseHandle(process.hThread);
			CloseHandle(process.hProcess);
			std::ifstream file(receipt);
			const std::string events{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
			// One failed frame verification precedes Shutdown's three bounded retries.
			// Any destruction, readback, submission or unsignaled wait adds an event.
			constexpr std::string_view expectedEvents =
				"accepted_without_signal\n"
				"bounded_idle_wait\n"
				"before_shutdown\n"
				"bounded_idle_wait\n"
				"bounded_idle_wait\n"
				"bounded_idle_wait\n";
			const std::string outcomeName = mode == L"timeout" ? "Timeout" : "Failed";
			context.Check(wait == WAIT_OBJECT_0 && exitCode == EXIT_FAILURE && events == expectedEvents,
				outcomeName + " idle exhaustion exits within the bound without resource destruction, "
				"readback, new submits or waiting on an unsignaled fence");
		}
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
}
