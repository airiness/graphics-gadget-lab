#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabFoundation/Logging/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace gglab
{
	struct TaskSystem::TaskRecord
	{
		enum class FinishPhase : uint8_t
		{
			Active,
			ReleasingWork,
			CompletionAvailable,
		};
		TaskHandle m_Handle{};
		TaskDesc m_Desc;
		std::unique_ptr<TaskWork> m_Work;
		std::unique_ptr<TaskCompletion> m_Completion;
		std::stop_source m_StopSource;
		TaskStatus m_Status = TaskStatus::Queued;
		FinishPhase m_FinishPhase = FinishPhase::Active;
		bool m_CancellationRequested = false;
		uint32_t m_WorkerIndex = std::numeric_limits<uint32_t>::max();
		std::chrono::steady_clock::time_point m_QueuedAt{};
		std::chrono::steady_clock::time_point m_StartedAt{};
	};

	namespace
	{
		constexpr LogTag TaskLogTag{ "FOUNDATION_TASK" };

		// Diagnostics here must not call a user log sink while rejecting lifecycle reentry.
		void RequireTaskContract(bool condition, const char* message) noexcept
		{
			if (!condition)
			{
				std::fprintf(stderr, "TaskSystem contract violation: %s\n", message);
				std::fflush(stderr);
				std::abort();
			}
		}

		struct TaskOperationScope;
		thread_local const TaskOperationScope* s_TaskOperation = nullptr;

		struct TaskOperationScope
		{
			explicit TaskOperationScope(const TaskSystem* system) noexcept :
				m_System(system), m_Previous(s_TaskOperation)
			{
				s_TaskOperation = this;
			}
			~TaskOperationScope() noexcept { s_TaskOperation = m_Previous; }
			GGLAB_DELETE_COPYABLE_MOVABLE(TaskOperationScope);

			const TaskSystem* m_System;
			const TaskOperationScope* m_Previous;
		};

		[[nodiscard]] bool IsTaskOperationActive(const TaskSystem* system) noexcept
		{
			for (auto* scope = s_TaskOperation; scope; scope = scope->m_Previous)
			{
				if (scope->m_System == system)
				{
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] size_t PriorityIndex(TaskPriority priority) noexcept
		{
			const size_t index = static_cast<size_t>(priority);
			return index < TaskPriorityCount ? index : static_cast<size_t>(TaskPriority::Normal);
		}

		[[nodiscard]] double Milliseconds(std::chrono::steady_clock::duration duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}
	}

#if GGLAB_DEVELOPMENT_BUILD
#define GGLAB_LOG_FOUNDATION_TASK(level, ...) Log(TaskLogTag, level, __VA_ARGS__)
#else
#define GGLAB_LOG_FOUNDATION_TASK(level, ...)
#endif

	TaskSystem::TaskSystem() noexcept : TaskSystem(CreateInfo{})
	{
	}

	TaskSystem::TaskSystem(const CreateInfo& createInfo) noexcept :
		m_WorkerLifecycle(createInfo.m_WorkerLifecycle),
		m_OwnerThreadId(std::this_thread::get_id()),
		m_RecentTaskCapacity(createInfo.m_RecentTaskCapacity)
	{
		const uint32_t workerCount = ResolveWorkerCount(createInfo.m_WorkerCount);
		m_WorkerCount = workerCount;
		m_Workers.reserve(workerCount);
		for (uint32_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			m_Workers.emplace_back([this, workerIndex](std::stop_token stopToken) noexcept
				{
					WorkerMain(stopToken, workerIndex);
				});
		}
	}

	TaskSystem::~TaskSystem()
	{
		RequireTaskContract(std::this_thread::get_id() == m_OwnerThreadId,
			"destruction must run on the owner thread");
		RequireTaskContract(!IsTaskOperationActive(this),
			"destruction during an active operation or callback");
		Shutdown();
		const TaskOperationScope operation(this);
		DiscardCompletions();
		m_WorkerLifecycle.reset();
	}

	TaskHandle TaskSystem::Submit(TaskDesc desc, TaskWork work, TaskCompletion completion) noexcept
	{
		const TaskOperationScope operation(this);
		auto task = std::make_shared<TaskRecord>();
		task->m_Desc = std::move(desc);
		task->m_Work = std::make_unique<TaskWork>(std::move(work));
		task->m_Completion = std::make_unique<TaskCompletion>(std::move(completion));
		// A moved-from type-erased callable is valid but need not be empty.
		work = {};
		completion = {};
		if (!*task->m_Work)
		{
			GGLAB_LOG_FOUNDATION_TASK(
				LogLevel::Warning, "TaskSystem rejected a task without work.");
			return {};
		}

		bool accepted = false;
		{
			std::scoped_lock lock(m_Mutex);
			if (m_LifecycleState == LifecycleState::Accepting)
			{
				task->m_Handle = TaskHandle{ m_NextTaskId++ };
				task->m_QueuedAt = std::chrono::steady_clock::now();
				m_Tasks.emplace(task->m_Handle.m_Value, task);
				m_Queues[PriorityIndex(task->m_Desc.m_Priority)].push_back(task);
				++m_SubmittedCount;
				accepted = true;
			}
		}
		if (!accepted)
		{
			GGLAB_LOG_FOUNDATION_TASK(LogLevel::Warning,
				"TaskSystem rejected task '{}' after shutdown began.", task->m_Desc.m_Name);
			return {};
		}

		m_WorkAvailable.notify_one();
		return task->m_Handle;
	}

	bool TaskSystem::Cancel(TaskHandle handle) noexcept
	{
		const TaskOperationScope operation(this);
		if (!handle.IsValid())
		{
			return false;
		}

		std::shared_ptr<TaskRecord> task;
		{
			std::scoped_lock lock(m_Mutex);
			const auto iterator = m_Tasks.find(handle.m_Value);
			if (iterator == m_Tasks.end() ||
				iterator->second->m_FinishPhase != TaskRecord::FinishPhase::Active ||
				iterator->second->m_CancellationRequested)
			{
				return false;
			}
			task = iterator->second;
			task->m_CancellationRequested = true;
		}
		task->m_StopSource.request_stop();
		return true;
	}

	bool TaskSystem::UpdatePriority(TaskHandle handle, TaskPriority priority) noexcept
	{
		const TaskOperationScope operation(this);
		if (!handle.IsValid() || priority >= TaskPriority::Count)
		{
			return false;
		}
		std::shared_ptr<TaskRecord> pin;
		{
			std::scoped_lock lock(m_Mutex);
			if (m_LifecycleState != LifecycleState::Accepting)
			{
				return false;
			}
			const auto iterator = m_Tasks.find(handle.m_Value);
			if (iterator == m_Tasks.end())
			{
				return false;
			}

			const std::shared_ptr<TaskRecord>& task = iterator->second;
			pin = task;
			if (task->m_Status != TaskStatus::Queued && task->m_Status != TaskStatus::Running)
			{
				return false;
			}
			if (task->m_Desc.m_Priority == priority)
			{
				return true;
			}

			if (task->m_Status == TaskStatus::Queued)
			{
				auto& oldQueue = m_Queues[PriorityIndex(task->m_Desc.m_Priority)];
				const auto queuedTask = std::ranges::find(oldQueue, task);
				GGLAB_ASSERT_MSG(queuedTask != oldQueue.end(),
					"Queued TaskSystem record was not present in its priority queue.");
				if (queuedTask == oldQueue.end())
				{
					return false;
				}
				oldQueue.erase(queuedTask);
				m_Queues[PriorityIndex(priority)].push_back(task);
			}
			task->m_Desc.m_Priority = priority;
		}
		m_WorkAvailable.notify_one();
		return true;
	}

	uint32_t TaskSystem::PumpCompletions(const TaskCompletionPumpBudget& budget) noexcept
	{
		const bool isOwnerThread = std::this_thread::get_id() == m_OwnerThreadId;
		GGLAB_ASSERT_MSG(
			isOwnerThread, "TaskSystem completions must be pumped on the owner thread.");
		if (!isOwnerThread)
		{
			return 0;
		}
		RequireTaskContract(!IsTaskOperationActive(this), "recursive completion pumping");
		const TaskOperationScope operation(this);

		const auto begin = std::chrono::steady_clock::now();
		uint32_t callbackCount = 0;
		while (callbackCount < budget.m_MaxCallbacks)
		{
			CompletionRecord completion{};
			std::shared_ptr<TaskRecord> pin;
			{
				std::scoped_lock lock(m_Mutex);
				if (m_Completions.empty())
				{
					break;
				}
				completion = std::move(m_Completions.front());
				m_Completions.pop_front();
				pin = m_Tasks.at(completion.m_Info.m_Handle.m_Value);
				m_Tasks.erase(completion.m_Info.m_Handle.m_Value);
			}

			if (completion.m_Callback && *completion.m_Callback)
			{
				try
				{
					(*completion.m_Callback)(completion.m_Info);
					std::scoped_lock lock(m_Mutex);
					++m_CompletionCallbackCount;
				}
				catch (const std::exception& exception)
				{
					GGLAB_UNUSED(exception);
					GGLAB_LOG_FOUNDATION_TASK(LogLevel::Error,
						"TaskSystem completion '{}' threw an exception: {}",
						completion.m_Info.m_Name, exception.what());
					std::scoped_lock lock(m_Mutex);
					++m_CompletionCallbackFailureCount;
				}
				catch (...)
				{
					GGLAB_LOG_FOUNDATION_TASK(LogLevel::Error,
						"TaskSystem completion '{}' threw an unknown exception.",
						completion.m_Info.m_Name);
					std::scoped_lock lock(m_Mutex);
					++m_CompletionCallbackFailureCount;
				}
			}

			++callbackCount;
			if (Milliseconds(std::chrono::steady_clock::now() - begin) >= budget.m_MaxMilliseconds)
			{
				break;
			}
		}
		return callbackCount;
	}

	void TaskSystem::Shutdown() noexcept
	{
		RequireTaskContract(std::this_thread::get_id() == m_OwnerThreadId,
			"shutdown must run on the owner thread");
		LifecycleState state;
		{
			std::scoped_lock lock(m_Mutex);
			state = m_LifecycleState;
		}
		RequireTaskContract(state != LifecycleState::Stopping, "shutdown reentered while stopping");
		if (state == LifecycleState::Stopped)
		{
			return;
		}
		RequireTaskContract(!IsTaskOperationActive(this), "shutdown from an active operation or callback");
		const TaskOperationScope operation(this);
		decltype(m_Queues) abandoned;
		std::vector<std::shared_ptr<TaskRecord>> stopTasks;
		std::vector<std::jthread> workers;
		{
			std::scoped_lock lock(m_Mutex);
			m_LifecycleState = LifecycleState::Stopping;
			for (const auto& task : m_Tasks | std::views::values)
			{
				if (task->m_FinishPhase == TaskRecord::FinishPhase::Active)
				{
					task->m_CancellationRequested = true;
				}
				if (task->m_CancellationRequested)
				{
					stopTasks.push_back(task);
				}
			}
			abandoned.swap(m_Queues);
			workers.swap(m_Workers);
		}
		for (const auto& task : stopTasks)
		{
			task->m_StopSource.request_stop();
		}
		for (auto& worker : workers)
		{
			worker.request_stop();
		}
		m_WorkAvailable.notify_all();
		for (const auto& queue : abandoned)
		{
			for (const auto& task : queue)
			{
				FinishTask(task, TaskStatus::Cancelled, {});
			}
		}
		for (auto& worker : workers)
		{
			worker.join();
			std::scoped_lock lock(m_Mutex);
			--m_WorkerCount;
		}
		bool finished;
		{
			std::scoped_lock lock(m_Mutex);
			finished = m_WorkerCount == 0 && m_RunningCount == 0 &&
				m_PendingFinishPublicationCount == 0 &&
				std::ranges::all_of(m_Tasks | std::views::values, [](const auto& task) noexcept
				{
					return task->m_FinishPhase == TaskRecord::FinishPhase::CompletionAvailable;
				});
			if (finished)
			{
				m_LifecycleState = LifecycleState::Stopped;
			}
		}
		RequireTaskContract(finished, "shutdown did not drain finish publication");
	}

	bool TaskSystem::IsAcceptingTasks() const noexcept
	{
		const TaskOperationScope operation(this);
		std::scoped_lock lock(m_Mutex);
		return m_LifecycleState == LifecycleState::Accepting;
	}

	TaskSystemStatistics TaskSystem::GetStatistics() const
	{
		const TaskOperationScope operation(this);
		TaskSystemStatistics statistics{};
		const auto now = std::chrono::steady_clock::now();
		std::scoped_lock lock(m_Mutex);
		statistics.m_WorkerCount = m_WorkerCount;
		for (size_t priorityIndex = 0; priorityIndex < m_Queues.size(); ++priorityIndex)
		{
			statistics.m_QueuedByPriority[priorityIndex] =
				static_cast<uint32_t>(m_Queues[priorityIndex].size());
		}
		statistics.m_RunningCount = m_RunningCount;
		statistics.m_PendingCompletionCount = static_cast<uint32_t>(m_Completions.size());
		statistics.m_SubmittedCount = m_SubmittedCount;
		statistics.m_StartedCount = m_StartedCount;
		statistics.m_SucceededCount = m_SucceededCount;
		statistics.m_FailedCount = m_FailedCount;
		statistics.m_CancelledCount = m_CancelledCount;
		statistics.m_CompletionCallbackCount = m_CompletionCallbackCount;
		statistics.m_CompletionCallbackFailureCount = m_CompletionCallbackFailureCount;
		statistics.m_AcceptingTasks = m_LifecycleState == LifecycleState::Accepting;
		statistics.m_RecentTasks.assign(m_RecentTasks.begin(), m_RecentTasks.end());

		statistics.m_ActiveTasks.reserve(m_Tasks.size());
		for (const auto& task : m_Tasks | std::views::values)
		{
			if (task->m_Status != TaskStatus::Queued && task->m_Status != TaskStatus::Running)
			{
				continue;
			}
			TaskActivity activity{};
			activity.m_Handle = task->m_Handle;
			activity.m_Name = task->m_Desc.m_Name;
			activity.m_Priority = task->m_Desc.m_Priority;
			activity.m_Status = task->m_Status;
			if (task->m_Desc.m_Progress)
			{
				activity.m_Progress = task->m_Desc.m_Progress->GetSnapshot();
			}
			activity.m_WorkerIndex = task->m_WorkerIndex;
			activity.m_QueueMilliseconds =
				Milliseconds((task->m_Status == TaskStatus::Queued ? now : task->m_StartedAt) -
					task->m_QueuedAt);
			if (task->m_Status == TaskStatus::Running)
			{
				activity.m_ExecutionMilliseconds = Milliseconds(now - task->m_StartedAt);
			}
			statistics.m_ActiveTasks.push_back(std::move(activity));
		}
		return statistics;
	}

	uint32_t TaskSystem::ResolveWorkerCount(uint32_t requested) noexcept
	{
		if (requested > 0)
		{
			return requested;
		}
		const uint32_t hardwareThreads = std::thread::hardware_concurrency();
		const uint32_t availableWorkers = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
		return std::clamp(availableWorkers, 1u, 8u);
	}

	bool TaskSystem::HasQueuedTasksLocked() const noexcept
	{
		return std::ranges::any_of(
			m_Queues, [](const auto& queue) noexcept { return !queue.empty(); });
	}

	std::shared_ptr<TaskSystem::TaskRecord> TaskSystem::PopNextTaskLocked() noexcept
	{
		for (auto& queue : m_Queues)
		{
			if (!queue.empty())
			{
				auto task = std::move(queue.front());
				queue.pop_front();
				return task;
			}
		}
		return {};
	}

	void TaskSystem::WorkerMain(std::stop_token systemStopToken, uint32_t workerIndex) noexcept
	{
		const TaskOperationScope operation(this);
		std::unique_ptr<TaskWorkerContext> workerContext;
		if (m_WorkerLifecycle)
		{
			try
			{
				workerContext = m_WorkerLifecycle->CreateContext(workerIndex);
			}
			catch (const std::exception& exception)
			{
				GGLAB_UNUSED(exception);
				GGLAB_LOG_FOUNDATION_TASK(LogLevel::Error,
					"TaskSystem worker {} lifecycle setup threw an exception: {}", workerIndex,
					exception.what());
			}
			catch (...)
			{
				GGLAB_LOG_FOUNDATION_TASK(LogLevel::Error,
					"TaskSystem worker {} lifecycle setup threw an unknown exception.",
					workerIndex);
			}
		}

		while (!systemStopToken.stop_requested())
		{
			std::shared_ptr<TaskRecord> task;
			bool executeWork = false;
			{
				std::unique_lock lock(m_Mutex);
				m_WorkAvailable.wait(
					lock, systemStopToken, [this]() noexcept
					{
						return m_LifecycleState != LifecycleState::Accepting || HasQueuedTasksLocked();
					});
				if (m_LifecycleState != LifecycleState::Accepting || systemStopToken.stop_requested())
				{
					break;
				}
				task = PopNextTaskLocked();
				if (!task)
				{
					continue;
				}
				task->m_Status = TaskStatus::Running;
				task->m_WorkerIndex = workerIndex;
				task->m_StartedAt = std::chrono::steady_clock::now();
				++m_RunningCount;
				++m_StartedCount;
				executeWork = !task->m_CancellationRequested;
			}

			if (!executeWork)
			{
				FinishTask(task, TaskStatus::Cancelled, {});
				continue;
			}

			TaskResult result{};
			TaskStatus status = TaskStatus::Succeeded;
			try
			{
				result = (*task->m_Work)(task->m_StopSource.get_token());
				status = result.m_Succeeded ? TaskStatus::Succeeded : TaskStatus::Failed;
			}
			catch (const std::exception& exception)
			{
				status = TaskStatus::Failed;
				result.m_Error = exception.what();
			}
			catch (...)
			{
				status = TaskStatus::Failed;
				result.m_Error = "Unknown task exception.";
			}

			FinishTask(task, status, std::move(result.m_Error));
		}
	}

	void TaskSystem::FinishTask(
		const std::shared_ptr<TaskRecord>& task, TaskStatus status, std::string error) noexcept
	{
		const TaskOperationScope operation(this);
		const auto finishedAt = std::chrono::steady_clock::now();
		std::unique_ptr<TaskWork> work;
		TaskCompletionInfo info{};
		info.m_Handle = task->m_Handle;
		info.m_Name = task->m_Desc.m_Name;

		{
			std::scoped_lock lock(m_Mutex);
			RequireTaskContract(task->m_FinishPhase == TaskRecord::FinishPhase::Active,
				"duplicate terminal decision");
			if (task->m_CancellationRequested)
			{
				status = TaskStatus::Cancelled;
				error.clear();
			}
			info.m_Status = status;
			info.m_Error = std::move(error);
			const bool wasRunning = task->m_Status == TaskStatus::Running;
			info.m_QueueMilliseconds =
				Milliseconds((wasRunning ? task->m_StartedAt : finishedAt) - task->m_QueuedAt);
			info.m_ExecutionMilliseconds =
				wasRunning ? Milliseconds(finishedAt - task->m_StartedAt) : 0.0;
			info.m_Priority = task->m_Desc.m_Priority;
			task->m_Status = status;
			task->m_FinishPhase = TaskRecord::FinishPhase::ReleasingWork;
			++m_PendingFinishPublicationCount;
			work = std::move(task->m_Work);
			if (wasRunning)
			{
				GGLAB_ASSERT(m_RunningCount > 0);
				--m_RunningCount;
			}
			switch (status)
			{
			case TaskStatus::Succeeded:
				++m_SucceededCount;
				break;
			case TaskStatus::Failed:
				++m_FailedCount;
				break;
			case TaskStatus::Cancelled:
				++m_CancelledCount;
				break;
			default:
				break;
			}
			if (m_RecentTaskCapacity > 0)
			{
				m_RecentTasks.push_front(info);
				while (m_RecentTasks.size() > m_RecentTaskCapacity)
				{
					m_RecentTasks.pop_back();
				}
			}
		}
		// Terminal state is visible, but no completion may observe work still being released.
		work.reset();
		{
			std::scoped_lock lock(m_Mutex);
			RequireTaskContract(task->m_FinishPhase == TaskRecord::FinishPhase::ReleasingWork &&
				m_PendingFinishPublicationCount > 0, "invalid finish publication phase");
			m_Completions.push_back({ std::move(info), std::move(task->m_Completion) });
			task->m_FinishPhase = TaskRecord::FinishPhase::CompletionAvailable;
			--m_PendingFinishPublicationCount;
		}
	}

	void TaskSystem::DiscardCompletions() noexcept
	{
		decltype(m_Completions) completions;
		decltype(m_Tasks) tasks;
		{
			std::scoped_lock lock(m_Mutex);
			completions.swap(m_Completions);
			tasks.swap(m_Tasks);
		}
	}

#undef GGLAB_LOG_FOUNDATION_TASK
}
