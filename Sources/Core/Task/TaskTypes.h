#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	struct TaskHandle
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		explicit constexpr operator bool() const noexcept { return IsValid(); }
		friend constexpr auto operator<=>(const TaskHandle&, const TaskHandle&) = default;
	};

	enum class TaskPriority : uint8_t
	{
		Critical,
		High,
		Normal,
		Background,

		Count,
	};

	inline constexpr size_t TaskPriorityCount = static_cast<size_t>(TaskPriority::Count);

	enum class TaskStatus : uint8_t
	{
		Invalid,
		Queued,
		Running,
		Succeeded,
		Failed,
		Cancelled,
	};

	struct TaskDesc
	{
		std::string m_Name;
		TaskPriority m_Priority = TaskPriority::Normal;
	};

	struct TaskResult
	{
		bool m_Succeeded = true;
		std::string m_Error;

		[[nodiscard]] static TaskResult Success() noexcept { return {}; }
		[[nodiscard]] static TaskResult Failure(std::string error) noexcept
		{
			return {
				.m_Succeeded = false,
				.m_Error = std::move(error),
			};
		}
	};

	struct TaskCompletionInfo
	{
		TaskHandle m_Handle{};
		std::string m_Name;
		TaskPriority m_Priority = TaskPriority::Normal;
		TaskStatus m_Status = TaskStatus::Invalid;
		std::string m_Error;
		double m_QueueMilliseconds = 0.0;
		double m_ExecutionMilliseconds = 0.0;
	};

	using TaskWork = std::function<TaskResult(std::stop_token)>;
	using TaskCompletion = std::function<void(const TaskCompletionInfo&)>;

	struct TaskCompletionPumpBudget
	{
		uint32_t m_MaxCallbacks = std::numeric_limits<uint32_t>::max();
		double m_MaxMilliseconds = std::numeric_limits<double>::max();
	};

	struct TaskActivity
	{
		TaskHandle m_Handle{};
		std::string m_Name;
		TaskPriority m_Priority = TaskPriority::Normal;
		TaskStatus m_Status = TaskStatus::Invalid;
		uint32_t m_WorkerIndex = std::numeric_limits<uint32_t>::max();
		double m_QueueMilliseconds = 0.0;
		double m_ExecutionMilliseconds = 0.0;
	};

	struct TaskSystemStatistics
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
}
