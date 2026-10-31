#pragma once
#include <cstdint>

namespace gglab
{
	// Shared admission limits for bounded diffuse and specular observations.
	// Work counts are conservative ray/sample counts, not a GPU-time budget.
	struct GIObservationLimits
	{
		static constexpr uint32_t MaximumSamples = 1u << 20;
		static constexpr uint32_t MaximumBatchSamples = 4096;
		static constexpr uint32_t MaximumImagePixels = 1u << 23;
		static constexpr uint32_t MaximumSampleWorkPerSubmission = 1u << 23;
		static constexpr uint32_t MaximumQueryWorkPerSubmission = 1u << 24;
		static constexpr uint32_t MaximumDiffuseDepth = 256;
		static constexpr uint32_t DefaultSeed = 61051;
		static constexpr uint32_t DefaultReferenceSamples = 64;
		static constexpr uint32_t DefaultVisibilitySamples = 4096;
		static constexpr uint32_t DefaultBatchSamples = 64;
		static constexpr uint32_t DefaultVisibilityQueryWork = 1u << 23;
		// Each diffuse hit may query its successor and sun visibility; GGX evaluates
		// at most one query for each of the base and clearcoat lobes.
		static constexpr uint32_t QueriesPerDiffuseDepth = 2;
		static constexpr uint32_t VisibilityQueriesPerSample = 2;
	};
}
