#include "GpuFaultLifecycleSelfTests.h"
#include "CapturePublicationTestFileSystem.h"
#include "Capture/CaptureEnvironment.h"
#include "Capture/CaptureWriter.h"
#include "ApplicationInput.h"
#include "Capture/FrameCaptureCoordinator.h"
#include "Capture/FrameCaptureImageEncoder.h"
#include "Capture/FrameSequenceCoordinator.h"
#include "Demo/DemoLoadingShell.h"
#include "Demo/DemoManager.h"
#include "GGLabAppRuntime.h"
#include "Parameters/SessionParameterControl.h"
#include "Lab/LabRuntime.h"
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

#include <nlohmann/json.hpp>

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
			bool m_EnableParameterLab = false;
			bool m_FrameBeginUnavailable = false;
			uint32_t m_ContentSerialOffset = 0;
			uint64_t m_PlannedContentIdentity = 0;
			uint64_t m_ExpectedReleasedBatch = 0;
			bool m_BindingReleasedBeforeExit = false;
			uint32_t m_DemoDestructions = 0;
			bool m_EnableLightingProfiles = false;
			bool m_ProfileNamedLabBool = false;
			bool m_InvalidLightingProfiles = false;
			bool m_EquivalentLightingProfiles = false;
			uint32_t m_LightingProfileQueries = 0;
			bool m_ObserveParameterCommit = false;
			bool m_ParameterCommitWasComplete = false;
			uint64_t m_ExpectedCommitGeneration = 0;
			LabRuntime* m_ParameterRuntime = nullptr;
			SessionParameterControl* m_ParameterControl = nullptr;
			uint32_t m_ParameterApplications = 0;
			uint64_t m_ParameterBuiltSerial = 0;
			float m_ParameterBuiltRadius = 0.0f;
			ResolvedViewRenderSettings m_ParameterBuiltView{};
			bool m_SkipParameterFrame = false;
			bool m_SkipLightingPublication = false;
			bool m_RebakeAfterParameterBuild = false;
			bool m_UnrelatedLightingChange = false;
			EnvironmentLightingControlBase* m_ParameterEnvironment = nullptr;
			bool m_ChangeParameterAfterBuild = false;
			bool m_Inject = false;
			bool m_Faulted = false;
			bool m_SafeToRelease = true;
			bool m_HoldUploads = false;
			uint32_t m_EarlyDestructions = 0;
			uint32_t m_ReadbackMaps = 0;
			uint32_t m_LiveReadbackBuffers = 0;
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
			GGLabAppRuntime* m_AppRuntime = nullptr;
			uint64_t m_CancelJobDuringBuild = 0;
			std::optional<CaptureCancelResult> m_CancellationResult;
			bool m_HoldCaptureFence = false;
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
				else
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
				if (!m_Probe.m_EnableLightingProfiles)
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
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("radius"),
					.m_Name = "Radius", .m_Type = LabParameterType::Float, .m_DefaultValue = 1.0f,
					.m_MinValue = LabValue(0.0f), .m_MaxValue = LabValue(4.0f) }));
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("layout"),
					.m_Name = "Layout", .m_Type = LabParameterType::UInt,
					.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = uint32_t(1) }));
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("deferred"),
					.m_Name = "Deferred", .m_Type = LabParameterType::Bool, .m_DefaultValue = false,
					.m_EffectClass = ParameterEffectClass::Deferred }));
				GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("rebake"),
					.m_Name = "Misdeclared rebake", .m_Type = LabParameterType::Bool, .m_DefaultValue = false }));
				if (m_Probe.m_ProfileNamedLabBool)
				{
					GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId("environment.profile"),
						.m_Name = "Lab-owned boolean", .m_Type = LabParameterType::Bool, .m_DefaultValue = false }));
				}
				for (const auto id : { "lighting", "broken-lighting" })
				{
					GGLAB_UNUSED(GetMutableParameters().Add({ .m_Id = LabParameterId(id),
						.m_Name = id, .m_Type = LabParameterType::UInt, .m_DefaultValue = uint32_t(1),
						.m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(4096)),
						.m_EffectClass = ParameterEffectClass::Deferred }));
				}
			}
			std::optional<LabLightingParameterRequest> GetLightingParameterRequest(
				const LabParameterId& id) const noexcept override
			{
				if (id == LabParameterId("lighting") || id == LabParameterId("broken-lighting"))
				{
					return m_LightingRequest;
				}
				return std::nullopt;
			}
			void Update(float) noexcept override { GetCamera().Update(); }

		private:
			void ApplyImmediateParameters() noexcept override
			{
				++m_Probe.m_ParameterApplications;
				if (std::exchange(m_Probe.m_ObserveParameterCommit, false))
				{
					const auto* control = m_Probe.m_ParameterControl;
					const auto* batch = control ? control->GetParameterBatchStatus() : nullptr;
					m_Probe.m_ParameterCommitWasComplete = batch && batch->m_State == ParameterBatchState::Applying &&
						batch->m_Items.size() == 4 && batch->m_Revision == control->GetParameterRevision() &&
						control->GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == 11.0f &&
						control->GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 1.0f &&
						GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f &&
						m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration() == m_Probe.m_ExpectedCommitGeneration;
				}
				const auto samples = GetParameters().Get(LabParameterId("lighting"), uint32_t(1));
				const auto broken = GetParameters().Get(LabParameterId("broken-lighting"), uint32_t(1));
				if (samples != m_Samples || broken != m_Broken)
				{
					m_LightingRequest.m_PreviousGeneration =
						m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
					m_Services.m_EnvironmentLightingControl->RequestRebake();
					m_LightingRequest.m_Generation =
						m_Services.m_RenderServices.m_Environment->GetBakeRequestGeneration();
					if (broken != m_Broken)
					{
						// Deliberately misreport owner evidence; admission remains valid.
						m_LightingRequest.m_PreviousGeneration = 0;
					}
					m_Samples = samples;
					m_Broken = broken;
				}
				GetMutableViewRenderProfile().m_Lighting.m_GTAO.m_Radius =
					GetParameters().Get(LabParameterId("radius"), 1.0f);
				const bool rebake = GetParameters().Get(LabParameterId("rebake"), false);
				if (rebake != m_Rebake)
				{
					m_Rebake = rebake;
					m_Services.m_EnvironmentLightingControl->RequestRebake();
				}
			}
			LifetimeProbe& m_Probe;
			bool m_Rebake = false;
			uint32_t m_Samples = 1;
			uint32_t m_Broken = 1;
			LabLightingParameterRequest m_LightingRequest{};
		};

		std::unique_ptr<LabSessionBase> CreateParameterTestLab(const LabSessionCreateInfo& info) noexcept
		{
			return std::make_unique<ParameterTestLab>(info);
		}

		class FaultDemo final : public DemoBase
		{
		public:
			explicit FaultDemo(const DemoCreateInfo& info) : m_CameraOwner(info),
				m_Probe(static_cast<FaultContext*>(info.m_Services.m_RHIContext)->m_Probe),
				m_Pipeline(m_Probe)
			{
				m_Probe.m_ParameterEnvironment = info.m_Services.m_EnvironmentLightingControl;
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
			const std::filesystem::path& root, FrameCaptureImageEncoder imageEncoder = {},
			CaptureJobLimits jobLimits = {})
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
				checks.Check(sequence.GetStatus()->m_State == FrameSequenceState::Failed &&
					sequence.GetStatus()->m_SubmittedFrames == 0 && runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 &&
					sequence.GetStatus()->m_AcceptedCaptureJobs == 1 && sequence.GetStatus()->m_FinishedCaptureJobs == 1 &&
					sequence.GetStatus()->m_Failure.find("UnknownResource") != std::string::npos &&
					runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && capture.GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 && probe.m_ImageEncodes == 0,
					"The permanent failure consumes one sequence receipt without a legacy fan-out, encoding or leaked reservation");
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
				const auto accepted = sequence.GetStatus()->m_AcceptedCaptureJobs;
				const auto executions = probe.m_GraphExecutions;
				const auto ends = probe.m_FrameEnds;
				const auto aborts = probe.m_FrameAborts;
				const auto maps = probe.m_ReadbackMaps;
				checks.Check(probe.m_RequiredPlanDeferrals == 1 && accepted == 1 &&
					sequence.GetStatus()->m_State == FrameSequenceState::Running &&
					sequence.GetStatus()->m_SubmittedFrames == 0 && capture.GetUnfinishedRequestCount() == 2 &&
					sequence.ShouldHoldTime() && probe.m_CaptureControl->GetUnfinishedRequestCount() == 0,
					"Late production deferral retires bound Runtime owners but preserves "
					"the accepted job and unsubmitted sequence sample");
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_RequiredPlanDeferrals == 0 &&
					probe.m_GraphExecutions == executions && probe.m_FrameEnds == ends && probe.m_FrameAborts == aborts + 1 &&
					probe.m_ReadbackMaps == maps && runtime.GetTime()->GetTotalTime() == time &&
					capture.GetLastFrameState()->m_FrameSerial == serial + 1 &&
					sequence.GetStatus()->m_AcceptedCaptureJobs == accepted && sequence.GetStatus()->m_SubmittedFrames == 0 &&
					capture.GetJobSnapshot(1).has_value(),
					"A second real RAII abort records no GPU work, maps no payload "
					"and repeats the same simulation instant with fresh provenance");
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue && probe.m_FrameEnds == ends + 1 &&
					probe.m_GraphExecutions == executions + 1 && runtime.GetTime()->GetTotalTime() == time &&
					sequence.GetStatus()->m_SubmittedFrames == 1,
					"Only the successful complete-plan retry submits and advances the sequence "
					"at the preserved simulation instant");
				const auto successfulSerial = capture.GetLastFrameState()->m_FrameSerial;
				for (uint32_t attempt = 0; attempt < 100 && sequence.IsActive(); ++attempt)
				{
					GGLAB_UNUSED(runtime.Tick());
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				std::vector<nlohmann::json> manifests;
				if (std::filesystem::exists(root / "Captures"))
				{
					for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Captures"))
					{
						if (entry.path().filename().string().ends_with(".capture-bundle.json"))
						{
							std::ifstream file(entry.path());
							manifests.push_back(nlohmann::json::parse(file));
						}
					}
				}
				checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && manifests.size() == 1 &&
					manifests.front()["products"].size() == 3 &&
					manifests.front()["provenance"]["frame"]["serial"] == successfulSerial &&
					manifests.front()["provenance"]["time"]["totalTime"] == time &&
					manifests.front()["provenance"]["sequence"]["frame"] == 0 &&
					sequence.GetStatus()->m_State == FrameSequenceState::Completed &&
					sequence.GetStatus()->m_CompletedCaptures == 2 && sequence.GetStatus()->m_FinishedCaptureJobs == 1 &&
					runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
					probe.m_ImageEncodes == 1 && capture.GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 &&
					probe.m_CaptureControl->GetResourceExportFrameAvailability() == FrameCaptureAvailability::Available,
					"Production image, graph and NPY publication uses only the successful attempt "
					"and releases all payload/write reservations");
			}
			runtime.Shutdown();
			checks.Check(runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
				"Required-product teardown adds no duplicate terminal result and releases GPU owners only after safe settlement");
		}

		void RunOwnedJobRoutingScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			const auto waitingPlan = []()
				{
					return CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} },
						.m_Timing = FrameCaptureTiming::AfterReady,
						.m_RequiredContentId = "Missing.RoutingContent" };
				};
			auto plan = waitingPlan();
			const auto unavailable = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(unavailable.m_Status == CaptureAdmissionStatus::Rejected &&
				unavailable.m_Reason == CaptureAdmissionReason::ServicesUnavailable && unavailable.m_JobId == 0 &&
				plan.m_RequiredProducts.size() == 1 && !runtime.FindCaptureJobResult(0) &&
				!runtime.TakeCaptureJobResult(1) && !runtime.GetCaptureJobSnapshot(1) &&
				runtime.CancelCaptureJob(1) == CaptureCancelResult::NotFound &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0,
				"Uncomposed AppRuntime rejects job intake without consuming plans or manufacturing receipts");
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root, {},
				{ .m_MaxActiveJobs = 1, .m_MaxReceipts = 2 }))
			{
				return;
			}
			auto& capture = *runtime.GetFrameCaptureCoordinator();
			const auto first = runtime.TrySubmitCaptureJob(std::move(plan));
			plan = waitingPlan();
			const auto busy = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(first.m_Status == CaptureAdmissionStatus::Accepted && first.m_JobId == 1 &&
				busy.m_Status == CaptureAdmissionStatus::Busy && busy.m_Reason == CaptureAdmissionReason::ActiveJobLimit &&
				plan.m_RequiredProducts.size() == 1 && runtime.GetCaptureIntakeSnapshot().m_Limits.m_MaxReceipts == 2 &&
				runtime.GetCaptureJobSnapshot(first.m_JobId)->m_Stage == CaptureJobStage::Waiting,
				"AppRuntime applies its configured shared active and receipt limits before moving a caller's plan");
			checks.Check(runtime.CancelCaptureJob(first.m_JobId) == CaptureCancelResult::Cancelled &&
				!capture.TakeJobResult(first.m_JobId) && !runtime.TakeCaptureJobResult(first.m_JobId) &&
				!runtime.FindCaptureJobResult(first.m_JobId) &&
				runtime.GetCaptureIntakeSnapshot().m_AppRuntimeUnpublishedResults == 1,
				"A terminal receipt cannot escape through the coordinator or host before AppRuntime routes it");
			const auto second = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(second.m_JobId == 2 && runtime.CancelCaptureJob(second.m_JobId) == CaptureCancelResult::Cancelled,
				"Terminal completion returns active credit without releasing the reserved host receipt");
			capture.Update();
			checks.Check(!runtime.FindCaptureJobResult(first.m_JobId) && !runtime.FindCaptureJobResult(second.m_JobId),
				"Updating transport directly does not bypass AppRuntime's host-publication boundary");
			plan = waitingPlan();
			bool bounded = true;
			for (uint32_t attempt = 0; attempt < 64; ++attempt)
			{
				const auto full = runtime.TrySubmitCaptureJob(std::move(plan));
				bounded &= full.m_Status == CaptureAdmissionStatus::Busy && full.m_JobId == 0 &&
					full.m_Reason == CaptureAdmissionReason::ReceiptLimit;
			}
			checks.Check(bounded && plan.m_RequiredProducts.size() == 1 &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
				"Unconsumed terminal receipts bound repeated intake even before the host polls");
			GGLAB_UNUSED(runtime.Tick());
			const CaptureJobResult* borrowed = runtime.FindCaptureJobResult(first.m_JobId);
			const auto published = runtime.GetCaptureIntakeSnapshot();
			checks.Check(borrowed && borrowed->m_Status == CaptureJobStatus::Cancelled &&
				borrowed->m_Publication == CaptureJobPublication::NotPublished &&
				runtime.FindCaptureJobResult(first.m_JobId) == borrowed && runtime.FindCaptureJobResult(second.m_JobId) &&
				published.m_ActiveJobs == 0 && published.m_ReservedReceipts == 2 && published.m_TerminalReceipts == 2 &&
				published.m_AppRuntimeUnpublishedResults == 0 && published.m_AppRuntimePublishedResults == 2,
				"Tick publishes immutable receipts to multiple non-consuming observers "
				"without moving or releasing their slots");
			checks.Check(!capture.TakeJobResult(first.m_JobId) && !runtime.TakeCaptureJobResult(0) &&
				!runtime.TakeCaptureJobResult(999) &&
				capture.TrySubmit(std::move(plan)).m_Reason == CaptureAdmissionReason::ReceiptLimit &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
				"Neither the standalone drain nor direct coordinator admission bypasses retained host capacity");
			const auto consumed = runtime.TakeCaptureJobResult(second.m_JobId);
			const auto third = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(consumed && consumed->m_JobId == second.m_JobId && third.m_JobId == 3 &&
				!runtime.TakeCaptureJobResult(second.m_JobId) && !runtime.GetCaptureJobSnapshot(second.m_JobId) &&
				runtime.FindCaptureJobResult(first.m_JobId) == borrowed &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 2,
				"Out-of-order consumption returns exactly one slot; reuse preserves another observer's borrowed receipt");
			runtime.HandleHostEvent({ .m_Type = AppHostEventType::ExitRequested });
			plan = waitingPlan();
			const auto exiting = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(exiting.m_Status == CaptureAdmissionStatus::Rejected &&
				exiting.m_Reason == CaptureAdmissionReason::ShuttingDown && plan.m_RequiredProducts.size() == 1,
				"An exiting AppRuntime stops unified intake before shutdown without consuming the plan");
			runtime.Shutdown();
			runtime.Shutdown();
			const auto stopped = runtime.GetCaptureIntakeSnapshot();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 &&
				runtime.FindCaptureJobResult(first.m_JobId) == borrowed && runtime.FindCaptureJobResult(third.m_JobId) &&
				stopped.m_ActiveJobs == 0 && stopped.m_ReservedReceipts == 2 &&
				stopped.m_AppRuntimePublishedResults == 2 &&
				runtime.CancelCaptureJob(first.m_JobId) == CaptureCancelResult::Finished,
				"Repeated shutdown retains and publishes each reserved receipt after device teardown, "
				"with every receipt retaining its original bounded slot");
			const auto firstResult = runtime.TakeCaptureJobResult(first.m_JobId);
			const auto thirdResult = runtime.TakeCaptureJobResult(third.m_JobId);
			checks.Check(firstResult && thirdResult && thirdResult->m_Status == CaptureJobStatus::Cancelled &&
				!runtime.TakeCaptureJobResult(third.m_JobId) && !runtime.FindCaptureJobResult(first.m_JobId) &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
				runtime.TrySubmitCaptureJob(std::move(plan)).m_Reason == CaptureAdmissionReason::ShuttingDown,
				"The host can consume final results exactly once after shutdown without reopening intake");
		}

		void RunOwnedJobFailureScenario(SelfTestContext& checks, const std::filesystem::path& root,
			FrameEndInjection injection, bool completionUnknown)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			const auto admitted = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			const auto maps = probe.m_ReadbackMaps;
			probe.m_FrameEndInjection = injection;
			probe.m_Inject = completionUnknown;
			checks.Check(admitted.m_Status == CaptureAdmissionStatus::Accepted &&
				runtime.Tick() == AppRuntimeTickResult::Fatal && probe.m_ReadbackMaps == maps,
				"Submission failure and unknown completion fail the production host "
				"without mapping a job's GPU data");
			runtime.Shutdown();
			const auto* borrowed = runtime.FindCaptureJobResult(admitted.m_JobId);
			checks.Check(borrowed && borrowed->m_Status == CaptureJobStatus::Failed &&
				borrowed->m_Publication == CaptureJobPublication::NotPublished && borrowed->m_ManifestPath.empty() &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 1 &&
				runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 1 &&
				probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && probe.m_LiveReadbackBuffers == 0,
				"Fatal shutdown publishes one reserved failure receipt only through "
				"its AppRuntime owner after safe GPU retirement");
			const auto result = runtime.TakeCaptureJobResult(admitted.m_JobId);
			runtime.Shutdown();
			checks.Check(result && result->m_Products.size() == 1 && !result->m_Products.front().m_Failure.empty() &&
				!runtime.TakeCaptureJobResult(admitted.m_JobId) && runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 &&
				runtime.GetLifecycleState() == AppRuntimeLifecycleState::Failed,
				"Post-shutdown host consumption returns failure credit once "
				"and leaves no published receipt after consumption");
		}

		void RunOwnedWriterDrainScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			std::atomic<bool> entered = false;
			std::atomic<bool> release = false;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
				[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					++probe.m_ImageEncodes;
					entered = true;
					while (!release)
					{
						std::this_thread::yield();
					}
					return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
				}, { .m_MaxActiveJobs = 1, .m_MaxReceipts = 1 }))
			{
				return;
			}
			const auto admitted = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!entered && std::chrono::steady_clock::now() < deadline)
			{
				GGLAB_UNUSED(runtime.Tick());
				std::this_thread::yield();
			}
			checks.Check(entered && runtime.CancelCaptureJob(admitted.m_JobId) == CaptureCancelResult::Cancelled,
				"The AppRuntime cancellation API withdraws delivery "
				"while the real writer is blocked inside its codec");
			GGLAB_UNUSED(runtime.Tick());
			const auto result = runtime.TakeCaptureJobResult(admitted.m_JobId);
			const auto draining = runtime.GetCaptureIntakeSnapshot();
			checks.Check(result && result->m_Status == CaptureJobStatus::Cancelled &&
				result->m_Publication == CaptureJobPublication::NotPublished && draining.m_ReservedReceipts == 0 &&
				draining.m_DrainingWriterJobs == 1 && draining.m_DrainingWriteReservations == 1,
				"Host acknowledgement releases only the receipt; "
				"a cancelled codec retains its independent writer reservation");
			const auto replacement = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} },
				.m_Timing = FrameCaptureTiming::AfterReady, .m_RequiredContentId = "Missing.WriterContent" });
			checks.Check(replacement.m_Status == CaptureAdmissionStatus::Accepted &&
				runtime.GetCaptureIntakeSnapshot().m_DrainingWriteReservations == 1,
				"Reusing an acknowledged receipt slot neither loses nor duplicates "
				"the cancelled writer's drain ownership");
			release = true;
			const auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (runtime.GetCaptureIntakeSnapshot().m_DrainingWriterJobs != 0 &&
				std::chrono::steady_clock::now() < drainDeadline)
			{
				GGLAB_UNUSED(runtime.Tick());
				std::this_thread::yield();
			}
			runtime.Shutdown();
			const auto replacementResult = runtime.TakeCaptureJobResult(replacement.m_JobId);
			bool manifest = false;
			if (std::filesystem::exists(root / "Captures"))
			{
				for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "Captures"))
				{
					manifest |= entry.path().filename().string().ends_with(".capture-bundle.json");
				}
			}
			checks.Check(replacementResult && replacementResult->m_Status == CaptureJobStatus::Cancelled &&
				!runtime.TakeCaptureJobResult(admitted.m_JobId) && !manifest &&
				runtime.GetCaptureIntakeSnapshot().m_DrainingWriterJobs == 0 &&
				runtime.GetCaptureIntakeSnapshot().m_DrainingWriteReservations == 0 &&
				runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 0 && probe.m_EarlyDestructions == 0,
				"Late codec completion retires its writer without publishing a manifest "
				"or emitting another host result");
		}

		void RunAdmittedJobScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool missingResource, bool multipleImages = false)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
				[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					++probe.m_ImageEncodes;
					return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
				}))
			{
				return;
			}
			auto& capture = *runtime.GetFrameCaptureCoordinator();
			CapturePlan plan{ .m_RequiredProducts = {
				ImageCaptureProduct{}, ResourceCaptureProduct{ { {
					.m_Resource = missingResource ? "Missing.AdmittedResource" : "FaultLifecycle.Color" } } },
				GraphCaptureProduct{} } };
			if (multipleImages)
			{
				plan.m_RequiredProducts.push_back(ImageCaptureProduct{ CaptureImageSource::Composited });
			}
			const auto admission = runtime.TrySubmitCaptureJob(std::move(plan));
			checks.Check(admission.m_Status == CaptureAdmissionStatus::Accepted && admission.m_JobId != 0,
				"The production AppRuntime accepts one logical job for image, resource and graph products");
			const auto maps = probe.m_ReadbackMaps;
			const auto aborts = probe.m_FrameAborts;
			if (!missingResource)
			{
				probe.m_RequiredPlanDeferrals = 2;
				GGLAB_UNUSED(runtime.Tick());
				GGLAB_UNUSED(runtime.Tick());
				const auto snapshot = runtime.GetCaptureJobSnapshot(admission.m_JobId);
				checks.Check(snapshot && snapshot->m_AttemptCount == 2 &&
					snapshot->m_Stage == CaptureJobStage::Waiting && probe.m_FrameAborts == aborts + 2 &&
					probe.m_ReadbackMaps == maps && !runtime.TakeCaptureJobResult(admission.m_JobId) &&
					capture.GetIntakeSnapshot().m_ReservedReceipts == 1,
					"Production late deferral safely aborts twice while retaining one accepted job and receipt");
			}
			std::optional<CaptureJobResult> result;
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
			while (!result && std::chrono::steady_clock::now() < deadline)
			{
				GGLAB_UNUSED(runtime.Tick());
				result = runtime.TakeCaptureJobResult(admission.m_JobId);
				std::this_thread::yield();
			}
			checks.Check(result && result->m_Products.size() == (multipleImages ? 4u : 3u) &&
				result->m_Status == (missingResource ? CaptureJobStatus::Failed : CaptureJobStatus::Completed) &&
				result->m_Publication == (missingResource ? CaptureJobPublication::NotPublished : CaptureJobPublication::Published) &&
				result->m_ManifestPath.empty() == missingResource,
				std::format("Production compiled failure or successful GPU transport reaches exactly one logical job result ({}).",
					result && !result->m_Products.empty() ? result->m_Products.front().m_Failure : "no receipt"));
			if (result && result->m_Products.front().m_Metadata)
			{
				const auto serial = result->m_Products.front().m_Metadata->m_FrameSerial;
				checks.Check(std::ranges::all_of(result->m_Products, [&](const auto& product)
					{
						return product.m_Metadata && product.m_Metadata->m_FrameSerial == serial;
					}) && probe.m_ImageEncodes == (missingResource ? 0u : multipleImages ? 2u : 1u) &&
					(!missingResource || (probe.m_ReadbackMaps == maps && probe.m_FrameAborts == aborts + 1)),
					"Job product provenance comes from one actual attempt; compiled failure maps and encodes nothing");
			}
			if (multipleImages)
			{
				checks.Check(result && result->m_Products.size() == 4 &&
					result->m_Products[0].m_Metadata && result->m_Products[3].m_Metadata &&
					result->m_Products[0].m_Metadata->m_Source == FrameCaptureSource::Scene &&
					result->m_Products[3].m_Metadata->m_Source == FrameCaptureSource::Composited &&
					result->m_Products[0].m_Files != result->m_Products[3].m_Files,
					"Distinct same-frame image sources compile separate graph imports and publish separate product files");
			}
			checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && capture.GetIntakeSnapshot().m_ReservedReceipts == 0 &&
				capture.GetUnfinishedRequestCount() == 0 && probe.m_CaptureControl->GetUnfinishedRequestCount() == 0,
				"Production receipt consumption releases all host and transport reservations");
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(!runtime.TakeCaptureJobResult(admission.m_JobId), "A later production tick cannot repeat the terminal receipt");
			runtime.Shutdown();
			checks.Check(probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0,
				"Admitted-job teardown preserves the production GPU ownership settlement order");
		}

		void RunJobCancellationScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool beforeSubmit)
		{
			LifetimeProbe probe;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root,
				[&](const FrameCaptureImage&) -> std::optional<std::vector<uint8_t>>
				{
					++probe.m_ImageEncodes;
					return std::vector<uint8_t>{ 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
				}))
			{
				return;
			}
			auto& capture = *runtime.GetFrameCaptureCoordinator();
			probe.m_AppRuntime = &runtime;
			const auto cancelled = runtime.TrySubmitCaptureJob(beforeSubmit ?
				CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{},
					ResourceCaptureProduct{ { { .m_Resource = "FaultLifecycle.Color" } } }, GraphCaptureProduct{} } } :
				CapturePlan{ .m_RequiredProducts = { ImageCaptureProduct{} } });
			checks.Check(cancelled.m_Status == CaptureAdmissionStatus::Accepted,
				"The production cancellation harness accepts its logical job");
			const auto maps = probe.m_ReadbackMaps;
			const auto aborts = probe.m_FrameAborts;
			const auto ends = probe.m_FrameEnds;
			const auto copies = probe.m_CaptureCopies;
			std::optional<CaptureJobResult> cancelledResult;
			if (beforeSubmit)
			{
				probe.m_CancelJobDuringBuild = cancelled.m_JobId;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
					probe.m_CancellationResult == CaptureCancelResult::Cancelled &&
					probe.m_FrameAborts == aborts + 1 && probe.m_FrameEnds == ends &&
					probe.m_CaptureCopies == copies && probe.m_ReadbackMaps == maps,
					"Cancelling after image binding aborts through real frame RAII before graph execution, mapping or submission");
				capture.Update();
				checks.Check(capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0 &&
					probe.m_CaptureControl->GetUnfinishedRequestCount() == 0 && probe.m_ImageEncodes == 0,
					"The real abort drains provisional cancelled capture owners without encoding a partial bundle");
			}
			else
			{
				const auto surviving = runtime.TrySubmitCaptureJob({ .m_RequiredProducts = { ImageCaptureProduct{} } });
				probe.m_HoldCaptureFence = true;
				GGLAB_UNUSED(runtime.Tick());
				const auto buffers = probe.m_LiveReadbackBuffers;
				checks.Check(capture.GetJobSnapshot(cancelled.m_JobId)->m_Stage == CaptureJobStage::AwaitingProducts &&
					probe.m_FrameEnds == ends + 1 && probe.m_CaptureCopies == copies + 1 && buffers != 0 &&
					probe.m_ReadbackMaps == maps && probe.m_CaptureControl->GetUnfinishedRequestCount() == 2,
					"Two production subscribers share one image copy while its certified submission remains pending");
				checks.Check(runtime.CancelCaptureJob(cancelled.m_JobId) == CaptureCancelResult::Cancelled,
					"The submitted production job accepts delivery cancellation");
				capture.Update();
				checks.Check(probe.m_LiveReadbackBuffers == buffers && probe.m_ReadbackMaps == maps &&
					capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 1 &&
					!runtime.TakeCaptureJobResult(surviving.m_JobId),
					"Cancelling one subscriber neither maps nor retires its pending shared GPU buffer");
				GGLAB_UNUSED(runtime.Tick());
				cancelledResult = runtime.TakeCaptureJobResult(cancelled.m_JobId);
				checks.Check(cancelledResult && runtime.GetCaptureIntakeSnapshot().m_ReservedReceipts == 1 &&
					runtime.GetCaptureIntakeSnapshot().m_DrainingRuntimeRequests == 1 &&
					probe.m_LiveReadbackBuffers == buffers && probe.m_ReadbackMaps == maps,
					"Host receipt consumption returns no GPU or draining reservation while the shared submission is pending");
				probe.m_HoldCaptureFence = false;
				const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				std::optional<CaptureJobResult> result;
				while (!result && std::chrono::steady_clock::now() < deadline)
				{
					GGLAB_UNUSED(runtime.Tick());
					result = runtime.TakeCaptureJobResult(surviving.m_JobId);
					std::this_thread::yield();
				}
				checks.Check(result && result->m_Status == CaptureJobStatus::Completed &&
					std::filesystem::exists(result->m_ManifestPath) && probe.m_ImageEncodes == 1 &&
					capture.GetIntakeSnapshot().m_DrainingRuntimeRequests == 0,
					"Certified completion delivers and publishes only the surviving subscriber and returns cancelled drain credit");
			}
			if (!cancelledResult)
			{
				cancelledResult = runtime.TakeCaptureJobResult(cancelled.m_JobId);
			}
			checks.Check(cancelledResult && cancelledResult->m_Status == CaptureJobStatus::Cancelled &&
				cancelledResult->m_Publication == CaptureJobPublication::NotPublished && cancelledResult->m_ManifestPath.empty(),
				"Production draining preserves exactly the original cancelled, unpublished job receipt");
			runtime.Shutdown();
			checks.Check(runtime.GetCaptureIntakeSnapshot().m_AppRuntimePublishedResults == 0 && !runtime.TakeCaptureJobResult(cancelled.m_JobId) &&
				probe.m_ContextDestructions == 1 && probe.m_EarlyDestructions == 0 && probe.m_LiveReadbackBuffers == 0,
				"Cancellation shutdown preserves GPU ownership and emits no duplicate terminal result");
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
				const std::string runIdentity = CreateCaptureRunIdentity();
				jobs[i]->m_Bundle = { .m_RunIdentity = runIdentity,
					.m_Environment = SerializeCaptureEnvironment({}, runIdentity),
					.m_JobId = i + 1, .m_Attempt = 1, .m_ProductCount = 1,
					.m_Deliveries = { { .m_Input = { .m_Image = products[i].m_Image,
						.m_Submission = { products[i].m_SubmissionFenceIndex, products[i].m_SubmissionFenceGeneration,
							products[i].m_SubmissionFenceValue, products[i].m_AdapterCompatibilityIdentity },
						.m_Metadata = { .m_RequestId = i + 1, .m_Backend = "dx12",
							.m_FrameSerial = products[i].m_FrameSerial, .m_Width = 32, .m_Height = 32 } } } } };
				jobs[i]->m_Directory = root / "Published";
				jobs[i]->m_BaseStem = i == 0 ? "running" : "queued";
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
				std::filesystem::exists(root / "Published/running.capture-bundle.json") == claimed &&
				!std::filesystem::exists(root / "Published/queued.capture-bundle.json"),
				"Late worker completion releases the last CPU lease without touching a destroyed device or publishing abandoned work");
		}

		void RunHostParameterScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool withLab)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = withLab;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& parameters = *runtime.GetParameterControl();
			probe.m_ParameterControl = &parameters;
			const auto revisionBeforeDiscovery =
				runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_ParameterRevision;
			checks.Check(revisionBeforeDiscovery != 0,
				"Production frames already carry a valid parameter identity before observer activation");
			parameters.Enable();
			checks.Check(parameters.IsAvailable() && parameters.GetHostParameters().GetParameters().size() == 25 &&
				(parameters.GetLabParameters() != nullptr) == withLab,
				"Host descriptors are available with Demo content and alongside the active Lab source");
			std::string error;
			const auto initial = parameters.GetParameterRevision();
			const LabParameterId inheritedRadius("view.gtao.radius");
			const std::array inherited{ LabParameterValue{ inheritedRadius,
				parameters.GetHostParameters().Find(inheritedRadius)->m_Value } };
			const auto unchangedId = parameters.RequestParameterBatch(inherited, error);
			GGLAB_UNUSED(runtime.Tick());
			const auto unchanged = parameters.TakeParameterBatchResult(unchangedId);
			checks.Check(unchanged && unchanged->m_Items.front().m_State == ParameterEffectState::NoChange &&
				parameters.GetParameterRevision() == initial &&
				runtime.GetFrameCaptureCoordinator()->GetLastFrameState()->m_ParameterRevision == revisionBeforeDiscovery,
				"Initial discovery and a first NoChange preserve the frame identity used by capture settling");
			for (const auto& invalid : std::array{
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 25.0f },
				LabParameterValue{ LabParameterId("view.gtao.direction_count"), int32_t(2) },
				LabParameterValue{ LabParameterId("view.taa.history_filter"), int32_t(99) },
				LabParameterValue{ LabParameterId("unknown.host"), true } })
			{
				std::vector<LabParameterValue> values{ { LabParameterId("view.gtao.radius"), 3.0f }, invalid };
				if (withLab)
				{
					values.push_back({ LabParameterId("radius"), 2.0f });
				}
				checks.Check(parameters.RequestParameterBatch(values, error) == 0 &&
					!parameters.GetParameterBatchStatus() && parameters.GetParameterRevision() == initial &&
					(!withLab || probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f),
					"Invalid host/type/enum/range items reject every source without live mutation or receipt allocation");
			}
			std::vector<LabParameterValue> values
			{
				{ LabParameterId("view.gtao.radius"), 3.0f },
				{ LabParameterId("view.exposure.ev100"), 8.0f },
				{ LabParameterId("view.exposure.compensation"), 1.0f },
				{ LabParameterId("view.taa.max_history_feedback"), 0.7f },
				{ LabParameterId("view.taa.history_rectification"), int32_t(TemporalAAHistoryRectification::BoundedVarianceClip) },
			};
			if (withLab)
			{
				values.push_back({ LabParameterId("radius"), 2.0f });
			}
			const auto id = parameters.RequestParameterBatch(values, error);
			checks.Check(id != 0 && parameters.GetParameterBatchStatus()->m_Revision == 0 &&
				parameters.GetParameterBatchStatus()->m_Items.empty(),
				"Host/mixed admission owns values without predicting application stamps");
			probe.m_SkipParameterFrame = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto* aborted = parameters.GetParameterBatchStatus();
			checks.Check(aborted && !aborted->IsTerminal() && aborted->m_Items.front().m_Published &&
				!aborted->m_Items.front().m_FirstConsumed && !parameters.TakeParameterBatchResult(id),
				"An aborted real host parameter frame retains application/publication but cannot be consumed");
			const auto revision = aborted->m_Revision;
			GGLAB_UNUSED(runtime.Tick());
			const auto* done = parameters.GetParameterBatchStatus();
			checks.Check(done && done->IsTerminal() && done->m_Revision == revision &&
				std::ranges::all_of(done->m_Items, [](const auto& item)
				{
					return item.m_State == ParameterEffectState::Consumed &&
						item.m_Identity.m_Revision != 0 && item.m_Published == item.m_Applied &&
						item.m_FirstConsumed > item.m_Published;
				}),
				"A successful retry consumes the frozen host/mixed revision without applying a second time");
			const auto& view = probe.m_ParameterBuiltView;
			const auto expectedExposure = ResolveManualExposureSettings(8.0f, 1.0f,
				runtime.GetDemoManager()->GetActiveDemo()->GetViewRenderProfile().m_EnableScenePreExposure);
			checks.Check(view.m_Lighting.m_GTAO.m_Radius == 3.0f &&
				view.m_TemporalAA.m_MaxHistoryFeedback == 0.7f &&
				view.m_TemporalAA.m_HistoryRectification == TemporalAAHistoryRectification::BoundedVarianceClip &&
				view.m_Exposure.m_EffectiveEV100 == 7.0f &&
				view.m_Exposure.m_PreExposure == expectedExposure.m_PreExposure &&
				(!withLab || probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f),
				"The production frame uses host overrides after Lab authoring, with coherent manual exposure and pre-exposure");
			checks.Check(parameters.RequestParameterBatch(values, error) == 0 &&
				!parameters.TakeParameterBatchResult(id + 1) && parameters.TakeParameterBatchResult(id).has_value(),
				"Every source shares one retained result slot and only the correct terminal id releases it");
			const auto nochange = parameters.RequestParameterBatch(values, error);
			GGLAB_UNUSED(runtime.Tick());
			const auto same = parameters.TakeParameterBatchResult(nochange);
			checks.Check(same && same->m_Revision == revision &&
				std::ranges::all_of(same->m_Items, [](const auto& item) { return item.m_State == ParameterEffectState::NoChange; }),
				"Repeating a host/mixed batch preserves its revision and introduces no effects");
			values.front().m_Value = 4.0f;
			const auto stale = parameters.RequestParameterBatch(values, error);
			if (withLab)
			{
				probe.m_ParameterRuntime->OnExit();
				probe.m_ParameterRuntime->RequestRestartSession();
				probe.m_ParameterRuntime->ProcessPendingCommands();
				probe.m_ParameterRuntime->OnEnter();
				GGLAB_UNUSED(runtime.Tick());
				const auto replaced = parameters.TakeParameterBatchResult(stale);
				checks.Check(replaced && replaced->m_State == ParameterBatchState::Failed &&
					replaced->m_Items.empty() && probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius != 4.0f,
					"Replacing the Lab session clears host overrides and never applies a stale cross-source batch");
			}
			else
			{
				probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
					parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Failed &&
					!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed,
					"Fatal host-only submission fails its effects without fabricated consumption");
			}
			runtime.Shutdown();
		}

		void RunParameterFrameScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool fatalSubmission)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = true;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& lab = *probe.m_ParameterRuntime;
			auto& parameters = *runtime.GetParameterControl();
			probe.m_ParameterControl = &parameters;
			std::string error;
			const auto request = [&](float radius)
				{
					const std::array values{ LabParameterValue{ LabParameterId("radius"), radius } };
					return parameters.RequestParameterBatch(values, error);
				};
			const auto applications = probe.m_ParameterApplications;
			for (const auto& invalid : std::array{
				LabParameterValue{ LabParameterId("unknown"), true },
				LabParameterValue{ LabParameterId("radius"), int32_t(2) },
				LabParameterValue{ LabParameterId("radius"), 5.0f },
				LabParameterValue{ LabParameterId("layout"), uint32_t(2) },
				LabParameterValue{ LabParameterId("deferred"), true } })
			{
				const std::array values{ LabParameterValue{ LabParameterId("radius"), 2.0f }, invalid };
				checks.Check(parameters.RequestParameterBatch(values, error) == 0 && !error.empty() &&
					probe.m_ParameterApplications == applications && !parameters.GetParameterBatchStatus(),
					"Production Lab admission rejects a complete invalid/unsupported batch without mutation or ownership");
			}
			const auto id = request(2.0f);
			checks.Check(id != 0 && error.empty() && request(3.0f) == 0 &&
				probe.m_ParameterApplications == applications &&
				lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f,
				"One accepted Lab transaction survives Update-time command draining without eager application");
			const std::array wrongOwnerValues{ LabParameterValue{ LabParameterId("radius"), 2.0f } };
			checks.Check(!lab.ApplyParameterBatch(id + 1, wrongOwnerValues, error),
				"A wrong source command id cannot consume the accepted session reservation");
			if (fatalSubmission)
			{
				probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal &&
					parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
					parameters.GetParameterBatchStatus()->m_State == ParameterBatchState::Failed &&
					!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed &&
					probe.m_ParameterApplications == applications + 1,
					"A fatal fenced production frame never confirms parameter consumption");
				checks.Check(parameters.TakeParameterBatchResult(id).has_value() && request(1.0f) == 0,
					"Fatal runtime failure closes the retained receipt and stops parameter intake");
				runtime.Shutdown();
				return;
			}
			checks.Check(runtime.Tick() == AppRuntimeTickResult::Continue &&
				parameters.GetParameterBatchStatus()->IsTerminal() && probe.m_ParameterBuiltRadius == 2.0f &&
				probe.m_ParameterApplications == applications + 1,
				"The real AppRuntime Ready/build/submit path applies the Lab batch to the built frame exactly once");
			const auto* status = parameters.GetParameterBatchStatus();
			const auto revision = status->m_Revision;
			const auto& item = status->m_Items.front();
			checks.Check(item.m_State == ParameterEffectState::Consumed &&
				item.m_Requested == probe.m_ParameterBuiltSerial && item.m_Applied == item.m_Requested &&
				item.m_Published == item.m_Applied && item.m_FirstConsumed == item.m_Applied &&
				request(3.0f) == 0 && !parameters.TakeParameterBatchResult(id + 1),
				"Four Immediate stamps bind to the actual submitted serial and the owner retains its result until consumption");
			checks.Check(parameters.TakeParameterBatchResult(id).has_value() && !parameters.TakeParameterBatchResult(id),
				"The bounded Lab result has exactly one explicit consumption");

			const auto noChange = request(2.0f);
			GGLAB_UNUSED(runtime.Tick());
			const auto unchanged = parameters.TakeParameterBatchResult(noChange);
			checks.Check(unchanged && unchanged->m_Revision == revision &&
				unchanged->m_Items.front().m_State == ParameterEffectState::NoChange &&
				!unchanged->m_Items.front().m_Published && probe.m_ParameterApplications == applications + 1,
				"NoChange preserves the active revision and starts neither a handler call nor publication");

			const auto retry = request(3.0f);
			probe.m_SkipParameterFrame = true;
			const auto aborts = probe.m_FrameAborts;
			GGLAB_UNUSED(runtime.Tick());
			const auto published = parameters.GetParameterBatchStatus()->m_Items.front().m_Published;
			checks.Check(published && !parameters.GetParameterBatchStatus()->IsTerminal() &&
				!parameters.GetParameterBatchStatus()->m_Items.front().m_FirstConsumed &&
				probe.m_FrameAborts == aborts + 1 && probe.m_ParameterApplications == applications + 2,
				"A real RAII frame abort preserves publication without firstConsumed");
			GGLAB_UNUSED(runtime.Tick());
			const auto retried = parameters.TakeParameterBatchResult(retry);
			checks.Check(retried && retried->m_Items.front().m_Published == published &&
				retried->m_Items.front().m_FirstConsumed == *published + 1 &&
				probe.m_ParameterBuiltRadius == 3.0f && probe.m_ParameterApplications == applications + 2,
				"Only a matching successful retry consumes the state; application and publication are not repeated");

			const auto superseded = request(2.5f);
			probe.m_SkipParameterFrame = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto oldPublication = parameters.GetParameterBatchStatus()->m_Items.front().m_Published;
			lab.RequestSetParameter(LabParameterId("radius"), 1.5f);
			GGLAB_UNUSED(runtime.Tick());
			const auto replaced = parameters.TakeParameterBatchResult(superseded);
			checks.Check(replaced && replaced->m_Items.front().m_State == ParameterEffectState::Superseded &&
				replaced->m_Items.front().m_Published == oldPublication &&
				!replaced->m_Items.front().m_FirstConsumed && probe.m_ParameterBuiltRadius == 1.5f,
				"A later UI edit supersedes the aborted state instead of being mislabeled as its consumption");

			const auto frozen = request(2.0f);
			probe.m_ChangeParameterAfterBuild = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto consumed = parameters.TakeParameterBatchResult(frozen);
			checks.Check(consumed && consumed->m_Items.front().m_State == ParameterEffectState::Consumed &&
				consumed->m_Items.front().m_FirstConsumed == probe.m_ParameterBuiltSerial &&
				probe.m_ParameterBuiltRadius == 2.0f &&
				lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 3.5f,
				"Submission uses the identity frozen by scene build rather than re-reading a later live value");

			const auto unrelated = request(2.5f);
			probe.m_UnrelatedLightingChange = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto uncertain = parameters.TakeParameterBatchResult(unrelated);
			checks.Check(uncertain && uncertain->m_State == ParameterBatchState::Failed &&
				uncertain->m_Failure.find("UnexpectedLightingTransition") != std::string::npos &&
				!uncertain->m_Items.front().m_FirstConsumed,
				"An unrelated lighting transition fails conservatively without inventing parameter attribution");

			const std::array rebake{ LabParameterValue{ LabParameterId("rebake"), true } };
			const auto mismatched = parameters.RequestParameterBatch(rebake, error);
			GGLAB_UNUSED(runtime.Tick());
			const auto mismatch = parameters.TakeParameterBatchResult(mismatched);
			checks.Check(mismatch && mismatch->m_State == ParameterBatchState::Failed &&
				mismatch->m_Failure.find("EffectMismatch") != std::string::npos &&
				!mismatch->m_Items.front().m_FirstConsumed,
				"An actual Runtime lighting generation advance cannot masquerade as an Immediate effect");

			const auto queued = request(1.0f);
			lab.RequestRestartSession();
			GGLAB_UNUSED(runtime.Tick());
			const auto stale = parameters.TakeParameterBatchResult(queued);
			checks.Check(stale && stale->m_State == ParameterBatchState::Failed && stale->m_Items.empty(),
				"Replacing the Lab session rejects a queued batch before it can mutate the replacement");
			runtime.Shutdown();
		}

		void RunLightingParameterScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = true;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& lab = *probe.m_ParameterRuntime;
			auto& parameters = *runtime.GetParameterControl();
			probe.m_ParameterControl = &parameters;
			std::string error;
			const auto request = [&](uint32_t value, bool mixed = false)
				{
					std::vector<LabParameterValue> values{ { LabParameterId("lighting"), value } };
					if (mixed)
					{
						values.push_back({ LabParameterId("radius"), 2.0f });
						values.push_back({ LabParameterId("view.exposure.ev100"), 8.0f });
					}
					return parameters.RequestParameterBatch(values, error);
				};
			const auto tickUntil = [&](auto condition)
				{
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					while (!condition() && std::chrono::steady_clock::now() < deadline)
					{
						// This fault backend deliberately holds cache-readback fences. This
						// success scenario explicitly delivers completion so a later bake can start.
						probe.m_ReadbackFence = {};
						if (runtime.Tick() != AppRuntimeTickResult::Continue)
						{
							return false;
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
					return condition();
				};
			const auto& environment = *runtime.GetRenderServices().m_Environment;
			checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration != 0; }),
				"Deferred fixture establishes an actual published production bake before its experiment");
			const std::array tooMany{ LabParameterValue{ LabParameterId("lighting"), uint32_t(2) },
				LabParameterValue{ LabParameterId("broken-lighting"), uint32_t(2) } };
			checks.Check(parameters.RequestParameterBatch(tooMany, error) == 0 && !parameters.GetParameterBatchStatus(),
				"Multiple lighting bindings reject atomically before any owner handler runs");
			const auto applications = probe.m_ParameterApplications;
			const auto id = request(2, true);
			GGLAB_UNUSED(runtime.Tick());
			const auto first = *parameters.GetParameterBatchStatus();
			checks.Check(id != 0 && first.m_Items.size() == 3 && !first.IsTerminal() &&
				first.m_Items[0].m_State == ParameterEffectState::Applied && !first.m_Items[0].m_Published &&
				first.m_Items[1].m_State == ParameterEffectState::Consumed &&
				first.m_Items[2].m_State == ParameterEffectState::Consumed &&
				first.m_Items[0].m_Identity.m_Domain == "lighting" &&
				first.m_Items[0].m_Identity.m_Generation == environment.GetBakeRequestGeneration(),
				"A mixed real frame consumes Immediate state while Deferred state keeps its own applied generation pending");
			checks.Check(!parameters.TakeParameterBatchResult(id) && request(3) == 0,
				"Pending Deferred effects retain their one result slot and cannot be consumed early");
			checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
				"The production scheduler eventually publishes the exact Deferred generation");
			const auto done = parameters.TakeParameterBatchResult(id);
			checks.Check(done && done->m_Items[0].m_State == ParameterEffectState::Consumed &&
				done->m_Items[0].m_Applied == first.m_Items[0].m_Applied &&
				done->m_Items[0].m_Published > done->m_Items[0].m_Applied &&
				done->m_Items[0].m_FirstConsumed == done->m_Items[0].m_Published &&
				probe.m_ParameterApplications == applications + 1,
				"Deferred stamps distinguish application from actual publication and successful first consumption without reapplying");
			const auto generation = environment.GetBakeRequestGeneration();
			const auto revision = parameters.GetParameterRevision();
			const auto nochange = request(2);
			GGLAB_UNUSED(runtime.Tick());
			const auto unchanged = parameters.TakeParameterBatchResult(nochange);
			checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
				!unchanged->m_Items[0].m_Published && !unchanged->m_Items[0].m_FirstConsumed &&
				parameters.GetParameterRevision() == revision && environment.GetBakeRequestGeneration() == generation &&
				probe.m_ParameterApplications == applications + 1,
				"Deferred NoChange makes no bake request, handler call, publication or revision");
			const auto replacedId = request(3);
			GGLAB_UNUSED(runtime.Tick());
			lab.RequestSetParameter(LabParameterId("lighting"), uint32_t(4));
			GGLAB_UNUSED(runtime.Tick());
			const auto replaced = parameters.TakeParameterBatchResult(replacedId);
			checks.Check(replaced && replaced->m_Items[0].m_State == ParameterEffectState::Superseded &&
				!replaced->m_Items[0].m_Published && !replaced->m_Items[0].m_FirstConsumed,
				"A later UI lighting request supersedes an unpublished exact generation without consuming its replacement");
			const auto retryId = request(5);
			probe.m_SkipLightingPublication = true;
			checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
				parameters.GetParameterBatchStatus()->m_Items[0].m_Published.has_value(); }),
				"A publication can be observed in a frame subsequently aborted by the real RAII path");
			const auto aborted = *parameters.GetParameterBatchStatus();
			checks.Check(!aborted.IsTerminal() && !aborted.m_Items[0].m_FirstConsumed,
				"An aborted publication frame is never Deferred firstConsumed");
			parameters.OnFrameSubmitted({ .m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed,
				.m_SubmittedFence = probe.m_LastEndFence,
				.m_RenderFrameSerial = *aborted.m_Items[0].m_Published });
			checks.Check(!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed &&
				parameters.GetParameterBatchStatus()->m_Items[0].m_Published == aborted.m_Items[0].m_Published,
				"A failed scene upload cannot consume a lighting publication even with a valid fence and matching serial");
			probe.m_RebakeAfterParameterBuild = true;
			GGLAB_UNUSED(runtime.Tick());
			const auto retried = parameters.TakeParameterBatchResult(retryId);
			checks.Check(retried && retried->m_Items[0].m_State == ParameterEffectState::Consumed &&
				retried->m_Items[0].m_Published == aborted.m_Items[0].m_Published &&
				retried->m_Items[0].m_FirstConsumed > retried->m_Items[0].m_Published &&
				environment.GetBakeRequestGeneration() != retried->m_Items[0].m_Identity.m_Generation,
				"Retry consumes the frozen built lighting generation even when a later live request advances after build");
			const auto supersededAfterPublication = request(6);
			probe.m_SkipLightingPublication = true;
			checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->m_Items.size() == 1 &&
				parameters.GetParameterBatchStatus()->m_Items[0].m_Published.has_value(); }),
				"Deferred publication is retained before a later UI replacement");
			const auto oldPublished = parameters.GetParameterBatchStatus()->m_Items[0].m_Published;
			lab.RequestSetParameter(LabParameterId("lighting"), uint32_t(7));
			GGLAB_UNUSED(runtime.Tick());
			const auto overwritten = parameters.TakeParameterBatchResult(supersededAfterPublication);
			checks.Check(overwritten && overwritten->m_Items[0].m_State == ParameterEffectState::Superseded &&
				overwritten->m_Items[0].m_Published == oldPublished && !overwritten->m_Items[0].m_FirstConsumed,
				"A published but aborted Deferred state keeps publication evidence when superseded before consumption");
			const std::array broken{ LabParameterValue{ LabParameterId("broken-lighting"), uint32_t(2) },
				LabParameterValue{ LabParameterId("radius"), 3.0f } };
			const auto brokenId = parameters.RequestParameterBatch(broken, error);
			GGLAB_UNUSED(runtime.Tick());
			const auto failed = parameters.TakeParameterBatchResult(brokenId);
			checks.Check(failed && failed->m_State == ParameterBatchState::Failed &&
				std::ranges::all_of(failed->m_Items, [](const auto& item) { return item.IsTerminal(); }) &&
				failed->m_Failure.find("LightingEffectUnproven") != std::string::npos,
				"Unproven domain attribution closes every pending sibling instead of returning a terminal batch with unfinished items");
			const auto fatalId = request(8);
			probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
			checks.Check(fatalId != 0 && runtime.Tick() == AppRuntimeTickResult::Fatal &&
				parameters.GetParameterBatchStatus()->IsTerminal() && !parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed,
				"Fatal submission closes an applied Deferred wait without inventing publication or consumption");
			runtime.Shutdown();
		}

		void RunContentBindingScenario(SelfTestContext& checks, const std::filesystem::path& root,
			bool failBeforeShutdown)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = true;
			probe.m_EnableLightingProfiles = true;
			// The Demo owns the nested content identity; it need not equal the raw Lab serial.
			probe.m_ContentSerialOffset = 100;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			// Retain this borrow: subsequent owner transitions must not need a fresh getter.
			auto& parameters = *runtime.GetParameterControl();
			parameters.Enable();
			probe.m_ParameterControl = &parameters;
			auto& demos = *runtime.GetDemoManager();
			checks.Check(parameters.GetContentIdentity() == demos.GetActiveContentIdentity() &&
				probe.m_PlannedContentIdentity == parameters.GetContentIdentity(),
				"Parameter binding and frame planning share the Demo owner's nested content identity");
			const std::array overrideValues
			{
				LabParameterValue{ LabParameterId("view.gtao.radius"), 3.0f },
				LabParameterValue{ LabParameterId("radius"), 2.0f },
			};
			std::string error;
			const auto initial = parameters.RequestParameterBatch(overrideValues, error);
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(initial != 0 && parameters.TakeParameterBatchResult(initial).has_value() &&
				probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 3.0f,
				"The binding fixture establishes an explicit Host override before content replacement");
			const std::array pendingValues
			{
				LabParameterValue{ LabParameterId("view.gtao.radius"), 4.0f },
				LabParameterValue{ LabParameterId("radius"), 2.5f },
			};
			const auto oldIdentity = parameters.GetContentIdentity();
			const auto staleLab = parameters.RequestParameterBatch(pendingValues, error);
			probe.m_ParameterRuntime->RequestRestartSession();
			probe.m_FrameBeginUnavailable = true;
			const auto readyFrames = probe.m_FrameBegins;
			GGLAB_UNUSED(runtime.Tick());
			const auto labResult = parameters.TakeParameterBatchResult(staleLab);
			checks.Check(staleLab != 0 && labResult && labResult->IsTerminal() && labResult->m_Items.empty() &&
				parameters.GetContentIdentity() != oldIdentity &&
				parameters.GetContentIdentity() == demos.GetActiveContentIdentity() && probe.m_FrameBegins == readyFrames &&
				parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f,
				"Lab replacement cancels the stale batch and clears Host overrides during an Unavailable frame");
			auto* session = probe.m_ParameterRuntime->GetActiveSession();
			GGLAB_UNUSED(session->SetParameter(LabParameterId("radius"), 2.75f));
			session->ApplyParameterChanges(LabChangeImpact::Immediate);
			const auto beforeQuery = parameters.GetParameterRevision();
			checks.Check(runtime.GetParameterControl() == &parameters &&
				static_cast<const GGLabAppRuntime&>(runtime).GetParameterControl() == &parameters &&
				parameters.GetParameterRevision() == beforeQuery &&
				parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.0f,
				"The parameter getter is a pure const accessor, without settings refresh or revision mutation");
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(parameters.GetParameterRevision() > beforeQuery &&
				parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 2.75f,
				"The explicit content update stage refreshes inherited settings even without RHI readiness");
			probe.m_FrameBeginUnavailable = false;
			const auto restoredOverride = parameters.RequestParameterBatch(overrideValues, error);
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(restoredOverride != 0 && parameters.TakeParameterBatchResult(restoredOverride).has_value() &&
				probe.m_PlannedContentIdentity == parameters.GetContentIdentity(),
				"The next Ready frame plans with the same refreshed identity as parameter control");
			const auto pendingDemo = parameters.RequestParameterBatch(pendingValues, error);
			const auto oldDestructions = probe.m_DemoDestructions;
			const auto oldFrames = probe.m_FrameBegins;
			probe.m_ExpectedReleasedBatch = pendingDemo;
			probe.m_FrameBeginUnavailable = true;
			probe.m_HoldCaptureFence = true;
			demos.RequestActiveDemo(1);
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(pendingDemo != 0 && probe.m_BindingReleasedBeforeExit &&
				demos.GetRetiringDemoCount() == 1 && probe.m_DemoDestructions == oldDestructions &&
				parameters.GetHostParameters().Get(LabParameterId("view.gtao.radius"), 0.0f) == 1.0f &&
				parameters.GetContentIdentity() == demos.GetActiveContentIdentity(),
				"Demo ownership cancels the old reservation and releases bindings before OnExit, independent of queries");
			GGLAB_UNUSED(runtime.Tick());
			probe.m_HoldCaptureFence = false;
			probe.m_ReadbackFence = {};
			GGLAB_UNUSED(runtime.Tick());
			checks.Check(demos.GetRetiringDemoCount() == 0 && probe.m_DemoDestructions == oldDestructions + 1 &&
				probe.m_FrameBegins == oldFrames && parameters.TakeParameterBatchResult(pendingDemo).has_value(),
				"The old Demo is destroyed behind its completed fence during repeated Unavailable frames without a control getter");
			const auto shutdownBatch = parameters.RequestParameterBatch(pendingValues, error);
			checks.Check(shutdownBatch != 0, "The current content accepts a new batch before teardown");
			if (failBeforeShutdown)
			{
				probe.m_FrameBeginUnavailable = false;
				probe.m_FrameEndInjection = FrameEndInjection::FatalFenced;
				checks.Check(runtime.Tick() == AppRuntimeTickResult::Fatal,
					"Runtime failure terminates the new binding after the previous owner has been destroyed");
			}
			runtime.Shutdown();
			const auto finished = parameters.TakeParameterBatchResult(shutdownBatch);
			checks.Check(finished && finished->IsTerminal() &&
				std::ranges::none_of(finished->m_Items, [](const auto& item) { return item.m_FirstConsumed.has_value(); }) &&
				parameters.GetContentIdentity() == 0 && !parameters.GetLabParameters() &&
				parameters.GetHostParameters().CaptureValues().empty(),
				"Shutdown clears content borrows and overrides without revisiting the destroyed owner or inventing consumption");
		}

		void RunBatchPreparationScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = true;
			probe.m_EnableLightingProfiles = true;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& parameters = *runtime.GetParameterControl();
			parameters.Enable();
			probe.m_ParameterControl = &parameters;
			auto& lab = *probe.m_ParameterRuntime;
			auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
			auto& lighting = *runtime.GetRenderServices().m_WorldLighting;
			const auto& environment = *runtime.GetRenderServices().m_Environment;
			const auto source = lighting.GetWorldLightingPublication().m_Requested;
			std::optional<entt::entity> sun;
			for (const auto entity : world.GetRegistry().view<components::LightComponent>())
			{
				if (world.GetRegistry().get<components::LightComponent>(entity).m_WorldSun)
				{
					sun = entity;
					break;
				}
			}
			checks.Check(source && sun && parameters.GetLightingProfiles().size() == 2,
				"Batch preparation fixture binds real Lab, Host and World Lighting sources");
			if (!source || !sun)
			{
				runtime.Shutdown();
				return;
			}
			const std::array values
			{
				LabParameterValue{ LabParameterId("radius"), 2.0f },
				LabParameterValue{ LabParameterId("environment.profile"), int32_t(1) },
				LabParameterValue{ LabParameterId("view.exposure.ev100"), 11.0f },
				LabParameterValue{ LabParameterId("view.gtao.radius"), 1.0f },
			};
			std::string error;
			const auto initialExposure = parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f);
			const auto applications = probe.m_ParameterApplications;
			const auto labRevision = lab.GetParameterRevision();
			const auto unchanged = [&]()
				{
					return probe.m_ParameterApplications == applications && lab.GetParameterRevision() == labRevision &&
						lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 1.0f &&
						parameters.GetHostParameters().Get(LabParameterId("view.exposure.ev100"), 0.0f) == initialExposure &&
						probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == initialExposure;
				};

			const auto cancelled = parameters.RequestParameterBatch(values, error);
			const auto cancelledRevision = parameters.GetParameterRevision();
			const auto cancelledGeneration = environment.GetBakeRequestGeneration();
			const std::array labValues{ LabParameterValue{ LabParameterId("radius"), 2.0f } };
			auto labPlan = lab.PrepareParameterBatch(cancelled, labValues, error);
			checks.Check(labPlan && labPlan->HasChanges() && unchanged() &&
				environment.GetBakeRequestGeneration() == cancelledGeneration,
				"Lab preparation changes neither live values, handler counts nor Runtime requests");
			labPlan.reset();
			labPlan = lab.PrepareParameterBatch(cancelled, labValues, error);
			checks.Check(labPlan.has_value() && unchanged(),
				"Discarding a Lab plan leaves the same command reservation available for preparation");
			labPlan.reset();
			lab.CancelParameterBatch(cancelled);
			GGLAB_UNUSED(runtime.Tick());
			const auto lostReservation = parameters.TakeParameterBatchResult(cancelled);
			checks.Check(cancelled != 0 && lostReservation && lostReservation->m_State == ParameterBatchState::Failed &&
				lostReservation->m_Items.empty() && lostReservation->m_Revision == 0 && unchanged() &&
				parameters.GetParameterRevision() == cancelledRevision &&
				environment.GetBakeRequestGeneration() == cancelledGeneration,
				std::format("A lost Lab reservation rejects preparation before mutation "
					"(id={}, failure={}, unchanged={}, revision={}/{}, generation={}/{})",
					cancelled, lostReservation ? lostReservation->m_Failure : error, unchanged(),
					parameters.GetParameterRevision(), cancelledRevision,
					environment.GetBakeRequestGeneration(), cancelledGeneration));

			const auto missing = parameters.RequestParameterBatch(values, error);
			auto& light = world.GetRegistry().get<components::LightComponent>(*sun);
			const auto originalSun = light.m_WorldSun;
			light.m_WorldSun.reset();
			// This is an external authored edit, not part of the accepted batch.
			lighting.SynchronizeWorldLighting(world, source->m_SessionIdentity);
			parameters.Enable();
			const auto missingRevision = parameters.GetParameterRevision();
			const auto missingGeneration = environment.GetBakeRequestGeneration();
			GGLAB_UNUSED(runtime.Tick());
			const auto missingTarget = parameters.TakeParameterBatchResult(missing);
			checks.Check(missing != 0 && missingTarget && missingTarget->m_State == ParameterBatchState::Failed &&
				missingTarget->m_Failure.starts_with("LightingPreparationFailed:") &&
				missingTarget->m_Items.empty() && missingTarget->m_Revision == 0 && unchanged() &&
				parameters.GetParameterRevision() == missingRevision &&
				environment.GetBakeRequestGeneration() == missingGeneration,
				"A sun lost after admission rejects the entire prepared batch without applying exposure or calling the Lab handler");

			light.m_WorldSun = originalSun;
			lighting.SynchronizeWorldLighting(world, source->m_SessionIdentity);
			parameters.Enable();
			const auto revision = parameters.GetParameterRevision();
			probe.m_ExpectedCommitGeneration = environment.GetBakeRequestGeneration() + 1;
			probe.m_ObserveParameterCommit = true;
			const auto accepted = parameters.RequestParameterBatch(values, error);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (accepted != 0 && !parameters.GetParameterBatchStatus()->IsTerminal() &&
				std::chrono::steady_clock::now() < deadline)
			{
				probe.m_ReadbackFence = {};
				if (runtime.Tick() != AppRuntimeTickResult::Continue)
				{
					break;
				}
			}
			const auto committed = parameters.TakeParameterBatchResult(accepted);
			checks.Check(committed && committed->m_State == ParameterBatchState::Finished &&
				probe.m_ParameterCommitWasComplete && probe.m_ParameterApplications == applications + 1 &&
				parameters.GetParameterRevision() == revision + 1 && lab.GetParameterRevision() == labRevision + 1,
				std::format("A successful mixed batch installs all sources before effects "
					"(id={}, failure={}, observed={}, applications={}/{}, revision={}/{}, labRevision={}/{})",
					accepted, committed ? committed->m_Failure : error, probe.m_ParameterCommitWasComplete,
					probe.m_ParameterApplications, applications + 1, parameters.GetParameterRevision(), revision + 1,
					lab.GetParameterRevision(), labRevision + 1));
			checks.Check(committed && committed->m_Items.size() == 4 &&
				std::ranges::all_of(committed->m_Items, [](const auto& item) { return item.m_State == ParameterEffectState::Consumed; }) &&
				committed->m_Items[1].m_Identity.m_Generation == probe.m_ExpectedCommitGeneration &&
				probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 11.0f &&
				probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius == 1.0f &&
				lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f) == 2.0f,
				std::format("Host precedence survives an inherited NoChange becoming changed "
					"(hostRadius={}, labRadius={}, exposure={}, items={})",
					probe.m_ParameterBuiltView.m_Lighting.m_GTAO.m_Radius,
					lab.GetActiveSession()->GetParameters().Get(LabParameterId("radius"), 0.0f),
					probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100, committed ? committed->m_Items.size() : 0));
			runtime.Shutdown();
		}

		void RunLightingProfileRegistrationScenario(SelfTestContext& checks,
			const std::filesystem::path& root, bool labBoolean, bool equivalent)
		{
			LifetimeProbe probe;
			probe.m_EnableParameterLab = labBoolean;
			probe.m_ProfileNamedLabBool = labBoolean;
			probe.m_EnableLightingProfiles = !labBoolean;
			probe.m_InvalidLightingProfiles = !labBoolean && !equivalent;
			probe.m_EquivalentLightingProfiles = equivalent;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& parameters = *runtime.GetParameterControl();
			parameters.Enable();
			std::string error;
			if (labBoolean)
			{
				const std::array values{ LabParameterValue{ LabParameterId("environment.profile"), true } };
				const auto id = parameters.RequestParameterBatch(values, error);
				GGLAB_UNUSED(runtime.Tick());
				const auto result = parameters.TakeParameterBatchResult(id);
				checks.Check(result && result->m_State == ParameterBatchState::Finished &&
					probe.m_ParameterRuntime->GetActiveSession()->GetParameters().Get(
						LabParameterId("environment.profile"), false),
					"A Lab-owned environment.profile boolean dispatches through its source, not the Host enum variant");
			}
			else
			{
				checks.Check(parameters.IsAvailable() && parameters.GetLightingProfiles().empty() &&
					parameters.GetLightingProfileError().starts_with("LightingProfileRegistration:"),
					"Invalid or effective-equivalent registrations report a configuration error without disabling Host controls");
				const auto queries = probe.m_LightingProfileQueries;
				const std::array invalid{ LabParameterValue{ LabParameterId("environment.profile"), int32_t(0) } };
				checks.Check(parameters.RequestParameterBatch(invalid, error) == 0 &&
					error.starts_with("LightingProfileRegistration:"),
					"Selecting an invalid registration reports its concrete error instead of source unreadiness");
				const std::array host{ LabParameterValue{ LabParameterId("view.gtao.radius"), 2.0f } };
				const auto id = parameters.RequestParameterBatch(host, error);
				GGLAB_UNUSED(runtime.Tick());
				const auto result = parameters.TakeParameterBatchResult(id);
				checks.Check(result && result->m_State == ParameterBatchState::Finished &&
					probe.m_LightingProfileQueries == queries,
					"Unrelated Host controls remain usable and invalid registrations are cached for the content lifetime");
			}
			runtime.Shutdown();
		}

		void RunLightingProfileScenario(SelfTestContext& checks, const std::filesystem::path& root)
		{
			LifetimeProbe probe;
			probe.m_EnableLightingProfiles = true;
			FaultFactory factory(probe);
			ApplicationInput input;
			GGLabAppRuntime runtime;
			if (!InitializeOutcomeRuntime(checks, runtime, factory, input, probe, root))
			{
				return;
			}
			auto& parameters = *runtime.GetParameterControl();
			probe.m_ParameterControl = &parameters;
			parameters.Enable();
			const auto& environment = *runtime.GetRenderServices().m_Environment;
			const auto tickUntil = [&](auto condition)
				{
					const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					while (!condition() && std::chrono::steady_clock::now() < deadline)
					{
						probe.m_ReadbackFence = {};
						if (runtime.Tick() != AppRuntimeTickResult::Continue)
						{
							return false;
						}
					}
					return condition();
				};
			checks.Check(tickUntil([&]() { return environment.GetBakingStatus().m_ActiveGeneration != 0; }),
				"Profile fixture publishes its real initial Physical Sky generation");
			checks.Check(parameters.GetLightingProfiles().size() == 2 &&
				parameters.GetHostParameters().Find(LabParameterId("environment.profile")) != nullptr,
				"Only active content registers the bounded named profile source");
			std::string error;
			const auto request = [&](int32_t profile)
				{
					const std::array values{ LabParameterValue{ LabParameterId("environment.profile"), profile },
						LabParameterValue{ LabParameterId("view.exposure.ev100"), 8.0f } };
					return parameters.RequestParameterBatch(values, error);
				};
			const auto initialRevision = parameters.GetParameterRevision();
			checks.Check(request(-1) == 0 && request(99) == 0 &&
				parameters.GetParameterRevision() == initialRevision && !parameters.GetParameterBatchStatus(),
				"Read-only custom and unknown profile enums reject the complete mixed batch");
			const auto id = request(1);
			GGLAB_UNUSED(runtime.Tick());
			const auto first = *parameters.GetParameterBatchStatus();
			const auto state = parameters.GetLightingProfilePublication();
			checks.Check(id != 0 && first.m_Items[0].m_State == ParameterEffectState::Applied &&
				first.m_Items[0].m_Identity.m_Generation == environment.GetBakeRequestGeneration() &&
				state.m_Requested && state.m_Requested->m_Id == "test.oblique" &&
				state.m_Active && state.m_Active->m_Id == "test.baseline" &&
				state.m_Requested->m_Generation != state.m_Active->m_Generation,
				"A real authored sun transition retains separate requested and active profile identities");
			checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }),
				"Profile publication reaches its exact submitted generation");
			const auto done = parameters.TakeParameterBatchResult(id);
			checks.Check(done && done->m_Items[0].m_State == ParameterEffectState::Consumed &&
				done->m_Items[0].m_Published > done->m_Items[0].m_Applied &&
				done->m_Items[0].m_FirstConsumed == done->m_Items[0].m_Published &&
				probe.m_ParameterBuiltView.m_Exposure.m_ManualEV100 == 8.0f,
				"Profile receipts use actual publication/submission and never apply recommended exposure");
			const auto generation = environment.GetBakeRequestGeneration();
			const auto revision = parameters.GetParameterRevision();
			const auto same = request(1);
			GGLAB_UNUSED(runtime.Tick());
			const auto unchanged = parameters.TakeParameterBatchResult(same);
			checks.Check(unchanged && unchanged->m_Items[0].m_State == ParameterEffectState::NoChange &&
				parameters.GetParameterRevision() == revision && environment.GetBakeRequestGeneration() == generation,
				"Current profile NoChange causes no new request or revision");
			const auto queries = probe.m_LightingProfileQueries;
			auto& world = runtime.GetDemoManager()->GetActiveDemo()->GetWorld();
			for (const auto entity : world.GetRegistry().view<components::LightComponent>())
			{
				auto& light = world.GetRegistry().get<components::LightComponent>(entity);
				if (light.m_WorldSun)
				{
					auto& rotation = world.GetRegistry().get<components::TransformComponent>(entity).m_Rotation;
					rotation = Quaternion(-rotation.m_X, -rotation.m_Y, -rotation.m_Z, -rotation.m_W);
					light.m_WorldSun->m_Chromaticity *= 0.5f;
				}
			}
			const auto equivalent = request(1);
			GGLAB_UNUSED(runtime.Tick());
			const auto equivalentResult = parameters.TakeParameterBatchResult(equivalent);
			checks.Check(equivalentResult && equivalentResult->m_Items[0].m_State == ParameterEffectState::NoChange &&
				environment.GetBakeRequestGeneration() == generation &&
				parameters.GetLightingProfilePublication().m_Requested->m_Generation == generation &&
				probe.m_LightingProfileQueries == queries,
				"Equivalent Quaternion/chromaticity authoring stays NoChange and never rebuilds static registrations or relabels a request");
			world.m_Atmosphere->m_RayleighScaleHeightMeters *= 2.0f;
			const auto retry = request(0);
			probe.m_SkipLightingPublication = true;
			checks.Check(tickUntil([&]()
				{
					const auto* status = parameters.GetParameterBatchStatus();
					return !status->m_Items.empty() && status->m_Items[0].m_Published.has_value();
				}), "Profile publication may be observed on an aborted frame");
			const auto published = parameters.GetParameterBatchStatus()->m_Items[0].m_Published;
			checks.Check(!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed &&
				parameters.GetParameterBatchStatus()->m_Items[0].m_Identity.m_Generation == generation + 2,
				"An unresolved authored edit settles before profile attribution, and aborted publication is not consumed");
			GGLAB_UNUSED(runtime.Tick());
			const auto retried = parameters.TakeParameterBatchResult(retry);
			checks.Check(retried && retried->m_Items[0].m_Published == published &&
				retried->m_Items[0].m_FirstConsumed > published,
				"Retry preserves profile application/publication and supplies only first consumption");
			const auto supersededId = request(1);
			GGLAB_UNUSED(runtime.Tick());
			GGLAB_UNUSED(ApplyWorldLightingInputsToWorld(runtime.GetDemoManager()->GetActiveDemo()->GetWorld(),
				parameters.GetLightingProfiles()[0].m_Inputs));
			GGLAB_UNUSED(runtime.Tick());
			const auto superseded = parameters.TakeParameterBatchResult(supersededId);
			checks.Check(superseded && superseded->m_Items[0].m_State == ParameterEffectState::Superseded &&
				!superseded->m_Items[0].m_FirstConsumed,
				"An unrelated authored sun edit supersedes the selected profile instead of borrowing its generation");
			const auto selected = request(1);
			checks.Check(tickUntil([&]() { return parameters.GetParameterBatchStatus()->IsTerminal(); }) &&
				parameters.TakeParameterBatchResult(selected).has_value(),
				"A profile selection completes before exercising live content replacement");
			const auto oldContent = runtime.GetDemoManager()->GetTemporalSessionSerial();
			const auto oldGeneration = environment.GetBakeRequestGeneration();
			const auto cancelled = request(0);
			runtime.GetDemoManager()->RequestActiveDemo(1);
			GGLAB_UNUSED(runtime.Tick());
			parameters.Enable();
			const auto replacedContent = parameters.TakeParameterBatchResult(cancelled);
			checks.Check(replacedContent && replacedContent->m_State == ParameterBatchState::Failed &&
				runtime.GetDemoManager()->GetTemporalSessionSerial() != oldContent &&
				parameters.GetLightingProfiles().size() == 2 &&
				environment.GetBakeRequestGeneration() > oldGeneration &&
				parameters.GetHostParameters().Get(LabParameterId("environment.profile"), int32_t(-1)) == 0,
				"A running content replacement cancels old ownership, rebuilds registrations and never retains the prior profile override");
			const auto replaced = request(1);
			GGLAB_UNUSED(runtime.Tick());
			runtime.Shutdown();
			checks.Check(parameters.GetParameterBatchStatus()->m_BatchId == replaced &&
				parameters.GetParameterBatchStatus()->IsTerminal() &&
				!parameters.GetParameterBatchStatus()->m_Items[0].m_FirstConsumed,
				"Shutdown closes profile ownership without fabricating consumption");
			parameters.SynchronizeContent(nullptr, 999, "replacement", false);
			checks.Check(parameters.GetLightingProfiles().empty() &&
				!parameters.GetLightingProfilePublication().m_Requested &&
				!parameters.GetLightingProfilePublication().m_Active,
				"Content replacement clears profile registrations and both publication identities");
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
			const auto capture = runtime.TrySubmitCaptureJob({
				.m_RequiredProducts = { ImageCaptureProduct{} } }).m_JobId;
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
			const auto result = runtime.TakeCaptureJobResult(capture);
			checks.Check(result && result->m_JobId == capture &&
				result->m_Status == CaptureJobStatus::Failed && result->m_Products.front().m_Files.empty(),
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
		RunHostParameterScenario(context, root / "host-demo", false);
		RunHostParameterScenario(context, root / "host-lab", true);
		RunLightingParameterScenario(context, root / "lighting-parameters");
		RunBatchPreparationScenario(context, root / "batch-preparation");
		RunContentBindingScenario(context, root / "content-binding-shutdown", false);
		RunContentBindingScenario(context, root / "content-binding-failure", true);
		RunLightingProfileRegistrationScenario(context, root / "profile-source-binding", true, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-invalid-registration", false, false);
		RunLightingProfileRegistrationScenario(context, root / "profile-equivalent-registration", false, true);
		RunLightingProfileScenario(context, root / "lighting-profiles");
		RunParameterFrameScenario(context, root / "parameter-frames", false);
		RunParameterFrameScenario(context, root / "parameter-failed-submit", true);
		RunFrameOutcomeScenarios(context, root / "outcomes");
		RunOwnedJobRoutingScenario(context, root / "owned-job-routing");
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-fenced", FrameEndInjection::FatalFenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-failed-unfenced", FrameEndInjection::FatalUnfenced, false);
		RunOwnedJobFailureScenario(context, root / "owned-job-unknown", FrameEndInjection::Completed, true);
		RunOwnedWriterDrainScenario(context, root / "owned-writer-drain");
		RunAdmittedJobScenario(context, root / "admitted-job", false);
		RunAdmittedJobScenario(context, root / "admitted-job-missing", true);
		RunAdmittedJobScenario(context, root / "admitted-job-multiple-images", false, true);
		RunJobCancellationScenario(context, root / "cancel-before-submit", true);
		RunJobCancellationScenario(context, root / "cancel-shared-submission", false);
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
