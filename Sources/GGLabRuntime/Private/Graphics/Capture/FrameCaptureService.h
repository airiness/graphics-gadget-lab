#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureAccess.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"

#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class RHIDevice;
	class RenderGraph;
	class RenderShaderProgramAccess;
	class RenderPipelineResolver;
	class RHIPipelineSystem;

	struct FrameCaptureLimits
	{
		uint32_t m_MaxRequests = 64;
		// A payload slot spans GPU readback through the last CPU payload consumer.
		uint32_t m_MaxPayloads = 8;
		// Includes pitched readback and reserved tight CPU rows during the copy.
		uint64_t m_MaxPayloadBytes = 256ull * 1024 * 1024;
	};

	// Owns capture requests, per-frame readback buffers and finished results. The
	// render host drives the frame lifecycle: a tap is bound while the frame graph
	// is built, adopts the frame's submission fence, and keeps its readback buffer
	// until that fence completes and the rows have been copied to CPU memory.
	class FrameCaptureService final : public FrameCaptureControlBase,
		public RenderFrameCaptureAccess
	{
	public:
		explicit FrameCaptureService(RHIDevice& device, FrameCaptureLimits limits = {}) noexcept;
		~FrameCaptureService() override;
		GGLAB_DELETE_COPYABLE_MOVABLE(FrameCaptureService);

		[[nodiscard]] uint64_t RequestCapture(FrameCaptureSource source,
			uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override;
		[[nodiscard]] uint64_t RequestDiagnosticCapture(PostProcessDebugTap tap,
			uint64_t frameSerial = 0, uint64_t groupId = 0) noexcept override;
		[[nodiscard]] uint64_t RequestResourceExport(std::vector<RGResourceAddress> targets,
			uint64_t frameSerial = 0, bool deferFrameOnBusy = false, bool includeGraph = false, uint64_t groupId = 0) noexcept override;
		[[nodiscard]] uint64_t RequestNonFiniteScan(std::vector<NonFiniteScanTarget> targets,
			uint64_t configurationVersion, uint64_t frameSerial = 0, bool includeGraph = false, uint64_t groupId = 0) noexcept override;
		bool EnableNonFiniteProduction() noexcept
		{
			if (m_Limits.m_MaxPayloads < 2 || ProductionPayloadBytes + ProductionCpuBytes > m_Limits.m_MaxPayloadBytes)
			{
				return false;
			}
			m_ProductionEnabled = true;
			return true;
		}
		void ConsumeNonFiniteProductionFrames(std::vector<std::shared_ptr<const NonFiniteProductionFrame>>& frames) noexcept override;
		bool HasNonFiniteInstrumentation() const noexcept override { return m_ProductionEnabled; }
		uint32_t GetNonFiniteScanTargetLimit() const noexcept override
		{
			const uint32_t used = GetPayloadUsage().second;
			const uint32_t reserved = m_ProductionEnabled ? 1u : 0u;
			const uint32_t ceiling = m_Limits.m_MaxPayloads - (m_ProductionEnabled ? 2u : 0u);
			return used > m_Limits.m_MaxPayloads - reserved ? 0u
				: std::min(ceiling, m_Limits.m_MaxPayloads - reserved - used);
		}
		std::shared_ptr<const NonFiniteProductionFrame> GetLatestNonFiniteProductionFrame() const noexcept override { return m_LatestProduction; }
		// Borrows stable renderer services; programs and pipelines are demanded only
		// when a scan or production frame is admitted. Services outlive this service.
		void ConfigureNonFiniteScans(RenderShaderProgramAccess& shaders,
			RenderPipelineResolver& resolver, RHIPipelineSystem& pipelines) noexcept;
		[[nodiscard]] FrameCaptureAvailability PrepareResourceExports(uint64_t frameSerial,
			RenderGraph& graph, ResourceExportFrameContext context = {}) noexcept override;
		void ConsumeResults(std::vector<FrameCaptureResult>& outResults) noexcept override;
		[[nodiscard]] uint32_t GetUnfinishedRequestCount() const noexcept override;
		[[nodiscard]] FrameCaptureAvailability GetRequestAvailability(uint32_t requestCount = 1) const noexcept override;
		[[nodiscard]] FrameCaptureAvailability GetResourceExportFrameAvailability() const noexcept override;
		[[nodiscard]] FrameCaptureAvailability GetAvailability(
			RHIFormat format, uint32_t width, uint32_t height) const noexcept override;

		[[nodiscard]] bool HasPendingRequests(FrameCaptureSource source) const noexcept override;
		[[nodiscard]] std::optional<FrameCaptureTapTarget> BindTap(uint64_t frameSerial,
			FrameCaptureSource source, const RHITextureDesc& displayTargetDesc) noexcept override;
		[[nodiscard]] std::optional<PostProcessDebugTap> GetPendingDiagnosticTap()
			const noexcept override;
		void FailPendingDiagnosticRequests(std::string_view failure) noexcept override;

		// Reuses request admission and fenced completion without adding graph nodes.
		void BeginGraphRecording(uint64_t frameSerial, RenderGraph& graph) noexcept;
		void EndGraphRecording(uint64_t frameSerial, RenderGraph& graph) noexcept;

		// The frame's GPU work, including its tap copies, was submitted.
		void OnFrameSubmitted(uint64_t frameSerial, const RHIFencePoint& fence) noexcept;
		// The frame ended without recording a submission. Its requests return to
		// the queue in their original order, unless restricted to this frame.
		void OnFrameAborted(uint64_t frameSerial) noexcept;
		// The frame's submission failed; its requests finish as Failed.
		void OnFrameSubmissionFailed(uint64_t frameSerial) noexcept;
		// Publishes every submitted tap whose fence has completed.
		void CollectCompleted() noexcept;
		// Requires the device to be idle. Publishes completed taps, fails every
		// remaining request and releases all readback buffers. Later requests fail
		// immediately.
		void Shutdown() noexcept;

	private:
		struct QueuedRequest
		{
			uint64_t m_Id = 0;
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			uint64_t m_FrameSerial = 0;
			uint64_t m_GroupId = 0;
			// Diagnostic requests only.
			PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::SceneColor;
			std::vector<RGResourceAddress> m_ResourceTargets;
			std::vector<NonFiniteScanTarget> m_ScanTargets;
			uint64_t m_ConfigurationVersion = 0;
			bool m_DeferFrameOnBusy = false;
			bool m_IncludeGraph = false;
		};

		struct ExportReadback
		{
			RGObservationCopy m_Copy;
			RHIBufferOwner m_Buffer;
			ResourceExportArray m_Array;
			uint64_t m_CpuBytes = 0;
		};

		struct ScanReadback
		{
			RGObservationScan m_Scan;
			RHIBufferOwner m_Buffer;
			RHIBufferOwner m_Components;
			NonFiniteScanBoundary m_Boundary;
			// GPU record/readback, peak CPU record copies, and the upload plus
			// two owned CPU layout tables. Metadata objects are not payload bytes.
			[[nodiscard]] uint64_t GetPayloadBytes() const noexcept
			{
				return 4 * sizeof(NonFiniteScanRecord) + 3 * m_Scan.m_ComponentBytes;
			}
		};

		struct Tap
		{
			uint64_t m_FrameSerial = 0;
			FrameCaptureSource m_Source = FrameCaptureSource::Scene;
			PostProcessDebugTap m_DiagnosticTap = PostProcessDebugTap::SceneColor;
			std::vector<QueuedRequest> m_Requests;
			RHIBufferOwner m_Buffer;
			std::shared_ptr<RGSnapshot> m_Graph;
			std::vector<ExportReadback> m_ExportReadbacks;
			std::vector<ScanReadback> m_ScanReadbacks;
			std::string m_AdapterCompatibilityIdentity;
			RHITextureCopyFootprint m_Footprint{};
			RHIFencePoint m_Fence{};
			bool m_ProductionCounters = false;
			bool m_Submitted = false;
			bool m_GraphExportActive = false;
			bool m_ResourceExportActive = false;
			bool m_NonFiniteScanActive = false;
		};

		struct RetainedImage
		{
			std::weak_ptr<const FrameCaptureImage> m_Image;
			uint64_t m_Bytes = 0;
		};
		struct RetainedExport
		{
			std::weak_ptr<const ResourceExportPayload> m_Payload;
			uint64_t m_Bytes = 0;
			uint32_t m_Slots = 0;
		};
		struct RetainedScan
		{
			std::weak_ptr<const NonFiniteScanPayload> m_Payload;
			uint64_t m_Bytes = 0;
			uint32_t m_Slots = 0;
		};
		[[nodiscard]] RGObservationFailure PrepareScanTarget(RenderGraph& graph,
			const NonFiniteScanTarget& target, ScanReadback& readback) noexcept;
		[[nodiscard]] bool AllocateScanReadback(ScanReadback& readback) noexcept;
		[[nodiscard]] RHIPipelineHandle ResolveScanPipeline(uint32_t variant) noexcept;
		void PublishScanTap(Tap& tap) noexcept;
		bool PrepareProductionCounters(uint64_t frameSerial, Tap& tap, RGProductionCounters& counters) noexcept;
		void PublishProductionTap(Tap& tap) noexcept;
		void PublishProductionFrame(std::shared_ptr<const NonFiniteProductionFrame> frame) noexcept;
		std::vector<std::shared_ptr<const NonFiniteProductionFrame>> m_ProductionResults;
		static constexpr uint64_t ProductionCpuBytes = nonfinite::SiteCount * nonfinite::ProgramCount * sizeof(NonFiniteProductionSite);
		static constexpr uint64_t ProductionPayloadBytes = nonfinite::BufferBytes * 3 + ProductionCpuBytes;
		std::vector<std::weak_ptr<const NonFiniteProductionFrame>> m_RetainedProduction;
		bool m_ProductionEnabled = false;
		ComputePipelineRecipe m_ProductionClearRecipe;
		ComputePipelineSlot m_ProductionClearSlot;
		RHIBindingLayoutHandle m_ProductionClearLayout;
		std::shared_ptr<const NonFiniteProductionFrame> m_LatestProduction;
		[[nodiscard]] std::pair<uint64_t, uint32_t> GetPayloadUsage(bool includeProduction = true) const noexcept;
		void DeferFrameCaptures(uint64_t frameSerial) noexcept override;

		[[nodiscard]] bool IsBoundBy(const QueuedRequest& request, FrameCaptureSource source,
			PostProcessDebugTap diagnosticTap) const noexcept;
		void DeferUnrelatedImages(uint64_t frameSerial) noexcept;
		void PublishFailure(uint64_t requestId, FrameCaptureSource source,
			uint64_t frameSerial, std::string failure,
			std::optional<PostProcessDebugTap> diagnosticTap = std::nullopt) noexcept;
		void FailQueuedFrameRequests(uint64_t frameSerial, std::string_view failure) noexcept;
		void FailTap(Tap& tap, uint64_t frameSerial, std::string_view failure) noexcept;
		void PublishTap(Tap& tap) noexcept;
		void PublishResourceTap(Tap& tap) noexcept;

		RHIDevice* m_Device = nullptr;
		FrameCaptureLimits m_Limits{};
		uint64_t m_NextRequestId = 1;
		std::vector<QueuedRequest> m_Queued;
		std::vector<Tap> m_Taps;
		std::vector<FrameCaptureResult> m_Results;
		std::vector<RetainedImage> m_RetainedImages;
		std::vector<RetainedExport> m_RetainedExports;
		std::vector<RetainedScan> m_RetainedScans;
		RenderShaderProgramAccess* m_ScanShaders = nullptr;
		RenderPipelineResolver* m_ScanResolver = nullptr;
		RHIPipelineSystem* m_ScanPipelineSystem = nullptr;
		RHIBindingLayoutHandle m_ScanBindingLayout;
		std::array<ComputePipelineRecipe, 3> m_ScanRecipes{};
		std::array<ComputePipelineSlot, 3> m_ScanSlots{};
		bool m_IsShutdown = false;
	};
}
