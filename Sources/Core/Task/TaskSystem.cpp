#include "Core/Precompiled.h"
#include "Core/Task/TaskSystem.h"

#include <exception>
#include <ranges>

namespace gglab
{
	struct TaskSystem::TaskRecord
	{
		TaskHandle m_Handle{};
		TaskDesc m_Desc;
		TaskWork m_Work;
		TaskCompletion m_Completion;
		std::stop_source m_StopSource;
		TaskStatus m_Status = TaskStatus::Queued;
		uint32_t m_WorkerIndex = std::numeric_limits<uint32_t>::max();
		std::chrono::steady_clock::time_point m_QueuedAt{};
		std::chrono::steady_clock::time_point m_StartedAt{};
	};

	namespace
	{
		[[nodiscard]] size_t PriorityIndex(TaskPriority priority) noexcept
		{
			const size_t index = static_cast<size_t>(priority);
			return index < TaskPriorityCount ? index : static_cast<size_t>(TaskPriority::Normal);
		}

		[[nodiscard]] double Milliseconds(
			std::chrono::steady_clock::duration duration) noexcept
		{
			return std::chrono::duration<double, std::milli>(duration).count();
		}
	}

	TaskSystem::TaskSystem() noexcept :
		TaskSystem(CreateInfo{})
	{}

