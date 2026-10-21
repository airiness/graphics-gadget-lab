#pragma once
#include "Capture/FrameCaptureMetadata.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	// Admission policy for every accepted job; these are record counts, not byte bounds.
	struct CaptureJobLimits
	{
		uint32_t m_MaxActiveJobs = 64;
		uint32_t m_MaxReceipts = 128;
		uint32_t m_MaxProductsPerJob = 8;
		uint32_t m_MaxResourceTargetsPerJob = 64;
	};

	enum class CaptureAdmissionStatus : uint8_t
	{
		Accepted,
		Busy,
		Rejected,
	};
	enum class CaptureAdmissionReason : uint8_t
	{
		None,
		ActiveJobLimit,
		ReceiptLimit,
		InvalidPlan,
		PlanTooLarge,
		ShuttingDown,
		IdentityExhausted,
		IdentityUnavailable,
		ServicesUnavailable,
	};

	// Busy/rejected admission consumes no identity, receipt or caller-owned plan.
	struct CaptureAdmission
	{
		CaptureAdmissionStatus m_Status = CaptureAdmissionStatus::Rejected;
		CaptureAdmissionReason m_Reason = CaptureAdmissionReason::None;
		uint64_t m_JobId = 0;
	};

	enum class CaptureJobStatus : uint8_t
	{
		Completed,
		Failed,
		Cancelled,
	};
	enum class CaptureJobPublication : uint8_t
	{
		NotPublished,
		Published,
		PartiallyPublished,
		Indeterminate,
	};

	enum class CaptureProductKind : uint8_t
	{
		Image,
		Diagnostic,
		Graph,
		Resource,
	};

	struct CaptureProductResult
	{
		uint32_t m_ProductIndex = 0;
		// Plan identity survives failure/cancellation before any frame metadata exists.
		CaptureProductKind m_Kind = CaptureProductKind::Image;
		CaptureJobStatus m_Status = CaptureJobStatus::Failed;
		CaptureJobPublication m_Publication = CaptureJobPublication::NotPublished;
		std::vector<std::filesystem::path> m_Files;
		RGObservationFailure m_ResourceFailure = RGObservationFailure::None;
		std::string m_Failure;
		std::optional<FrameCaptureMetadata> m_Metadata;
	};

	// One terminal receipt per accepted job, with products in plan order. Published
	// requires the final bundle manifest; individual file existence is insufficient.
	struct CaptureJobResult
	{
		uint64_t m_JobId = 0;
		CaptureJobStatus m_Status = CaptureJobStatus::Completed;
		CaptureJobPublication m_Publication = CaptureJobPublication::NotPublished;
		std::vector<CaptureProductResult> m_Products;
		std::string m_BundleId;
		std::filesystem::path m_ManifestPath;
	};

	enum class CaptureJobStage : uint8_t
	{
		Waiting,
		AwaitingProducts,
		AwaitingCoverage,
		Encoding,
		Finished,
	};

	struct CaptureJobSnapshot
	{
		uint64_t m_JobId = 0;
		CaptureJobStage m_Stage = CaptureJobStage::Waiting;
		uint64_t m_AttemptCount = 0;
		std::optional<uint64_t> m_FrameSerial;
		uint32_t m_RequiredProducts = 0;
		uint32_t m_FinishedProducts = 0;
	};

	struct CaptureIntakeSnapshot
	{
		CaptureJobLimits m_Limits;
		uint32_t m_ActiveJobs = 0;
		uint32_t m_ReservedReceipts = 0;
		uint32_t m_TerminalReceipts = 0;
		// AppRuntime publication changes visibility, not reservation ownership.
		uint32_t m_AppRuntimeUnpublishedResults = 0;
		uint32_t m_AppRuntimePublishedResults = 0;
		// Cancelled jobs may have a terminal receipt while these owners still drain.
		uint32_t m_DrainingRuntimeRequests = 0;
		uint32_t m_DrainingWriterJobs = 0;
		uint32_t m_DrainingWriteReservations = 0;
		uint32_t m_SequenceTimingWrites = 0;
	};

	// Cancelled guarantees no publication for the job, not immediate resource
	// retirement. TooLate leaves the job's eventual status/publication unchanged.
	enum class CaptureCancelResult : uint8_t
	{
		Cancelled,
		TooLate,
		Finished,
		NotFound,
	};

}
