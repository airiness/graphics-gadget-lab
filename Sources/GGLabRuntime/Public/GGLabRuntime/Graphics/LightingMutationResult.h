#pragma once

#include <cstdint>

namespace gglab
{
	// Returned by this synchronous operation. Unchanged binds the existing requested
	// target, which may still be pending; zero never identifies a publishable target.
	enum class LightingMutationOutcome
	{
		Rejected,
		Unchanged,
		Requested,
	};

	struct LightingMutationResult
	{
		LightingMutationOutcome m_Outcome = LightingMutationOutcome::Rejected;
		uint64_t m_PreviousGeneration = 0;
		uint64_t m_Generation = 0;
	};
}
