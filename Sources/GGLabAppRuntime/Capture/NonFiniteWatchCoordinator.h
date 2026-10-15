#pragma once
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"

#include <cstddef>
#include <array>
#include <cstdint>
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
	struct NonFiniteWatchStatus
	{
		uint64_t m_ConfigurationVersion = 0;
		bool m_Enabled = false;
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
	};

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

	private:
		struct Pending
		{
			uint64_t m_ConfigurationVersion;
			uint64_t m_FrameSerial;
			std::vector<NonFiniteWatchTargetStatus> m_Targets;
			std::vector<NonFiniteScanTarget> m_Scans;
			std::vector<size_t> m_ScanIndices;
		};
		void PublishBoundary(Pending pending) noexcept;
		void ApplyGenerationEvidence(NonFiniteWatchTargetStatus& target, uint64_t frameSerial) const noexcept;
		void RecordAnomaly(NonFiniteWatchAnomaly anomaly) noexcept;
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