	TaskSystem::TaskSystem(const CreateInfo& createInfo) noexcept :
		m_OwnerThreadId(std::this_thread::get_id()),
		m_RecentTaskCapacity(createInfo.m_RecentTaskCapacity)
	{
		const uint32_t workerCount = ResolveWorkerCount(createInfo.m_WorkerCount);
		m_Workers.reserve(workerCount);
		for (uint32_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
		{
			m_Workers.emplace_back(
				[this, workerIndex](std::stop_token stopToken) noexcept
				{
					WorkerMain(stopToken, workerIndex);
				});
		}
	}

	TaskSystem::~TaskSystem()
	{
		Shutdown();
		DiscardCompletions();
	}

	TaskHandle TaskSystem::Submit(
		TaskDesc desc,
		TaskWork work,
		TaskCompletion completion) noexcept
	{
		if (!work)
		{
			GGLAB_LOG_WARN("TaskSystem rejected a task without work.");
			return {};
		}

		auto task = std::make_shared<TaskRecord>();
		{
			std::scoped_lock lock(m_Mutex);
			if (!m_AcceptingTasks)
			{
				GGLAB_LOG_WARN("TaskSystem rejected task '{}' after shutdown began.", desc.m_Name);
				return {};
			}

			task->m_Handle = TaskHandle{ m_NextTaskId++ };
			task->m_Desc = std::move(desc);
			task->m_Work = std::move(work);
			task->m_Completion = std::move(completion);
			task->m_QueuedAt = std::chrono::steady_clock::now();
			m_Tasks.emplace(task->m_Handle.m_Value, task);
			m_Queues[PriorityIndex(task->m_Desc.m_Priority)].push_back(task);
			++m_SubmittedCount;
		}

		m_WorkAvailable.notify_one();
		return task->m_Handle;
	}

	bool TaskSystem::Cancel(TaskHandle handle) noexcept
	{
		if (!handle.IsValid())
		{
			return false;
		}

		std::scoped_lock lock(m_Mutex);
		const auto iterator = m_Tasks.find(handle.m_Value);
		if (iterator == m_Tasks.end())
		{
			return false;
		}
		if (iterator->second->m_Status != TaskStatus::Queued &&
			iterator->second->m_Status != TaskStatus::Running)
		{
			return false;
		}
		return iterator->second->m_StopSource.request_stop();
	}

	uint32_t TaskSystem::PumpCompletions(
		const TaskCompletionPumpBudget& budget) noexcept
	{
		const bool isOwnerThread = std::this_thread::get_id() == m_OwnerThreadId;
		GGLAB_ASSERT_MSG(isOwnerThread, "TaskSystem completions must be pumped on the owner thread.");
		if (!isOwnerThread)
		{
			return 0;
		}

		const auto begin = std::chrono::steady_clock::now();
		uint32_t callbackCount = 0;
		while (callbackCount < budget.m_MaxCallbacks)
		{
			CompletionRecord completion{};
			{
				std::scoped_lock lock(m_Mutex);
				if (m_Completions.empty())
				{
					break;
				}
				completion = std::move(m_Completions.front());
				m_Completions.pop_front();
				m_Tasks.erase(completion.m_Info.m_Handle.m_Value);
			}

			if (completion.m_Callback)
			{
				try
				{
					completion.m_Callback(completion.m_Info);
					std::scoped_lock lock(m_Mutex);
					++m_CompletionCallbackCount;
				}
				catch (const std::exception& exception)
				{
					GGLAB_LOG_ERROR(
						"TaskSystem completion '{}' threw an exception: {}",
						completion.m_Info.m_Name,
						exception.what());
					std::scoped_lock lock(m_Mutex);
					++m_CompletionCallbackFailureCount;
				}
				catch (...)
				{
					GGLAB_LOG_ERROR(
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
		{
			std::scoped_lock lock(m_Mutex);
			if (m_Shutdown)
			{
				return;
			}
			m_AcceptingTasks = false;
			m_Shutdown = true;
			for (const auto& task : m_Tasks | std::views::values)
			{
				task->m_StopSource.request_stop();
			}
		}

		for (auto& worker : m_Workers)
		{
			worker.request_stop();
		}
		m_WorkAvailable.notify_all();
		m_Workers.clear();

		std::vector<std::shared_ptr<TaskRecord>> abandoned;
		{
			std::scoped_lock lock(m_Mutex);
			for (auto& queue : m_Queues)
			{
				while (!queue.empty())
				{
					abandoned.push_back(std::move(queue.front()));
					queue.pop_front();
				}
			}
		}
		for (const auto& task : abandoned)
		{
			FinishTask(task, TaskStatus::Cancelled, {});
		}
	}

	bool TaskSystem::IsAcceptingTasks() const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_AcceptingTasks;
	}

	TaskSystemStatistics TaskSystem::GetStatistics() const
	{
		TaskSystemStatistics statistics{};
		const auto now = std::chrono::steady_clock::now();
		std::scoped_lock lock(m_Mutex);
		statistics.m_WorkerCount = static_cast<uint32_t>(m_Workers.size());
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
		statistics.m_AcceptingTasks = m_AcceptingTasks;
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
			activity.m_QueueMilliseconds = Milliseconds(
				(task->m_Status == TaskStatus::Queued ? now : task->m_StartedAt) - task->m_QueuedAt);
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
		return std::ranges::any_of(m_Queues,
			[](const auto& queue) noexcept { return !queue.empty(); });
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

	void TaskSystem::WorkerMain(
		std::stop_token systemStopToken,
		uint32_t workerIndex) noexcept
	{
		const std::wstring threadName = std::format(L"gglab.TaskWorker.{}", workerIndex);
		GGLAB_UNUSED(SetThreadDescription(GetCurrentThread(), threadName.c_str()));
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool uninitializeCom = SUCCEEDED(comResult);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
		{
			GGLAB_LOG_ERROR(
				"TaskSystem worker {} failed to initialize COM: 0x{:08X}.",
				workerIndex,
				static_cast<uint32_t>(comResult));
		}

		while (!systemStopToken.stop_requested())
		{
			std::shared_ptr<TaskRecord> task;
			{
				std::unique_lock lock(m_Mutex);
				m_WorkAvailable.wait(lock, systemStopToken,
					[this]() noexcept { return HasQueuedTasksLocked(); });
				if (systemStopToken.stop_requested())
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
			}

			if (task->m_StopSource.stop_requested())
			{
				FinishTask(task, TaskStatus::Cancelled, {});
				continue;
			}

			TaskResult result{};
			TaskStatus status = TaskStatus::Succeeded;
			try
			{
				result = task->m_Work(task->m_StopSource.get_token());
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

			if (task->m_StopSource.stop_requested())
			{
				status = TaskStatus::Cancelled;
				result.m_Error.clear();
			}
			FinishTask(task, status, std::move(result.m_Error));
		}

		if (uninitializeCom)
		{
			CoUninitialize();
		}
	}

	void TaskSystem::FinishTask(
		const std::shared_ptr<TaskRecord>& task,
		TaskStatus status,
		std::string error) noexcept
	{
		const auto finishedAt = std::chrono::steady_clock::now();
		TaskCompletionInfo info{};
		info.m_Handle = task->m_Handle;
		info.m_Name = task->m_Desc.m_Name;
		info.m_Priority = task->m_Desc.m_Priority;
		info.m_Status = status;
		info.m_Error = std::move(error);
		const bool wasRunning = task->m_Status == TaskStatus::Running;
		info.m_QueueMilliseconds = Milliseconds(
			(wasRunning ? task->m_StartedAt : finishedAt) - task->m_QueuedAt);
		info.m_ExecutionMilliseconds = wasRunning ?
			Milliseconds(finishedAt - task->m_StartedAt) : 0.0;

		{
			std::scoped_lock lock(m_Mutex);
			task->m_Status = status;
			task->m_Work = {};
			if (wasRunning)
			{
				GGLAB_ASSERT(m_RunningCount > 0);
				--m_RunningCount;
			}
			switch (status)
			{
			case TaskStatus::Succeeded: ++m_SucceededCount; break;
			case TaskStatus::Failed: ++m_FailedCount; break;
			case TaskStatus::Cancelled: ++m_CancelledCount; break;
			default: break;
			}
			m_Completions.push_back({ info, std::move(task->m_Completion) });
			if (m_RecentTaskCapacity > 0)
			{
				m_RecentTasks.push_front(info);
				while (m_RecentTasks.size() > m_RecentTaskCapacity)
				{
					m_RecentTasks.pop_back();
				}
			}
		}
	}

	void TaskSystem::DiscardCompletions() noexcept
	{
		std::scoped_lock lock(m_Mutex);
		m_Completions.clear();
		m_Tasks.clear();
	}
}
