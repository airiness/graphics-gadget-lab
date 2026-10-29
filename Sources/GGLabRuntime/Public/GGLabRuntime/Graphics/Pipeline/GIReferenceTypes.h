#pragma once
#include <cstdint>

namespace gglab
{
	enum class GIReferenceObservationStatus : uint8_t
	{
		Inactive,
		Running,
		Completed,
		Cancelled,
		Failed,
	};

	// Calculation progress only. Completed does not establish sampling/depth convergence.
	struct GIReferenceObservationDiagnostics
	{
		GIReferenceObservationStatus m_Status = GIReferenceObservationStatus::Inactive;
		uint64_t m_Generation = 0;
		uint64_t m_CommittedVersion = 0;
		uint32_t m_TargetSamples = 0;
		uint32_t m_SubmittedSamples = 0;
		uint32_t m_CompletedSamples = 0;
		uint32_t m_BatchSamples = 0;
		bool m_Active = false;
	};
}
