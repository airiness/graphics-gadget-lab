#include "GpuFaultLifecycleSelfTests.h"
#include "CapturePublicationTestFileSystem.h"
#include "Capture/CaptureWriter.h"
#include "ApplicationInput.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoLoadingShell.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGDeferredCommit.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphSetupPass.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/ForwardPlusFrameValidation.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlusDebugReadback.h"
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
#include <string_view>
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

		enum class FrameEndInjection
		{
			Completed,
			FatalFenced,
			FatalUnfenced,
			RecordingFailure,
		};

		enum class AbortInjection
		{
			Unfenced,
			Fenced,
			CompletionUnknown,
		};

		enum class CommitEnqueueFault
		{
			None,
			BeforeSubmission,
			DuringSettlement,
		};

		struct CommitObservation
		{
			uint64_t m_Serial = 0;
			RHIFencePoint m_Fence{};
			uint32_t m_Order = 0;
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
			FrameEndInjection m_FrameEndInjection = FrameEndInjection::Completed;
			AbortInjection m_AbortInjection = AbortInjection::Unfenced;
			bool m_RejectUploadWait = false;
			bool m_QueueExport = false;
			uint32_t m_RequiredPlanDeferrals = 0;
			bool m_EnableTemporalHistory = false;
			uint32_t m_FrameBegins = 0;
			uint32_t m_FrameEnds = 0;
			uint32_t m_FrameAborts = 0;
			uint32_t m_GraphBuilds = 0;
			uint32_t m_GraphExecutions = 0;
			uint32_t m_SubmittedCallbacks = 0;
			uint32_t m_RejectedUploadWaits = 0;
			RHIFencePoint m_LastEndFence{};
			RHIFencePoint m_LastAbortFence{};
			FrameCaptureControlBase* m_CaptureControl = nullptr;
			uint64_t m_ExportRequest = 0;
			bool m_EnableDeferredCommits = false;
			bool m_EnablePreviewCommits = false;
			bool m_ChangePreviewAfterBuild = false;
			CommitEnqueueFault m_CommitEnqueueFault = CommitEnqueueFault::None;
			std::optional<RGExecuteContext> m_RetainedExecuteContext;
			std::array<CommitObservation, 64> m_CommitObservations{};
			uint32_t m_CommitsRun = 0;
			uint32_t m_CommitPayloadsDestroyed = 0;
			uint32_t m_LateCommitsRun = 0;
			bool m_CommitIdentityMatches = true;
			RenderPresentationAccess* m_Presentation = nullptr;
			bool m_EnableForwardPlusReadback = false;
			bool m_ResizeValidation = false;
			bool m_ObserveForwardPlusQuery = true;
			std::optional<RHIFenceQueryResult> m_ForwardPlusQuery;
			RHIFencePoint m_ForwardPlusFence;
			ForwardPlusDebugReadback* m_ForwardPlusReadback = nullptr;
			RenderFrameValidationResult m_LastValidation;
			uint32_t m_ValidationPreparations = 0;

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
			explicit FaultPipeline(LifetimeProbe& probe) : m_Probe(probe) {}
			std::string_view GetName() const noexcept override { return "FaultLifecycle.Pipeline"; }
			ResolvedTemporalFramePlan ResolveTemporalFramePlan(TemporalFramePlanResolveInfo info) const noexcept override
			{
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
				m_IBL->AddPass(graph, frame, services);
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
				m_IBL->AddFinishPass(graph);
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
		class FaultDemo final : public DemoBase
		{
		public:
			explicit FaultDemo(const DemoCreateInfo& info) : m_CameraOwner(info),
				m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe),
				m_Pipeline(m_Probe) {}
			std::string_view GetName() const noexcept override { return "FaultLifecycle.Demo"; }
			void Update() noexcept override { m_CameraOwner.Update(); }
			World& GetWorld() noexcept override { return m_CameraOwner.GetWorld(); }
			Camera& GetCamera() noexcept override { return m_CameraOwner.GetCamera(); }
			CameraController& GetCameraController() noexcept override { return m_CameraOwner.GetCameraController(); }
			CameraRig& GetCameraRig() noexcept override { return m_CameraOwner.GetCameraRig(); }
			const ViewRenderProfile& GetViewRenderProfile() const noexcept override { return m_CameraOwner.GetViewRenderProfile(); }
			RenderPipelineBase& GetRenderPipeline() noexcept override { return m_Pipeline; }
			void OnFrameSubmitted(const DemoFrameFeedback&) noexcept override { ++m_Probe.m_SubmittedCallbacks; }
		private:
			DemoLoadingShell m_CameraOwner;
			LifetimeProbe& m_Probe;
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

		bool InitializeOutcomeRuntime(SelfTestContext& checks, GGLabAppRuntime& runtime,
			FaultFactory& factory, ApplicationInput& input, LifetimeProbe& probe,
			const std::filesystem::path& root, FrameCaptureImageEncoder imageEncoder = {})
		{
			probe.m_EnableTemporalHistory = true;
			const GGLabAppRuntimeCreateInfo info{
				.m_Config = { .m_RhiBackend = AppRuntimeRHIBackend::DX12,
					.m_StartupDemoId = "FaultLifecycle.Demo", .m_InitialExtent = { 32, 32 } },
				.m_Paths = { .m_RuntimeRoot = root, .m_AssetRoot = root / "Assets",
					.m_ShaderArtifactRoot = root / "Shaders", .m_IblDerivedDataRoot = root / "IBL",
					.m_TextureDerivedDataRoot = root / "Texture", .m_EnvironmentAssetRoot = root / "Environment",
					.m_SettingsRoot = root / "Settings", .m_CaptureRoot = root / "Captures" },
				.m_HostServices = { .m_FrameCaptureImageEncoder = std::move(imageEncoder) } };
			ApplicationContentRegistration content;
			content.m_Demos.push_back({ .m_Id = "FaultLifecycle.Demo", .m_Factory = &CreateFaultDemo });
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
			probe.m_Presentation = runtime.GetRenderServices().m_Presentation;
			probe.m_EnableDeferredCommits = true;
			checks.Check(probe.m_GraphExecutions > 0 && probe.m_SubmittedCallbacks > 0,
				"The outcome harness reaches its production content and submission hook before injection");
			return probe.m_GraphExecutions > 0;
		}

		enum class HostFrameAction
		{
			Submit,
			DiscardAfterBuild,
			DiscardAfterRecording,
			RejectUploadOrder,
		};

		RHIFrameEndResult RunOutcomeHostFrame(SelfTestContext& checks, GGLabAppRuntime& runtime,
			LifetimeProbe& probe, HostFrameAction action)
		{
			auto& host = *runtime.GetRenderHost();
			auto& demo = *runtime.GetDemoManager()->GetActiveDemo();
			// Keep the graph alive until the RenderFrame's RAII abort has settled it.
			RenderGraph graph(host.CreateRenderGraphCreateInfo());
			auto frame = host.BeginFrame();
			checks.Check(frame.IsReady(), "The production Renderer begins the outcome test frame");
			if (!frame.IsReady())
			{
				return RHIFrameEndResult::Fatal();
			}
			const auto resolution = ResolveNativeViewResolution({ 32, 32 });
			auto settings = ResolveViewRenderSettings(demo.GetViewRenderProfile(), demo.GetCamera());
			settings.m_TemporalAA.m_Enabled = false;
			settings.m_Lighting.m_GTAO.m_Enabled = false;
			const auto compatibility = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics().m_Compatibility;
			const ResolvedTemporalFramePlan plan{ .m_DisplayViewId = RenderViewID::Main,
				.m_Services = TemporalService::ColorDepthHistory | TemporalService::FrameContinuity,
				.m_ResetIdentity = compatibility.m_ResetIdentity,
				.m_SessionIdentity = compatibility.m_SessionIdentity };
			auto& temporal = host.BeginTemporalFrame(
				frame, plan, resolution, settings.m_Exposure.m_PreExposure, std::nullopt);
			ShadowVisualizationSettings shadows;
			auto built = host.BuildFrame({ .m_World = demo.GetWorld(), .m_CameraRig = demo.GetCameraRig(),
				.m_ViewRenderProfile = demo.GetViewRenderProfile(), .m_ShadowVisualizationSettings = shadows,
				.m_DisplayViewSettings = settings, .m_TemporalFramePlan = plan,
				.m_TemporalFrameTransaction = temporal, .m_ViewResolution = resolution,
				.m_FrameSlotIndex = frame.GetFrameSlotIndex(), .m_BackBufferIndex = frame.GetBackBufferIndex(),
				.m_FrameSerial = frame.GetSerial() });
			auto context = built.MakeRenderFrameContext();
			demo.GetRenderPipeline().BuildRenderGraph(graph, context, runtime.GetRenderServices());
			const bool compiled = graph.Compile();
			checks.Check(compiled, "The outcome test compiles the production content graph");
			if (!compiled || action == HostFrameAction::DiscardAfterBuild)
			{
				return RHIFrameEndResult::Fatal();
			}
			if (probe.m_ChangePreviewAfterBuild)
			{
				host.GetIBLPreviewControl()->SetIBLEnvironmentPreviewLayout(IBLPreviewLayout::Grid2x3);
				host.GetIBLPreviewControl()->SetIBLEnvironmentPreviewMip(1);
				host.GetIBLPreviewControl()->RequestIBLPreview(IBLPreviewType::Environment);
				host.GetPostProcessPreviewControl()->SetPostProcessPreviewExposureEV(2.0f);
				host.GetPostProcessPreviewControl()->RequestPostProcessPreview();
			}
			probe.m_RejectUploadWait = action == HostFrameAction::RejectUploadOrder;
			host.Render(frame, graph, context);
			if (action == HostFrameAction::DiscardAfterRecording)
			{
				return RHIFrameEndResult::Fatal();
			}
			if (probe.m_CommitEnqueueFault == CommitEnqueueFault::BeforeSubmission)
			{
				probe.m_RetainedExecuteContext->DeferCommit([&probe](const RGCommitContext&) noexcept
					{
						++probe.m_LateCommitsRun;
					});
			}
			return host.EndFrame(frame);
		}

		bool HasOutcomeTextureState(GGLabAppRuntime& runtime, TransientPoolSlotState state,
			const RHIFencePoint& fence)
		{
			TransientResourcePoolSnapshot snapshot;
			BuildTransientResourcePoolSnapshot(
				*runtime.GetRenderHost()->CreateRenderGraphCreateInfo().m_TransientResourcePool, snapshot);
			return std::ranges::any_of(snapshot.m_Textures, [&](const TransientTextureSlotSnapshot& slot)
				{
					return slot.m_LogicalName == "FaultLifecycle.Color" && slot.m_State == state &&
						slot.m_RetirementFence == fence;
				});
		}

		void RequestOutcomePreviews(GGLabAppRuntime& runtime, LifetimeProbe& probe)
		{
			probe.m_EnablePreviewCommits = true;
			runtime.GetRenderHost()->GetShadowPreviewControl()->RequestShadowPreview();
			auto* resources = runtime.GetRenderServices().m_Resources;
			resources->EnsurePostProcessPreviewResources(32, 32);
			resources->RequestPostProcessPreview();
			resources->MarkIBLPreviewDirty(IBLPreviewType::Environment);
			resources->RequestIBLPreview(IBLPreviewType::Environment);
		}

		void CheckOutcomePreviews(SelfTestContext& checks, GGLabAppRuntime& runtime, bool submitted)
		{
			auto* resources = runtime.GetRenderServices().m_Resources;
			checks.Check(resources->IsPostProcessPreviewRequested() != submitted &&
				resources->IsDirty(RenderTextureIndex::Preview_PostProcess) != submitted &&
				resources->HasPublishedPostProcessPreview() == submitted &&
				resources->ObserveShadowPreviewRequest().m_Requested &&
				resources->IsIBLPreviewRequested(IBLPreviewType::Environment) != submitted &&
				(submitted || resources->IsIBLPreviewDirty(IBLPreviewType::Environment)),
				"Production Renderer commits previews only on Submitted, preserves demand and dirty state otherwise, and keeps shadow demand without a producer");
		}

		void RunEndFrameOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
			FrameEndInjection injection, AbortInjection abort)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			RequestOutcomePreviews(runtime, probe);
			const auto previousFence = runtime.GetRenderServices().m_Presentation->GetLastSubmittedFencePoint();
			const uint32_t callbacks = probe.m_SubmittedCallbacks;
			const uint32_t ends = probe.m_FrameEnds;
			const uint32_t aborts = probe.m_FrameAborts;
			const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
			probe.m_FrameEndInjection = injection;
			probe.m_AbortInjection = abort;
			const bool submitted = injection == FrameEndInjection::Completed;
			checks.Check(runtime.Tick() == (submitted ? AppRuntimeTickResult::Continue : AppRuntimeTickResult::Fatal) &&
				probe.m_FrameEnds == ends + 1 &&
				probe.m_FrameAborts == aborts + (injection == FrameEndInjection::RecordingFailure ? 1u : 0u) &&
				probe.m_SubmittedCallbacks == callbacks + (submitted ? 1u : 0u),
				"The production host accepts only Completed and runs no submission hooks after any EndFrame Fatal");
			const RHIFencePoint frameFence = injection == FrameEndInjection::RecordingFailure ?
				probe.m_LastAbortFence : probe.m_LastEndFence;
			const auto expectedFence = frameFence.IsValid() ? frameFence : previousFence;
			checks.Check(runtime.GetRenderServices().m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
				HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence),
				"Submitted and Failed retire the recorded graph on the returned fence or the previous retention fence");
			std::vector<FrameCaptureResult> results;
			probe.m_CaptureControl->ConsumeResults(results);
			checks.Check(capture != 0 && results.size() == 1 && results[0].m_RequestId == capture &&
				results[0].m_Status == (submitted ? FrameCaptureStatus::Completed : FrameCaptureStatus::Failed) &&
				bool(results[0].m_Image) == submitted,
				"EndFrame Fatal fails the capture once, including fenceless and RHI-internal abort failures");
			const auto temporal = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics();
			checks.Check(submitted ?
				temporal.m_HistoryValid && temporal.m_LastCommitted.m_GraphicsFence == frameFence :
				!temporal.m_HasActiveHistory &&
					temporal.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission,
				"Submitted commits temporal history on its fence; every EndFrame failure invalidates it");
			checks.Check(probe.m_CommitsRun == (submitted ? 2u : 0u) &&
				probe.m_CommitPayloadsDestroyed == 2 && probe.m_CommitIdentityMatches &&
				(!submitted || (probe.m_CommitObservations[0].m_Order == 1 &&
					probe.m_CommitObservations[1].m_Order == 2 &&
					probe.m_CommitObservations[1].m_Fence == frameFence)),
				"Production settlement commits once in order with its fence only on Submitted and drops Failed captures");
			CheckOutcomePreviews(checks, runtime, submitted);
			if (!submitted)
			{
				const uint32_t begins = probe.m_FrameBegins;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins,
					"The failed production host begins no follow-up frame");
			}
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
				"Known-completion outcome teardown finalizes the backend once without early release");
			checks.Check(probe.m_CommitsRun == (submitted ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2,
				"No ended frame's CPU commit survives into teardown");
		}

		void RunDiscardOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
			HostFrameAction action, AbortInjection abort)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			if (action == HostFrameAction::RejectUploadOrder)
			{
				// Force a real scene-table transfer so Renderer::Render must order after it.
				auto& registry = runtime.GetDemoManager()->GetActiveDemo()->GetWorld().GetRegistry();
				const auto light = registry.create();
				registry.emplace<components::TransformComponent>(light);
				registry.emplace<components::LightComponent>(light);
			}
			RequestOutcomePreviews(runtime, probe);
			const auto previousFence = runtime.GetRenderServices().m_Presentation->GetLastSubmittedFencePoint();
			const uint32_t ends = probe.m_FrameEnds;
			const uint32_t aborts = probe.m_FrameAborts;
			const uint32_t executions = probe.m_GraphExecutions;
			const auto previousTemporal = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics();
			const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
			probe.m_AbortInjection = abort;
			GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, action));
			probe.m_RejectUploadWait = false;
			const bool recorded = action == HostFrameAction::DiscardAfterRecording;
			checks.Check(probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
				probe.m_GraphExecutions == executions + (recorded ? 1u : 0u) &&
				(action != HostFrameAction::RejectUploadOrder || probe.m_RejectedUploadWaits >= 1),
				"Build deferral, upload-order failure and recorded RAII abort each settle through RHI AbortFrame once");
			const bool unknown = abort == AbortInjection::CompletionUnknown;
			const auto expectedFence = probe.m_LastAbortFence.IsValid() ? probe.m_LastAbortFence : previousFence;
			checks.Check(runtime.GetRenderServices().m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
				(unknown ? !recorded || HasOutcomeTextureState(runtime, TransientPoolSlotState::Leased, {}) :
					!recorded || HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence)),
				"Renderer abort preserves the previous fence, adopts a retirement fence, or retains unknown work");
			std::vector<FrameCaptureResult> results;
			probe.m_CaptureControl->ConsumeResults(results);
			checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == (recorded ? 2u : 0u),
				"Discarded and unknown-completion frames destroy recorded CPU commits without running them");
			CheckOutcomePreviews(checks, runtime, false);
			if (unknown)
			{
				checks.Check(results.size() == 1 && results[0].m_RequestId == capture &&
					results[0].m_Status == FrameCaptureStatus::Failed && !results[0].m_Image &&
					probe.m_ReadbackMaps == 0 && probe.m_EarlyDestructions == 0 &&
					runtime.GetRenderHost()->BeginFrame().IsFatal(),
					"AbortFrame CompletionUnknown fails capture unread and poisons the real Renderer");
			}
			else
			{
				const auto temporal = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics();
				checks.Check(results.empty() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 1 &&
					temporal.m_ResetCount == previousTemporal.m_ResetCount && temporal.m_HistoryValid &&
					temporal.m_ReadIndex == previousTemporal.m_ReadIndex &&
					temporal.m_LastCommitted.m_GraphicsFence == previousTemporal.m_LastCommitted.m_GraphicsFence,
					"Discarded requeues flexible captures and preserves the previously committed temporal history");
				probe.m_AbortInjection = AbortInjection::Unfenced;
				const auto next = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
				CheckOutcomePreviews(checks, runtime, true);
				probe.m_CaptureControl->ConsumeResults(results);
				checks.Check(next.IsCompleted() && results.size() == 1 && results[0].m_RequestId == capture &&
					results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Image,
					"The next submitted frame fulfills the original requeued request exactly once");
				checks.Check(probe.m_CommitsRun == 2 && probe.m_CommitIdentityMatches &&
					probe.m_CommitPayloadsDestroyed == (recorded ? 4u : 2u) &&
					probe.m_CommitObservations[0].m_Order == 1 && probe.m_CommitObservations[1].m_Order == 2 &&
					probe.m_CommitObservations[1].m_Fence == next.GetSubmittedFence(),
					"Only the follow-up frame's commits run; discarded callbacks do not leak into its submission");
			}
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_SafeToRelease &&
				probe.m_EarlyDestructions == 0 && probe.m_LateSubmits == 0,
				"Abort outcome teardown verifies quiescence before releasing retained GPU owners");
		}

		void RunExportDeferralOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			// Retain completed CPU payloads to fill the real capture service's shared budget.
			std::vector<FrameCaptureResult> held;
			for (uint32_t index = 0; index < 8; ++index)
			{
				GGLAB_UNUSED(probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene));
				GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
				probe.m_CaptureControl->ConsumeResults(held);
			}
			checks.Check(held.size() == 8 && probe.m_CaptureControl->GetResourceExportFrameAvailability() ==
				FrameCaptureAvailability::Busy, "Eight held production payloads create genuine export backpressure");
			const uint32_t builds = probe.m_GraphBuilds;
			const uint32_t executions = probe.m_GraphExecutions;
			const uint32_t ends = probe.m_FrameEnds;
			const uint32_t aborts = probe.m_FrameAborts;
			const uint32_t callbacks = probe.m_SubmittedCallbacks;
			const uint32_t commits = probe.m_CommitsRun;
			RequestOutcomePreviews(runtime, probe);
			probe.m_QueueExport = true;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_ExportRequest != 0 &&
				probe.m_GraphBuilds == builds + 1 && probe.m_GraphExecutions == executions &&
				probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
				probe.m_SubmittedCallbacks == callbacks,
				"Production AppRuntime export deferral aborts after graph build without recording or submitting");
			checks.Check(probe.m_CommitsRun == commits && probe.m_CommitPayloadsDestroyed == commits,
				"The export-deferral abort carries no CPU commits from graph setup");
			CheckOutcomePreviews(checks, runtime, false);
			held.clear();
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_FrameEnds == ends + 1 &&
				probe.m_SubmittedCallbacks == callbacks + 1,
				"Production AppRuntime continues with one successful frame after export pressure clears");
			CheckOutcomePreviews(checks, runtime, true);
			std::vector<FrameCaptureResult> results;
			probe.m_CaptureControl->ConsumeResults(results);
			checks.Check(results.size() == 1 && results[0].m_RequestId == probe.m_ExportRequest &&
				results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Resources,
				"The deferred resource request is fulfilled by the follow-up frame exactly once");
			checks.Check(probe.m_CommitsRun == commits + 2 && probe.m_CommitPayloadsDestroyed == commits + 2 &&
				probe.m_CommitIdentityMatches,
				"The post-deferral submission commits only its own state once");
			runtime.Shutdown();
		}

		void RunRequiredPlanOutcomeScenario(SelfTestContext& checks, const std::filesystem::path& root,
			uint32_t referenceSamples, bool missingResource)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			// Encoding is a stand-in here; native backend captures verify real PNGs.
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
				[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					++probe.m_ImageEncodes;
					return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
				}))
			{
				return;
			}
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_Readiness.IsReady() &&
				std::chrono::steady_clock::now() < deadline)
			{
				GGLAB_UNUSED(runtime.Tick());
				std::this_thread::yield();
			}
			auto& rig = runtime.GetDemoManager()->GetActiveDemo()->GetCameraRig();
			checks.Check(rig.SetCameraPaths({ {
				.m_Id = "SEQ_RequiredProducts", .m_Name = "Required products", .m_Version = 1,
				.m_Keys = {
					{ .m_Frame = 0, .m_Position = { 0, 1, 0 }, .m_Target = { 0, 1, 10 } },
					{ .m_Frame = 1, .m_Position = { 1, 1, 0 }, .m_Target = { 1, 1, 10 } },
				},
			} }), "The production camera rig registers a deterministic required-product sequence");
			runtime.GetTime()->SetFixedDeltaTime(1.0 / 60.0);
			auto& sequence = *runtime.GetFrameSequenceCoordinator();
			auto& capture = *runtime.GetFrameCaptureCoordinator();
			std::string error;
			checks.Check(sequence.Start({ .m_CameraPathId = "SEQ_RequiredProducts", .m_CaptureFrames = { 0 },
				.m_Exports = { { .m_Frame = 0,
					.m_Targets = { { .m_Resource = missingResource ? "Missing.RequiredResource" : "FaultLifecycle.Color" } },
					.m_IncludeGraph = true } },
				.m_ReferenceSamples = referenceSamples }, error) != 0,
				"The production sequence accepts image, arrays and graph as one required-product plan");
			if (missingResource)
			{
				const auto executions = probe.m_GraphExecutions;
				const auto ends = probe.m_FrameEnds;
				const auto aborts = probe.m_FrameAborts;
				const auto maps = probe.m_ReadbackMaps;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
					probe.m_GraphExecutions == executions && probe.m_FrameEnds == ends &&
					probe.m_FrameAborts == aborts + 1 && probe.m_ReadbackMaps == maps,
					"A permanent compiled-resource failure aborts before image recording, readback or partial submission");
				std::vector<FrameCaptureRequestResult> results;
				runtime.ConsumeFrameCaptureResults(results);
				checks.Check(sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
					sequence.GetStatus()->m_SubmittedFrames == 0 && results.size() == 2 &&
					std::ranges::all_of(results, [](const auto& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Failed && result.m_ImagePath.empty() &&
							result.m_ResourcePaths.empty() && !result.m_Failure.empty();
					}) && capture.GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 && probe.m_ImageEncodes == 0,
					"The permanent failure fans out exactly once and drains every Runtime request without encoding its image");
			}
			else
			{
				probe.m_RequiredPlanDeferrals = 2;
				for (uint32_t attempt = 0; attempt < 8 && probe.m_RequiredPlanDeferrals == 2; ++attempt)
				{
					GGLAB_UNUSED(runtime.Tick());
				}
				const double time = runtime.GetTime()->GetTotalTime();
				const auto serial = capture.GetLastFrameState()->m_FrameSerial;
				const auto ids = sequence.GetStatus()->m_CaptureRequestIds;
				const auto executions = probe.m_GraphExecutions;
				const auto ends = probe.m_FrameEnds;
				const auto aborts = probe.m_FrameAborts;
				const auto maps = probe.m_ReadbackMaps;
				checks.Check(probe.m_RequiredPlanDeferrals == 1 && ids.size() == 2 &&
					sequence.GetStatus()->m_State == FrameSequenceState::Running &&
					sequence.GetStatus()->m_SubmittedFrames == 0 && capture.GetUnfinishedRequestCount() == 2 &&
					sequence.ShouldHoldTime() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 0,
					"Late production deferral retires bound Runtime owners but preserves "
					"both IDs and the unsubmitted sequence sample");
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_RequiredPlanDeferrals == 0 &&
					probe.m_GraphExecutions == executions && probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
					probe.m_ReadbackMaps == maps && runtime.GetTime()->GetTotalTime() == time &&
					capture.GetLastFrameState()->m_FrameSerial == serial + 1 &&
					sequence.GetStatus()->m_CaptureRequestIds == ids && sequence.GetStatus()->m_SubmittedFrames == 0,
					"A second real RAII abort records no GPU work, maps no payload "
					"and repeats the same simulation instant with fresh provenance");
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_FrameEnds == ends + 1 &&
					probe.m_GraphExecutions == executions + 1 && runtime.GetTime()->GetTotalTime() == time &&
					sequence.GetStatus()->m_SubmittedFrames == 1,
					"Only the successful complete-plan retry submits and advances the sequence "
					"at the preserved simulation instant");
				const auto successfulSerial = capture.GetLastFrameState()->m_FrameSerial;
				std::vector<FrameCaptureRequestResult> results;
				for (uint32_t attempt = 0; attempt < 100 && results.size() < 2; ++attempt)
				{
					GGLAB_UNUSED(runtime.Tick());
					runtime.ConsumeFrameCaptureResults(results);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				checks.Check(results.size() == 2 && std::ranges::all_of(results, [&](const auto& result)
					{
						return result.m_Status == FrameCaptureRequestStatus::Completed && result.m_Metadata &&
							result.m_Metadata->m_FrameSerial == successfulSerial && result.m_Metadata->m_TotalTime == time &&
							result.m_Metadata->m_Sequence && result.m_Metadata->m_Sequence->m_Frame == 0 &&
							std::ranges::find(ids, result.m_RequestId) != ids.end();
					}) && probe.m_ImageEncodes == 1 && capture.GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetResourceExportFrameAvailability() == FrameCaptureAvailability::Available,
					"Production image, graph and NPY publication uses only the successful attempt "
					"and releases all payload/write reservations");
			}
			runtime.Shutdown();
			std::vector<FrameCaptureRequestResult> late;
			runtime.ConsumeFrameCaptureResults(late);
			checks.Check(late.empty() && probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
				"Required-product teardown adds no duplicate terminal result and releases GPU owners only after safe settlement");
		}

		void RunPreviewChangeScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			RequestOutcomePreviews(runtime, probe);
			const auto before = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
			probe.m_ChangePreviewAfterBuild = true;
			const auto first = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
			const auto stale = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
			auto* resources = runtime.GetRenderServices().m_Resources;
			checks.Check(first.IsCompleted() && stale.m_Dirty && stale.m_Requested &&
				stale.m_PublishedLayout == before.m_Layout && stale.m_PublishedMip == before.m_SelectedMip &&
				stale.m_UpdateCount == before.m_UpdateCount + 1 &&
				resources->IsPostProcessPreviewRequested() && resources->IsDirty(RenderTextureIndex::Preview_PostProcess),
				"Build-to-execute selection changes preserve dirty state and newer requests while publishing only the drawn IBL selection");
			probe.m_ChangePreviewAfterBuild = false;
			const auto next = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
			const auto current = runtime.GetRenderHost()->GetIBLPreviewView()->GetIBLPreviewResourcesDiagnostics().m_EnvironmentPreview;
			checks.Check(next.IsCompleted() && !current.m_Requested &&
				current.m_PublishedLayout == static_cast<uint32_t>(IBLPreviewLayout::Grid2x3) &&
				current.m_PublishedMip == 1 && current.m_UpdateCount == stale.m_UpdateCount + 1,
				"The next requesting frame publishes the newer IBL layout and mip exactly once");
			CheckOutcomePreviews(checks, runtime, true);
			runtime.Shutdown();
		}

		void RunLateCommitScenario(SelfTestContext& checks, const std::filesystem::path& root,
			CommitEnqueueFault fault, AbortInjection abort, bool throughRuntime = true)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			const auto previousFence = probe.m_Presentation->GetLastSubmittedFencePoint();
			const auto previousTemporal = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics();
			const uint32_t ends = probe.m_FrameEnds;
			const uint32_t aborts = probe.m_FrameAborts;
			const uint32_t callbacks = probe.m_SubmittedCallbacks;
			const uint64_t capture = probe.m_CaptureControl->RequestCapture(FrameCaptureSource::Scene);
			probe.m_CommitEnqueueFault = fault;
			probe.m_AbortInjection = abort;
			const bool submitted = fault == CommitEnqueueFault::DuringSettlement;
			if (submitted && throughRuntime)
			{
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal,
					"Settlement-time late enqueue makes the production AppRuntime stop with Fatal");
			}
			else
			{
				const auto result = RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit);
				checks.Check(!result.IsCompleted() && result.GetSubmittedFence() ==
					(submitted ? probe.m_LastEndFence : probe.m_LastAbortFence),
					"Late enqueue returns Fatal with the real submission or abort fence");
			}
			checks.Check(probe.m_FrameEnds == ends + (submitted ? 1u : 0u) &&
				probe.m_FrameAborts == aborts + (submitted ? 0u : 1u) &&
				probe.m_SubmittedCallbacks == callbacks && probe.m_LateCommitsRun == 0 &&
				probe.m_CommitsRun == (submitted ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2 &&
				probe.m_CommitIdentityMatches,
				"Late enqueue never runs its callback, never resettles owners, and releases every CPU capture once");
			const auto expectedFence = submitted ? probe.m_LastEndFence :
				(probe.m_LastAbortFence.IsValid() ? probe.m_LastAbortFence : previousFence);
			checks.Check(probe.m_Presentation->GetLastSubmittedFencePoint() == expectedFence &&
				HasOutcomeTextureState(runtime, TransientPoolSlotState::PendingRetirement, expectedFence),
				"Late enqueue preserves the actual submission or abort retirement fence");
			const auto temporal = runtime.GetRenderServices().m_Temporal->GetTemporalHistoryDiagnostics();
			checks.Check(temporal.m_HistoryValid && (submitted ?
				temporal.m_LastCommitted.m_GraphicsFence == expectedFence &&
					probe.m_CommitObservations[0].m_Order == 1 && probe.m_CommitObservations[1].m_Order == 2 &&
					probe.m_CommitObservations[1].m_Fence == expectedFence :
				temporal.m_LastCommitted.m_GraphicsFence == previousTemporal.m_LastCommitted.m_GraphicsFence &&
					temporal.m_ResetCount == previousTemporal.m_ResetCount),
				"A settlement violation keeps Submitted history; a sealed-list abort preserves previous history");
			std::vector<FrameCaptureResult> results;
			probe.m_CaptureControl->ConsumeResults(results);
			checks.Check(submitted ? results.size() == 1 && results[0].m_RequestId == capture &&
				results[0].m_Status == FrameCaptureStatus::Completed && results[0].m_Image :
				results.empty() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 1,
				"Late enqueue does not change the capture's original Submitted or Discarded outcome");
			if (submitted && throughRuntime)
			{
				const uint32_t begins = probe.m_FrameBegins;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins,
					"AppRuntime begins no follow-up frame after settlement returns Fatal");
			}
			probe.m_RetainedExecuteContext.reset();
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
				probe.m_CommitsRun == (submitted ? 2u : 0u) && probe.m_CommitPayloadsDestroyed == 2,
				"Late-enqueue teardown neither repeats commits nor releases GPU resources early");
		}

		void RunForwardPlusValidationScenario(SelfTestContext& checks, const std::filesystem::path& root,
			RHIFenceQueryResult completion, bool resize, bool nativeQuery)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			probe.m_EnableForwardPlusReadback = true;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
				probe.m_ForwardPlusFence.IsValid() && probe.m_ForwardPlusReadback->GetScheduledCount() == 1,
				"The production Renderer adopts a Forward+ readback fence only after Submitted");
			const auto fence = probe.m_ForwardPlusFence;
			const auto begins = probe.m_FrameBegins;
			const auto ends = probe.m_FrameEnds;
			const auto aborts = probe.m_FrameAborts;
			const auto builds = probe.m_GraphBuilds;
			const auto executions = probe.m_GraphExecutions;
			const auto submits = probe.m_FrameSubmits;
			const auto maps = probe.m_ReadbackMaps;
			const auto preparations = probe.m_ValidationPreparations;
			probe.m_SafeToRelease = false;
			probe.m_ForwardPlusQuery = completion;
			probe.m_ObserveForwardPlusQuery = nativeQuery;
			probe.m_ResizeValidation = resize;
			if (completion == RHIFenceQueryResult::DeviceLost)
			{
				probe.m_Outcome = TeardownOutcome::DeviceLost;
			}
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
				probe.m_FrameBegins == begins + 1 && probe.m_FrameEnds == ends &&
				probe.m_FrameAborts == aborts + 1 && probe.m_GraphBuilds == builds &&
				probe.m_GraphExecutions == executions && probe.m_FrameSubmits == submits &&
				probe.m_ReadbackMaps == maps && probe.m_ValidationPreparations == preparations &&
				probe.m_ForwardPlusReadback->GetScheduledCount() == 1,
				"Readback validation fatally aborts the production host before preparation, graph build/compile/execute or resubmission");
			checks.Check(probe.m_LastValidation.m_Status == (completion == RHIFenceQueryResult::DeviceLost ?
				RenderFrameValidationStatus::DeviceLost : RenderFrameValidationStatus::ContractFailure) &&
				probe.m_LastValidation.m_Detail.find("value=" + std::to_string(fence.m_Value)) != std::string::npos &&
				probe.m_LastValidation.m_Detail.find(completion == RHIFenceQueryResult::DeviceLost ? "DeviceLost" :
					completion == RHIFenceQueryResult::Pending ? "Pending" :
					nativeQuery ? "native completion failure" : "rejected query") != std::string::npos,
				"The host receives the actual Forward+ classifier verdict and original query evidence even during resize");
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_FrameBegins == begins + 1,
				"Fatal Forward+ validation prevents the next frame from beginning in Debug and Release");
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
				probe.m_LateSubmits == 0 && probe.m_ReadbackMaps == maps,
				"Checked idle or confirmed-device-loss teardown releases retained resources without mapping or late submissions");
		}

		void RunPublicationRetentionScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool claimed)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto* capture = probe.m_CaptureControl;
			GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
			GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
			GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
			std::vector<FrameCaptureResult> products;
			capture->ConsumeResults(products);
			const bool shared = products.size() == 2 && products[0].m_Image &&
				products[0].m_Image == products[1].m_Image;
			checks.Check(shared, "Two production capture subscribers share one fenced immutable CPU image");
			if (!shared)
			{
				runtime.Shutdown();
				return;
			}
			std::weak_ptr<const FrameCaptureImage> image = products[0].m_Image;
			auto files = std::make_shared<CapturePublicationTestFileSystem>();
			files->m_Block = claimed ?
				CapturePublicationTestFileSystem::Block::Publication :
				CapturePublicationTestFileSystem::Block::TemporaryWrite;
			auto writer = std::make_unique<CaptureWriter>(
				[](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					return std::vector<uint8_t>{ 1, 2, 3 };
				}, false, files);
			std::array<std::shared_ptr<CaptureWriteJob>, 2> jobs;
			for (size_t i = 0; i < jobs.size(); ++i)
			{
				jobs[i] = std::make_shared<CaptureWriteJob>();
				jobs[i]->m_Products.m_Image = products[i].m_Image;
				jobs[i]->m_Directory = root / "Published";
				jobs[i]->m_BaseStem = i == 0 ? "running" : "queued";
				jobs[i]->m_RequestId = i + 1;
				checks.Check(writer->Submit(jobs[i]), "The production product enters the independent writer");
			}
			products.clear();
			checks.Check(files->WaitEntered(), "Production capture transport reaches the blocked publication boundary");
			// Seven other physical products saturate the real eight-slot budget.
			// The two writer subscribers must retain exactly one additional credit.
			for (uint32_t i = 0; i < 7; ++i)
			{
				GGLAB_UNUSED(capture->RequestCapture(FrameCaptureSource::Scene));
				GGLAB_UNUSED(RunOutcomeHostFrame(checks, runtime, probe, HostFrameAction::Submit));
				capture->ConsumeResults(products);
			}
			checks.Check(products.size() == 7 && capture->GetAvailability(RHIFormat::R8G8B8A8Unorm, 32, 32) ==
				FrameCaptureAvailability::Busy,
				"Blocked shared writer products conserve real Runtime payload credit after GPU buffers retire");
			const auto running = writer->Abandon(*jobs[0], std::chrono::milliseconds(0));
			const auto queued = writer->Abandon(*jobs[1], std::chrono::milliseconds(0));
			checks.Check(running.m_Publication == (claimed ?
				CapturePublicationOutcome::Indeterminate :
				CapturePublicationOutcome::NotPublished) &&
				queued.m_Publication == CapturePublicationOutcome::NotPublished &&
				capture->GetAvailability(RHIFormat::R8G8B8A8Unorm, 32, 32) == FrameCaptureAvailability::Busy,
				"Timeout receipts do not release credits still held by running or queued consumers");
			runtime.Shutdown();
			const auto maps = probe.m_ReadbackMaps;
			products.clear();
			writer.reset();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && !image.expired(),
				"The real Renderer and device can retire while an independent writer retains CPU products");
			files->Release();
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			for (const auto& job : jobs)
			{
				job->WaitUntil(deadline);
			}
			const auto late = jobs[0]->GetReceipt();
			const auto skipped = jobs[1]->GetReceipt();
			checks.Check(late && skipped &&
				late->m_Publication == (claimed ?
					CapturePublicationOutcome::Published :
					CapturePublicationOutcome::NotPublished) &&
				skipped->m_Publication == CapturePublicationOutcome::NotPublished && image.expired() &&
				probe.m_ReadbackMaps == maps &&
				std::filesystem::exists(root / "Published/running.json") == claimed &&
				!std::filesystem::exists(root / "Published/queued.json"),
				"Late worker completion releases the last CPU lease without touching a destroyed device or publishing abandoned work");
		}

		void RunFrameOutcomeScenarios(SelfTestContext& checks, const std::filesystem::path& root)
		{
			for (const auto injection : { FrameEndInjection::Completed, FrameEndInjection::FatalFenced,
				FrameEndInjection::FatalUnfenced, FrameEndInjection::RecordingFailure })
			{
				RunEndFrameOutcomeScenario(checks, root / ("end-" + std::to_string(static_cast<int>(injection))),
					injection, AbortInjection::Unfenced);
			}
			RunEndFrameOutcomeScenario(checks, root / "internal-abort-fenced",
				FrameEndInjection::RecordingFailure, AbortInjection::Fenced);
			for (const auto action : { HostFrameAction::DiscardAfterBuild,
				HostFrameAction::DiscardAfterRecording, HostFrameAction::RejectUploadOrder })
			{
				for (const auto abort : { AbortInjection::Unfenced, AbortInjection::Fenced })
				{
					const auto name = "abort-" + std::to_string(static_cast<int>(action)) +
						"-" + std::to_string(static_cast<int>(abort));
					RunDiscardOutcomeScenario(checks, root / name, action, abort);
				}
			}
			RunDiscardOutcomeScenario(checks, root / "abort-unknown", HostFrameAction::DiscardAfterRecording,
				AbortInjection::CompletionUnknown);
			RunExportDeferralOutcomeScenario(checks, root / "export-deferral");
			RunPreviewChangeScenario(checks, root / "preview-change");
			RunLateCommitScenario(checks, root / "late-sealed-unfenced",
				CommitEnqueueFault::BeforeSubmission, AbortInjection::Unfenced);
			RunLateCommitScenario(checks, root / "late-sealed-fenced",
				CommitEnqueueFault::BeforeSubmission, AbortInjection::Fenced);
			RunLateCommitScenario(checks, root / "late-settling",
				CommitEnqueueFault::DuringSettlement, AbortInjection::Unfenced);
			RunLateCommitScenario(checks, root / "late-settling-host-fence",
				CommitEnqueueFault::DuringSettlement, AbortInjection::Unfenced, false);
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
			probe.m_Presentation = runtime.GetRenderServices().m_Presentation;
			probe.m_EnableDeferredCommits = true;
			probe.m_Inject = true;
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_Faulted && probe.m_QuarantinedPages > 0,
				"An accepted submission without a signal quarantines actual frame uploads and fails the production runtime");
			checks.Check(probe.m_CaptureCopies > 0 && probe.m_ReadbackMaps == 0 && probe.m_EarlyDestructions == 0,
				"The retained production frame keeps graph, capture and IBL resources alive without reading GPU contents");
			checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == 2,
				"EndFrame CompletionUnknown drops plain CPU commits while retaining GPU-visible owners");
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
			checks.Check(probe.m_CommitsRun == 0 && probe.m_CommitPayloadsDestroyed == 2,
				"Unknown-frame callbacks never run or survive to the shutdown boundary");
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
		RunFrameOutcomeScenarios(context, root / "outcomes");
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-late", 0, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-reference", 3, false);
		RunRequiredPlanOutcomeScenario(context, root / "required-plan-missing", 0, true);
		RunPublicationRetentionScenario(context, root / "publication-before-claim", false);
		RunPublicationRetentionScenario(context, root / "publication-after-claim", true);
		for (const auto completion : { RHIFenceQueryResult::Pending, RHIFenceQueryResult::Failed,
			RHIFenceQueryResult::DeviceLost })
		{
			for (const bool resize : { false, true })
			{
				const auto name = "forward-plus-" + std::to_string(static_cast<int>(completion)) +
					(resize ? "-resize" : "-ready");
				RunForwardPlusValidationScenario(context, root / name, completion, resize, true);
			}
		}
		RunForwardPlusValidationScenario(context, root / "forward-plus-rejected",
			RHIFenceQueryResult::Failed, false, false);
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
