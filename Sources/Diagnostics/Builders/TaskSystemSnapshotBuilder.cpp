#include "Core/Precompiled.h"
#include "Diagnostics/Builders/TaskSystemSnapshotBuilder.h"
#include "Core/Task/TaskSystem.h"
#include "Diagnostics/Snapshots/TaskSystemSnapshot.h"

namespace gglab
{
	void BuildTaskSystemSnapshot(
		const TaskSystem& taskSystem, TaskSystemSnapshot& outSnapshot) noexcept
	{
		const TaskSystemStatistics statistics = taskSystem.GetStatistics();
		outSnapshot.m_WorkerCount = statistics.m_WorkerCount;
		outSnapshot.m_QueuedByPriority = statistics.m_QueuedByPriority;
		outSnapshot.m_RunningCount = statistics.m_RunningCount;
		outSnapshot.m_PendingCompletionCount = statistics.m_PendingCompletionCount;
		outSnapshot.m_SubmittedCount = statistics.m_SubmittedCount;
		outSnapshot.m_StartedCount = statistics.m_StartedCount;
		outSnapshot.m_SucceededCount = statistics.m_SucceededCount;
		outSnapshot.m_FailedCount = statistics.m_FailedCount;
		outSnapshot.m_CancelledCount = statistics.m_CancelledCount;
		outSnapshot.m_CompletionCallbackCount = statistics.m_CompletionCallbackCount;
		outSnapshot.m_CompletionCallbackFailureCount = statistics.m_CompletionCallbackFailureCount;
		outSnapshot.m_AcceptingTasks = statistics.m_AcceptingTasks;
		outSnapshot.m_ActiveTasks = statistics.m_ActiveTasks;
		outSnapshot.m_RecentTasks = statistics.m_RecentTasks;
	}
}
