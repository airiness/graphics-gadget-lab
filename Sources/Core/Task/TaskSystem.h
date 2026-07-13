#pragma once
#include "Core/CoreMacros.h"
#include "Core/Task/TaskTypes.h"

#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace gglab
{
	class TaskSystem
	{
	public:
		struct CreateInfo
		{
			// Zero selects min(hardware_concurrency - 1, 8), with at least one worker.
			uint32_t m_WorkerCount = 0;
			uint32_t m_RecentTaskCapacity = 64;
		};

		TaskSystem() noexcept;
		explicit TaskSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TaskSystem);
		~TaskSystem();

		[[nodiscard]] TaskHandle Submit(
			TaskDesc desc,
			TaskWork work,
			TaskCompletion completion = {}) noexcept;
		bool Cancel(TaskHandle handle) noexcept;
		uint32_t PumpCompletions(
			const TaskCompletionPumpBudget& budget = {}) noexcept;

		void Shutdown() noexcept;
		[[nodiscard]] bool IsAcceptingTasks() const noexcept;
		[[nodiscard]] TaskSystemStatistics GetStatistics() const;

	private:
		struct TaskRecord;
		struct CompletionRecord
		{
			TaskCompletionInfo m_Info;
			TaskCompletion m_Callback;
		};

		[[nodiscard]] static uint32_t ResolveWorkerCount(uint32_t requested) noexcept;
		[[nodiscard]] bool HasQueuedTasksLocked() const noexcept;
		[[nodiscard]] std::shared_ptr<TaskRecord> PopNextTaskLocked() noexcept;
		void WorkerMain(std::stop_token systemStopToken, uint32_t workerIndex) noexcept;
		void FinishTask(
			const std::shared_ptr<TaskRecord>& task,
			TaskStatus status,
			std::string error) noexcept;
		void DiscardCompletions() noexcept;

		mutable std::mutex m_Mutex;
		std::condition_variable_any m_WorkAvailable;
		std::array<std::deque<std::shared_ptr<TaskRecord>>, TaskPriorityCount> m_Queues;
		std::unordered_map<uint64_t, std::shared_ptr<TaskRecord>> m_Tasks;
		std::deque<CompletionRecord> m_Completions;
		std::deque<TaskCompletionInfo> m_RecentTasks;
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
		uint32_t m_RecentTaskCapacity = 64;
		bool m_AcceptingTasks = true;
		bool m_Shutdown = false;
	};
}
