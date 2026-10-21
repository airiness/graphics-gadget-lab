#pragma once
#include "Graphics/Capture/CapturePayloadBudget.h"
#include "Graphics/Capture/FencedReadback.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace gglab
{
	class RenderGraph;
	struct RGSnapshotExecutionRecord;

	class GraphCaptureProducer
	{
	public:
		explicit GraphCaptureProducer(RenderGraph& graph) noexcept;
		GraphCaptureProducer(const GraphCaptureProducer&) = delete;
		GraphCaptureProducer& operator=(const GraphCaptureProducer&) = delete;
		void Finish(RGSnapshotExecutionRecord record) noexcept;
		[[nodiscard]] bool IsRecorded() const noexcept;
		[[nodiscard]] std::shared_ptr<const RGSnapshot> GetProduct() const noexcept;

	private:
		std::shared_ptr<RGSnapshot> m_Snapshot;
		bool m_Frozen = false;
	};

	struct ResourceCaptureReadback
	{
		RGObservationCopy m_Copy;
		ResourceExportArray m_Array;
		uint64_t m_CpuBytes = 0;
	};

	struct ResourceCaptureProducer
	{
		std::vector<ResourceCaptureReadback> m_Readbacks;
		[[nodiscard]] std::pair<uint64_t, uint32_t> GetPayloadCost() const noexcept;
		[[nodiscard]] bool Allocate(RHIDevice& device, FencedReadback& readback) noexcept;
		[[nodiscard]] std::shared_ptr<const ResourceExportPayload> ReadProduct(
			RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease) noexcept;
	};

	struct ScanCaptureReadback
	{
		RGObservationScan m_Scan;
		NonFiniteScanBoundary m_Boundary;
		[[nodiscard]] uint64_t GetPayloadBytes() const noexcept
		{
			// GPU record/readback, peak CPU records, upload and two CPU layout tables.
			return 4 * sizeof(NonFiniteScanRecord) + 3 * m_Scan.m_ComponentBytes;
		}
		[[nodiscard]] bool Allocate(RHIDevice& device, FencedReadback& readback) noexcept;
	};

	struct ScanCaptureProducer
	{
		std::vector<ScanCaptureReadback> m_Readbacks;
		[[nodiscard]] std::pair<uint64_t, uint32_t> GetPayloadCost() const noexcept;
		[[nodiscard]] std::shared_ptr<const NonFiniteScanPayload> ReadProduct(
			RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease,
			uint64_t configurationVersion) noexcept;
	};

	struct ProductionCaptureProducer
	{
		static constexpr uint64_t CpuBytes =
			nonfinite::SiteCount * nonfinite::ProgramCount * sizeof(NonFiniteProductionSite);
		static constexpr uint64_t PayloadBytes = nonfinite::BufferBytes * 3 + CpuBytes;
		// A pending frame must fit beside the retained latest report.
		static constexpr uint64_t MinimumBytes = PayloadBytes + CpuBytes;
		[[nodiscard]] static bool Allocate(RHIDevice& device, FencedReadback& readback) noexcept;
		[[nodiscard]] static std::shared_ptr<const NonFiniteProductionFrame> ReadProduct(
			RHIDevice& device, FencedReadback& readback, CapturePayloadBudget::Lease& lease,
			uint64_t frameSerial) noexcept;
	};
}
