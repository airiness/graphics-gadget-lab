#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabFoundation/Task/TaskWorkerLifecycle.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gglab
{
	// Concurrent callers may submit/cancel/update/query. The owner controls object lifetime
	// and must relinquish external callers before destruction; callbacks cannot delete it.
	class TaskSystem
	{
	public:
		struct CreateInfo
		{
			// Zero selects min(hardware_concurrency - 1, 8), with at least one worker.
			uint32_t m_WorkerCount = 0;
			uint32_t m_RecentTaskCapacity = 64;
			// Null selects the portable no-op lifecycle. A host may explicitly provide
			// per-worker platform setup without coupling scheduling to that policy.
			std::shared_ptr<const TaskWorkerLifecycle> m_WorkerLifecycle;
		};

		TaskSystem() noexcept;
		explicit TaskSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TaskSystem);
		~TaskSystem();

		[[nodiscard]] TaskHandle Submit(
			TaskDesc desc, TaskWork work, TaskCompletion completion = {}) noexcept;
		// True accepts the first cancellation decision before terminal publication.
		// Stop callbacks run synchronously outside the state lock; work may already run.
		bool Cancel(TaskHandle handle) noexcept;
		// Returns false once shutdown takes queue ownership, including unchanged priority.
		bool UpdatePriority(TaskHandle handle, TaskPriority priority) noexcept;
		// Owner-thread operation. Work captures are released before a completion can be
		// pumped. Callbacks may submit/cancel/query, but cannot pump or start shutdown.
		uint32_t PumpCompletions(const TaskCompletionPumpBudget& budget = {}) noexcept;

		// Owner-thread control flow only; also required for implicit destructor shutdown.
		// Stopping reentry and deletion from a reentrant operation fail in all builds.
		// Normal return joins workers and completes finish publication, retaining queued
		// completions for an explicit pump. Repetition after Stopped is harmless.
		void Shutdown() noexcept;
		[[nodiscard]] bool IsAcceptingTasks() const noexcept;
		// Terminal/running/pending counts are snapshots, not a work-release or drain barrier.
		[[nodiscard]] TaskSystemStatistics GetStatistics() const;

	private:
		struct TaskRecord;
		struct CompletionRecord
		{
			TaskCompletionInfo m_Info;
			std::unique_ptr<TaskCompletion> m_Callback;
		};
		enum class LifecycleState : uint8_t
		{
			Accepting,
			Stopping,
			Stopped,
		};

		[[nodiscard]] static uint32_t ResolveWorkerCount(uint32_t requested) noexcept;
		[[nodiscard]] bool HasQueuedTasksLocked() const noexcept;
		[[nodiscard]] std::shared_ptr<TaskRecord> PopNextTaskLocked() noexcept;
		void WorkerMain(std::stop_token systemStopToken, uint32_t workerIndex) noexcept;
		void FinishTask(
			const std::shared_ptr<TaskRecord>& task, TaskStatus status, std::string error) noexcept;
		void DiscardCompletions() noexcept;

		mutable std::mutex m_Mutex;
		std::condition_variable_any m_WorkAvailable;
		std::array<std::deque<std::shared_ptr<TaskRecord>>, TaskPriorityCount> m_Queues;
		std::unordered_map<uint64_t, std::shared_ptr<TaskRecord>> m_Tasks;
		std::deque<CompletionRecord> m_Completions;
		std::deque<TaskCompletionInfo> m_RecentTasks;
		std::shared_ptr<const TaskWorkerLifecycle> m_WorkerLifecycle;
		std::vector<std::jthread> m_Workers;
		std::thread::id m_OwnerThreadId;

		uint64_t m_NextTaskId = 1;
		uint64_t m_SubmittedCount = 0;
		uint64_t m_StartedCount = 0;
		uint64_t m_SucceededCount = 0;
		uint64_t m_FailedCount = 0;
		uint64_t m_CancelledCount = 0;
		uint64_t m_CompletionCallbackCount = 0;
		uint64_t m_CompletionCallbackFailureCount = 0;
		uint32_t m_RunningCount = 0;
		uint32_t m_WorkerCount = 0;
		uint64_t m_PendingFinishPublicationCount = 0;
		uint32_t m_RecentTaskCapacity = 64;
		LifecycleState m_LifecycleState = LifecycleState::Accepting;
	};
}
