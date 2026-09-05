#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gglab
{
	struct TaskSystemSnapshot
	{
		uint32_t m_WorkerCount = 0;
		std::array<uint32_t, TaskPriorityCount> m_QueuedByPriority{};
		uint32_t m_RunningCount = 0;
		uint32_t m_PendingCompletionCount = 0;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_StartedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CancelledCount = 0;
		uint64_t m_CompletionCallbackCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		bool m_AcceptingTasks = false;
		std::vector<TaskActivity> m_ActiveTasks;
		std::vector<TaskCompletionInfo> m_RecentTasks;
	};

	template <> struct SnapshotTraits<TaskSystemSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.TaskSystemSnapshot");
	};
}
