#pragma once
#include <cstdint>

namespace gglab
{
	// Per-observation primary matching, independent of sampling/query-work caps.
	// Query work reserves the worst-case match attempts, not GPU milliseconds or
	// actual traversal counts. Completed pixels require checked fence completion.
	struct GIReceiverPreparationProgress
	{
		uint32_t m_TotalPixels = 0;
		uint32_t m_SubmittedPixels = 0;
		uint32_t m_CompletedPixels = 0;
		uint32_t m_BatchPixels = 0;
		uint32_t m_QueryWorkPerSubmission = 0;
		uint64_t m_BatchQueryWork = 0;
		// Logical capacity, not native reservation or replacement-overlap memory.
		uint64_t m_CacheBytes = 0;
		// Complete submitted data may be read through queue ordering. Scheduling
		// a later sampling submission also requires its preparation fence to complete.
		bool m_Ready = false;
	};
}
