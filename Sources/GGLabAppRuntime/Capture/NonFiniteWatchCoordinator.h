#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"

#include <cstddef>
#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace gglab
{
	enum class NonFiniteWatchMode : uint8_t { Report, Fail };
	struct NonFiniteWatchConfiguration
	{
		bool m_Enabled = true;
		NonFiniteWatchMode m_Mode = NonFiniteWatchMode::Report;
		bool m_Boundary = true;
		bool m_Production = true;
		std::vector<NonFiniteScanTarget> m_Targets;
		// Debug verification; requires the production claim and startup instrumentation.
		std::optional<NonFiniteInjection> m_Injection;
	};
	struct NonFiniteWatchTargetStatus
	{
		std::optional<NonFiniteWatchSemantic> m_Semantic;
		RGResourceAddress m_Address;
		NonFiniteScanRecord m_Record;
		NonFiniteBoundaryResult m_Result;
		std::optional<NonFiniteTextureLocation> m_TextureLocation;
		std::optional<uint32_t> m_Element;
		std::optional<uint32_t> m_ByteOffset;
		std::string m_Failure;
		NonFiniteWatchGeneration m_Generation;
		uint64_t m_CheckedFrameSerial = 0;
	};
	struct NonFiniteWatchAnomaly
	{
		uint64_t m_FrameSerial = 0;
		NonFiniteWatchFailure m_Failure = NonFiniteWatchFailure::None;
		std::optional<NonFiniteWatchTargetStatus> m_Target;
		std::optional<uint32_t> m_ProgramIndex;
		std::optional<uint32_t> m_SiteIndex;
	};
	// History since configuration, so audits over many frames see every site and target
	// that produced anomalies or clamps rather than only the latest completed frame.
	struct NonFiniteWatchSiteTotals
	{
		uint32_t m_ProgramIndex = 0;
		uint32_t m_SiteIndex = 0;
		// Frames whose counters recorded any replacement or clamp at this site.
		uint64_t m_Frames = 0;
		uint64_t m_FailedFrames = 0;
		uint64_t m_FirstFrameSerial = 0;
		NonFiniteSiteCounters m_Counters;
	};
	struct NonFiniteWatchTargetTotals
	{
		std::optional<NonFiniteWatchSemantic> m_Semantic;
		RGResourceAddress m_Address;
		uint64_t m_CheckedFrames = 0;
		uint64_t m_FailedFrames = 0;
		uint64_t m_FirstFailedFrameSerial = 0;
		uint64_t m_NanCount = 0;
		uint64_t m_PositiveInfCount = 0;
		uint64_t m_NegativeInfCount = 0;
		std::optional<float> m_Minimum;
		std::optional<float> m_Maximum;
	};
	struct NonFiniteWatchStatus
	{
		uint64_t m_ConfigurationVersion = 0;
		bool m_Enabled = false;
		NonFiniteWatchMode m_Mode = NonFiniteWatchMode::Report;
		bool m_BoundaryRequested = true;
		bool m_ProductionRequested = true;
		bool m_DefaultTargets = false;
		uint64_t m_BoundaryFrameSerial = 0;
		uint64_t m_ProductionFrameSerial = 0;
		NonFiniteClaimResult m_Boundary{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
		NonFiniteClaimResult m_Production{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
		std::vector<NonFiniteWatchTargetStatus> m_Targets;
		std::optional<NonFiniteWatchAnomaly> m_FirstAnomaly;
		std::string m_Failure;
		std::string m_ProductionFailure;
		std::optional<NonFiniteInjection> m_Injection;
		// Latest production evidence that carried the configured injection.
		uint64_t m_InjectionFrameSerial = 0;
		uint32_t m_InjectionAppliedCount = 0;
		// Complete production frames accumulated into the site totals.
		uint64_t m_ProductionFrames = 0;
		std::vector<NonFiniteWatchSiteTotals> m_SiteTotals;
		std::vector<NonFiniteWatchTargetTotals> m_TargetTotals;
	};
	struct NonFiniteWatchCoverage
	{
		uint64_t m_ConfigurationVersion = 0;
		NonFiniteWatchMode m_Mode = NonFiniteWatchMode::Report;
		bool m_BoundaryRequested = false;
		bool m_ProductionRequested = false;
		bool m_InstrumentationAvailable = false;
		uint64_t m_FirstFrameSerial = 0;
		uint64_t m_LastFrameSerial = 0;
		uint64_t m_FrameCount = 0;
		uint64_t m_CompletedFrames = 0;
		bool m_Complete = false;
		NonFiniteClaimResult m_Boundary{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
		NonFiniteClaimResult m_Production{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::None };
		std::optional<NonFiniteWatchAnomaly> m_FirstAnomaly;
		std::string m_Failure;
		// Debug verification bound with the configuration. Injected frames carried the
		// request in their production evidence; applied frames replaced at least one value.
		std::optional<NonFiniteInjection> m_Injection;
		uint64_t m_InjectedFrames = 0;
		uint64_t m_AppliedFrames = 0;
		[[nodiscard]] bool Fails() const noexcept;
	};
	[[nodiscard]] const char* GetNonFiniteWatchFailureName(NonFiniteWatchFailure failure) noexcept;

	// Session report state. GPU work and payload ownership remain in Runtime;
	// the capture coordinator routes the shared result stream here before encoding.
	class NonFiniteWatchCoordinator final
	{
	public:
		explicit NonFiniteWatchCoordinator(FrameCaptureControlBase* capture) noexcept : m_Capture(capture) {}
		[[nodiscard]] bool Configure(NonFiniteWatchConfiguration configuration, std::string& error) noexcept;
		void PrepareFrame(uint64_t frameSerial, bool yieldBoundaryCapacity = false,
			std::span<const NonFiniteWatchDefault> defaults = {}) noexcept;
		[[nodiscard]] bool UsesDefaultTargets() const noexcept
		{
			return m_Status.m_Enabled && m_Status.m_BoundaryRequested && m_Status.m_DefaultTargets;
		}
		// Returns true only for this coordinator's scan requests, including old versions.
		bool OnScanResult(const FrameCaptureResult& result) noexcept;
		void OnProductionFrame(const NonFiniteProductionFrame& frame) noexcept;
		[[nodiscard]] const NonFiniteWatchStatus& GetStatus() const noexcept { return m_Status; }
		// Ranges retain only unresolved frames. Completed frames reduce into claims;
		// no consumer infers range coverage from the latest interactive status.
		[[nodiscard]] uint64_t OpenCoverage() noexcept;
		void TrackCoverageFrame(uint64_t id, uint64_t frameSerial) noexcept;
		void CloseCoverage(uint64_t id) noexcept;
		void ReleaseCoverage(uint64_t id) noexcept;
		[[nodiscard]] std::shared_ptr<const NonFiniteWatchCoverage> GetCoverage(uint64_t id) const noexcept;
		void OnFrameSubmitted(uint64_t frameSerial) noexcept;
		void UpdateCoverage() noexcept;
		void FinalizeCoverage() noexcept;
		[[nodiscard]] std::string GetFailModeFailure() const noexcept;
		static constexpr uint64_t MaxPendingCoverageSubmissions = 64;

	private:
		struct Pending
		{
			uint64_t m_ConfigurationVersion;
			uint64_t m_FrameSerial;
			std::vector<NonFiniteWatchTargetStatus> m_Targets;
			std::vector<NonFiniteScanTarget> m_Scans;
			std::vector<size_t> m_ScanIndices;
			bool m_Submitted = false;
		};
		void PublishBoundary(Pending pending) noexcept;
		void ApplyGenerationEvidence(NonFiniteWatchTargetStatus& target, uint64_t frameSerial) const noexcept;
		void RecordAnomaly(NonFiniteWatchAnomaly anomaly) noexcept;
		void AccumulateSites(const NonFiniteProductionFrame& frame) noexcept;
		void AccumulateTarget(const NonFiniteWatchTargetStatus& target, uint64_t frameSerial) noexcept;
		struct CoverageFrame
		{
			uint64_t m_FrameSerial = 0;
			uint64_t m_SubmissionOrdinal = 0;
			std::chrono::steady_clock::time_point m_TrackedAt;
			bool m_BoundaryPending = false;
			bool m_ProductionPending = false;
			std::vector<NonFiniteWatchTargetStatus> m_Targets;
			NonFiniteClaimResult m_Production;
			std::optional<NonFiniteWatchAnomaly> m_Anomaly;
			std::string m_Failure;
		};
		struct CoverageState
		{
			NonFiniteWatchCoverage m_Report;
			bool m_Closed = false;
			NonFiniteClaimResult m_Boundary{ NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			NonFiniteClaimResult m_Production{ NonFiniteClaimStatus::NotApplicable, NonFiniteWatchFailure::None };
			std::vector<CoverageFrame> m_Frames;
		};
		void RefreshCoverage() noexcept;
		void ObserveCoverageBoundary(const Pending& pending) noexcept;
		std::optional<CoverageFrame> m_PreparedCoverage;
		std::unordered_map<uint64_t, CoverageState> m_Coverage;
		uint64_t m_NextCoverageId = 1;
		uint64_t m_SubmissionOrdinal = 0;
		uint64_t m_LastSubmittedFrame = 0;
		FrameCaptureControlBase* m_Capture = nullptr;
		NonFiniteWatchConfiguration m_Configuration;
		NonFiniteWatchStatus m_Status;
		std::unordered_map<uint64_t, Pending> m_Pending;
		uint64_t m_LastPreparedFrame = 0;
		uint64_t m_FirstApplicableFrame = 0;
		struct GenerationState
		{
			NonFiniteWatchGeneration m_Generation;
			std::vector<std::optional<NonFiniteWatchTargetStatus>> m_Results;
			std::vector<uint64_t> m_InFlight;
		};
		std::array<GenerationState, NonFiniteWatchSemanticCount> m_Generations;
		size_t m_GenerationCursor = 0;
	};
}
