#include "Application/Lab/Sessions/TaskSystemLabSession.h"
#include "AppRuntimeLog.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		const LabParameterId ScenarioId("task_system.scenario");
		const LabParameterId TaskCountId("task_system.task_count");
		const LabParameterId WorkUnitsId("task_system.work_units");

		constexpr std::array PrioritySubmissionOrder = {
			TaskPriority::Background,
			TaskPriority::Normal,
			TaskPriority::High,
			TaskPriority::Critical,
		};

		const char* ScenarioText(TaskSystemLabSession::Scenario scenario) noexcept
		{
			switch (scenario)
			{
			case TaskSystemLabSession::Scenario::BurstSuccess:
				return "Burst Success";
			case TaskSystemLabSession::Scenario::PriorityOrdering:
				return "Priority Ordering";
			case TaskSystemLabSession::Scenario::CancelQueued:
				return "Cancel Queued";
			case TaskSystemLabSession::Scenario::CancelRunning:
				return "Cancel Running";
			case TaskSystemLabSession::Scenario::ExplicitFailure:
				return "Explicit Failure";
			case TaskSystemLabSession::Scenario::ExceptionFailure:
				return "Exception Failure";
			case TaskSystemLabSession::Scenario::CompletionBacklog:
				return "Completion Backlog";
			case TaskSystemLabSession::Scenario::SessionSwitchSafety:
				return "Session Switch Safety";
			}
			return "Unknown";
		}

		TaskResult RunDeterministicWork(std::stop_token stopToken, uint32_t workUnits,
			std::atomic<uint64_t>& checksum, const ProgressReporter& progress = {}) noexcept
		{
			uint64_t value = 0x9E3779B97F4A7C15ull;
			workUnits = std::max(workUnits, 1u);
			for (uint32_t workUnit = 0; workUnit < workUnits; ++workUnit)
			{
				progress.Report(static_cast<float>(workUnit) / static_cast<float>(workUnits),
					"Executing deterministic work",
					std::format("Unit {} of {}", workUnit + 1, workUnits), workUnit, workUnits);
				for (uint32_t iteration = 0; iteration < 4096u; ++iteration)
				{
					if ((iteration & 255u) == 0 && stopToken.stop_requested())
					{
						return TaskResult::Success();
					}
					value ^= value >> 12;
					value ^= value << 25;
					value ^= value >> 27;
					value *= 0x2545F4914F6CDD1Dull;
				}
			}
			progress.Report(1.0f, "Deterministic work complete", std::format("{} units", workUnits),
				workUnits, workUnits);
			checksum.fetch_xor(value, std::memory_order_relaxed);
			return TaskResult::Success();
		}

		bool AcquireGatePermit(std::stop_token stopToken, std::atomic<uint32_t>& permits) noexcept
		{
			while (!stopToken.stop_requested())
			{
				uint32_t available = permits.load(std::memory_order_acquire);
				while (available > 0)
				{
					if (permits.compare_exchange_weak(available, available - 1,
						std::memory_order_acq_rel, std::memory_order_acquire))
					{
						return true;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return false;
		}
	}

	struct TaskSystemLabSession::ScenarioState
	{
		Scenario m_Scenario = Scenario::BurstSuccess;
		uint64_t m_Generation = 0;
		uint32_t m_WorkerCount = 0;
		uint32_t m_RequestedTargetCount = 0;
		uint32_t m_SubmittedCount = 0;
		uint32_t m_ExpectedCompletions = 0;
		uint32_t m_CompletedCount = 0;
		uint32_t m_SucceededCount = 0;
		uint32_t m_FailedCount = 0;
		uint32_t m_CancelledCount = 0;
		uint32_t m_TargetCompletedCount = 0;
		uint32_t m_TargetCancelledCount = 0;
		uint32_t m_WrongThreadCompletionCount = 0;
		uint32_t m_SubmitFailureCount = 0;
		uint32_t m_MaxCompletionBacklog = 0;
		uint32_t m_OrderingViolationCount = 0;
		double m_MaxQueueMilliseconds = 0.0;
		double m_MaxExecutionMilliseconds = 0.0;
		std::thread::id m_OwnerThread;
		std::atomic<uint32_t> m_StartedCount = 0;
		std::atomic<uint32_t> m_GateStartedCount = 0;
		std::atomic<uint32_t> m_GateReleasePermits = 0;
		std::atomic<uint32_t> m_TargetStartedCount = 0;
		std::atomic<uint32_t> m_TargetExecutedCount = 0;
		std::atomic<uint64_t> m_Checksum = 0;
		std::array<uint32_t, TaskPriorityCount> m_PriorityRemaining{};
		mutable std::mutex m_PriorityMutex;
		std::vector<std::string> m_Errors;
		bool m_Finished = false;
		bool m_Passed = false;
		bool m_TimedOut = false;
	};

	TaskSystemLabSession::TaskSystemLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ScenarioId,
			.m_Name = "Scenario",
			.m_Group = "Verification",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(0),
			.m_EnumItems =
				{
					{.m_Value = 0, .m_Name = "Burst Success"},
					{.m_Value = 1, .m_Name = "Priority Ordering"},
					{.m_Value = 2, .m_Name = "Cancel Queued"},
					{.m_Value = 3, .m_Name = "Cancel Running"},
					{.m_Value = 4, .m_Name = "Explicit Failure"},
					{.m_Value = 5, .m_Name = "Exception Failure"},
					{.m_Value = 6, .m_Name = "Completion Backlog"},
					{.m_Value = 7, .m_Name = "Session Switch Safety"},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = TaskCountId,
			.m_Name = "Task Count",
			.m_Group = "Verification",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(32),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(uint32_t(512)),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = WorkUnitsId,
			.m_Name = "Work Units",
			.m_Group = "Verification",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(8),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(uint32_t(64)),
			}));
		ApplyImmediateParameters();
	}

	void TaskSystemLabSession::OnEnter() noexcept
	{
		m_Entered = true;
		StartScenario();
	}

	void TaskSystemLabSession::OnExit() noexcept
	{
		m_Entered = false;
		CancelScenario();
	}

	void TaskSystemLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Finished)
		{
			return;
		}

		m_ElapsedSeconds += deltaTime;
		TaskSystem& taskSystem = *m_Services.m_TaskSystem;
		const TaskSystemStatistics statistics = taskSystem.GetStatistics();
		m_State->m_MaxCompletionBacklog =
			std::max(m_State->m_MaxCompletionBacklog, statistics.m_PendingCompletionCount);

		if ((m_Scenario == Scenario::PriorityOrdering || m_Scenario == Scenario::CancelQueued) &&
			!m_GatedTargetsSubmitted &&
			m_State->m_GateStartedCount.load(std::memory_order_acquire) >= m_State->m_WorkerCount)
		{
			SubmitGatedTargets();
		}

		if (m_GatedTargetsSubmitted && !m_AllGatesReleased &&
			m_State->m_TargetCompletedCount >= m_State->m_RequestedTargetCount)
		{
			m_State->m_GateReleasePermits.store(m_State->m_WorkerCount, std::memory_order_release);
			m_AllGatesReleased = true;
		}

		if (m_Scenario == Scenario::CancelRunning && !m_CancelRequested &&
			m_State->m_TargetStartedCount.load(std::memory_order_acquire) >=
			m_State->m_RequestedTargetCount)
		{
			for (const TaskHandle handle : m_Handles)
			{
				GGLAB_UNUSED(taskSystem.Cancel(handle));
			}
			m_CancelRequested = true;
		}

		if (m_Scenario != Scenario::SessionSwitchSafety && m_State->m_ExpectedCompletions > 0 &&
			m_State->m_CompletedCount >= m_State->m_ExpectedCompletions)
		{
			EvaluateScenario();
		}
		else if (m_Scenario != Scenario::SessionSwitchSafety && m_ElapsedSeconds > 10.0f)
		{
			m_State->m_TimedOut = true;
			m_State->m_Finished = true;
			m_State->m_Passed = false;
			m_State->m_Errors.push_back("Scenario exceeded the 10 second timeout.");
			GGLAB_LOG_ERROR("TaskSystem Lab generation {} ({}) timed out.", m_State->m_Generation,
				ScenarioText(m_State->m_Scenario));
			m_State->m_GateReleasePermits.store(m_State->m_WorkerCount, std::memory_order_release);
			for (const TaskHandle handle : m_Handles)
			{
				GGLAB_UNUSED(taskSystem.Cancel(handle));
			}
		}
	}

	void TaskSystemLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "TaskSystem Verification";
		if (!m_State)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Scenario state",
				.m_Status = LabDiagnosticCheckStatus::Pending,
				.m_Detail = "No active scenario.",
				});
			return;
		}

		const auto& state = *m_State;
		diagnostics.m_Metrics = {
			{.m_Name = "Scenario", .m_Value = ScenarioText(state.m_Scenario)},
			{.m_Name = "Generation", .m_Value = std::to_string(state.m_Generation)},
			{.m_Name = "Workers", .m_Value = std::to_string(state.m_WorkerCount)},
			{.m_Name = "Submitted / completed",
				.m_Value = std::format("{} / {}", state.m_SubmittedCount, state.m_CompletedCount)},
			{.m_Name = "Succeeded / failed / cancelled",
				.m_Value = std::format("{} / {} / {}", state.m_SucceededCount, state.m_FailedCount,
					state.m_CancelledCount)},
			{.m_Name = "Started / target executed",
				.m_Value =
					std::format("{} / {}", state.m_StartedCount.load(std::memory_order_relaxed),
						state.m_TargetExecutedCount.load(std::memory_order_relaxed))},
			{.m_Name = "Max completion backlog",
				.m_Value = std::to_string(state.m_MaxCompletionBacklog)},
			{.m_Name = "Max queue / execution",
				.m_Value = std::format("{:.3f} / {:.3f} ms", state.m_MaxQueueMilliseconds,
					state.m_MaxExecutionMilliseconds)},
		};

		if (state.m_Scenario == Scenario::SessionSwitchSafety)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Switch-away lifetime safety",
				.m_Status = LabDiagnosticCheckStatus::Pending,
				.m_Detail =
					"Switch to another Lab while these tasks are running. OnExit cancels all handles; workers and completions do not capture the session object.",
				});
		}
		else
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Scenario result",
				.m_Status = !state.m_Finished ? LabDiagnosticCheckStatus::Pending
							: state.m_Passed ? LabDiagnosticCheckStatus::Passed
											  : LabDiagnosticCheckStatus::Failed,
				.m_Detail = state.m_Finished
								? (state.m_Passed ? "All scenario invariants passed."
												  : "One or more scenario invariants failed.")
								: "Scenario is running.",
				});
		}
		diagnostics.m_Checks.push_back({
			.m_Name = "Owner-thread completions",
			.m_Status = state.m_WrongThreadCompletionCount > 0 ? LabDiagnosticCheckStatus::Failed
						: state.m_Finished ? LabDiagnosticCheckStatus::Passed
															   : LabDiagnosticCheckStatus::Pending,
			.m_Detail =
				std::format("Wrong-thread callbacks: {}", state.m_WrongThreadCompletionCount),
			});
		if (state.m_Scenario == Scenario::PriorityOrdering)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Priority ordering",
				.m_Status = state.m_OrderingViolationCount > 0 ? LabDiagnosticCheckStatus::Failed
							: state.m_Finished ? LabDiagnosticCheckStatus::Passed
															   : LabDiagnosticCheckStatus::Pending,
				.m_Detail = std::format("Ordering violations: {}", state.m_OrderingViolationCount),
				});
		}
		if (state.m_Finished && !state.m_Passed)
		{
			for (const std::string& error : state.m_Errors)
			{
				diagnostics.m_Checks.push_back({
					.m_Name = "Detail",
					.m_Status = LabDiagnosticCheckStatus::Failed,
					.m_Detail = error,
					});
			}
		}
	}

	void TaskSystemLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_Scenario = static_cast<Scenario>(parameters.Get(ScenarioId, int32_t(0)));
		m_TaskCount = parameters.Get(TaskCountId, uint32_t(32));
		m_WorkUnits = parameters.Get(WorkUnitsId, uint32_t(8));
		if (m_Entered)
		{
			StartScenario();
		}
	}

	void TaskSystemLabSession::StartScenario() noexcept
	{
		CancelScenario();
		m_State = std::make_shared<ScenarioState>();
		m_State->m_Scenario = m_Scenario;
		m_State->m_Generation = m_NextGeneration++;
		m_State->m_OwnerThread = std::this_thread::get_id();
		m_State->m_WorkerCount = m_Services.m_TaskSystem->GetStatistics().m_WorkerCount;
		m_ElapsedSeconds = 0.0f;
		m_GatedTargetsSubmitted = false;
		m_CancelRequested = false;
		m_AllGatesReleased = false;

		const uint32_t targetCount =
			m_Scenario == Scenario::CompletionBacklog ? std::max(m_TaskCount, 256u) : m_TaskCount;
		m_State->m_RequestedTargetCount = targetCount;

		if (m_Scenario == Scenario::PriorityOrdering || m_Scenario == Scenario::CancelQueued)
		{
			for (uint32_t worker = 0; worker < m_State->m_WorkerCount; ++worker)
			{
				auto state = m_State;
				SubmitTask(
					{
						.m_Name = std::format("TaskLab/{}/Gate/{}", state->m_Generation, worker),
						.m_Priority = TaskPriority::Critical,
					},
					[state](std::stop_token stopToken) noexcept
					{
						state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_GateStartedCount.fetch_add(1, std::memory_order_release);
						GGLAB_UNUSED(AcquireGatePermit(stopToken, state->m_GateReleasePermits));
						return TaskResult::Success();
					},
					false);
			}
			return;
		}

		const uint32_t submittedTargetCount =
			m_Scenario == Scenario::CancelRunning || m_Scenario == Scenario::SessionSwitchSafety
			? std::min(targetCount, std::max(m_State->m_WorkerCount, 1u))
			: targetCount;
		m_State->m_RequestedTargetCount = submittedTargetCount;
		for (uint32_t index = 0; index < submittedTargetCount; ++index)
		{
			auto state = m_State;
			TaskDesc desc{
				.m_Name = std::format("TaskLab/{}/Target/{}", state->m_Generation, index),
				.m_Priority = TaskPriority::Normal,
				.m_Progress = std::make_shared<ProgressChannel>(),
			};
			if (m_Scenario == Scenario::ExplicitFailure)
			{
				SubmitTask(
					std::move(desc),
					[state](std::stop_token) noexcept
					{
						state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetStartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetExecutedCount.fetch_add(1, std::memory_order_relaxed);
						return TaskResult::Failure("Intentional TaskSystem Lab failure.");
					},
					true);
			}
			else if (m_Scenario == Scenario::ExceptionFailure)
			{
				SubmitTask(
					std::move(desc),
					[state](std::stop_token) -> TaskResult
					{
						state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetStartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetExecutedCount.fetch_add(1, std::memory_order_relaxed);
						throw std::runtime_error("Intentional TaskSystem Lab exception.");
					},
					true);
			}
			else if (m_Scenario == Scenario::CancelRunning ||
				m_Scenario == Scenario::SessionSwitchSafety)
			{
				SubmitTask(
					std::move(desc),
					[state](std::stop_token stopToken) noexcept
					{
						state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetStartedCount.fetch_add(1, std::memory_order_release);
						state->m_TargetExecutedCount.fetch_add(1, std::memory_order_relaxed);
						while (!stopToken.stop_requested())
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(1));
						}
						return TaskResult::Success();
					},
					true);
			}
			else
			{
				const uint32_t workUnits =
					m_Scenario == Scenario::CompletionBacklog ? 1u : m_WorkUnits;
				const ProgressChannelPtr taskProgress = desc.m_Progress;
				SubmitTask(
					std::move(desc),
					[state, workUnits, progress = taskProgress](std::stop_token stopToken) noexcept
					{
						state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetStartedCount.fetch_add(1, std::memory_order_relaxed);
						state->m_TargetExecutedCount.fetch_add(1, std::memory_order_relaxed);
						return RunDeterministicWork(stopToken, workUnits, state->m_Checksum,
							ProgressReporter(progress, 0.05f, 0.98f));
					},
					true);
			}
		}
	}

	void TaskSystemLabSession::CancelScenario() noexcept
	{
		if (m_State)
		{
			m_State->m_GateReleasePermits.store(
				std::max(m_State->m_WorkerCount, 1u), std::memory_order_release);
		}
		if (m_Services.m_TaskSystem)
		{
			for (const TaskHandle handle : m_Handles)
			{
				GGLAB_UNUSED(m_Services.m_TaskSystem->Cancel(handle));
			}
		}
		m_Handles.clear();
		m_State.reset();
	}

	void TaskSystemLabSession::SubmitGatedTargets() noexcept
	{
		GGLAB_ASSERT(m_State);
		m_GatedTargetsSubmitted = true;
		const uint32_t targetCount = m_Scenario == Scenario::PriorityOrdering
			? std::max(m_State->m_RequestedTargetCount, 4u)
			: m_State->m_RequestedTargetCount;
		m_State->m_RequestedTargetCount = targetCount;

		for (uint32_t index = 0; index < targetCount; ++index)
		{
			auto state = m_State;
			auto progress = std::make_shared<ProgressChannel>();
			const TaskPriority priority =
				m_Scenario == Scenario::PriorityOrdering
				? PrioritySubmissionOrder[index % PrioritySubmissionOrder.size()]
				: TaskPriority::Normal;
			if (m_Scenario == Scenario::PriorityOrdering)
			{
				std::scoped_lock lock(state->m_PriorityMutex);
				++state->m_PriorityRemaining[static_cast<size_t>(priority)];
			}

			const TaskHandle handle = SubmitTask(
				{
					.m_Name = std::format("TaskLab/{}/Target/{}", state->m_Generation, index),
					.m_Priority = priority,
					.m_Progress = progress,
				},
				[state, priority, scenario = m_Scenario, workUnits = m_WorkUnits, progress](
					std::stop_token stopToken) noexcept
				{
					state->m_StartedCount.fetch_add(1, std::memory_order_relaxed);
					state->m_TargetStartedCount.fetch_add(1, std::memory_order_relaxed);
					state->m_TargetExecutedCount.fetch_add(1, std::memory_order_relaxed);
					if (scenario == Scenario::PriorityOrdering)
					{
						std::scoped_lock lock(state->m_PriorityMutex);
						const size_t priorityIndex = static_cast<size_t>(priority);
						for (size_t higher = 0; higher < priorityIndex; ++higher)
						{
							if (state->m_PriorityRemaining[higher] > 0)
							{
								++state->m_OrderingViolationCount;
								break;
							}
						}
						GGLAB_ASSERT(state->m_PriorityRemaining[priorityIndex] > 0);
						--state->m_PriorityRemaining[priorityIndex];
					}
					return RunDeterministicWork(stopToken, workUnits, state->m_Checksum,
						ProgressReporter(progress, 0.05f, 0.98f));
				},
				true);

			if (m_Scenario == Scenario::CancelQueued && handle.IsValid())
			{
				if (!m_Services.m_TaskSystem->Cancel(handle))
				{
					m_State->m_Errors.push_back("Failed to cancel a queued target task.");
				}
			}
		}
		m_State->m_GateReleasePermits.store(1, std::memory_order_release);
	}

	void TaskSystemLabSession::EvaluateScenario() noexcept
	{
		GGLAB_ASSERT(m_State);
		auto& state = *m_State;
		if (state.m_Finished)
		{
			return;
		}

		bool passed = state.m_SubmitFailureCount == 0 && state.m_WrongThreadCompletionCount == 0 &&
			state.m_CompletedCount == state.m_ExpectedCompletions;
		switch (state.m_Scenario)
		{
		case Scenario::BurstSuccess:
		case Scenario::CompletionBacklog:
			passed &= state.m_SucceededCount == state.m_ExpectedCompletions;
			if (state.m_Scenario == Scenario::CompletionBacklog)
			{
				passed &= state.m_MaxCompletionBacklog > 0;
			}
			break;
		case Scenario::PriorityOrdering:
			passed &= state.m_SucceededCount == state.m_ExpectedCompletions &&
				state.m_OrderingViolationCount == 0;
			break;
		case Scenario::CancelQueued:
			passed &= state.m_TargetCancelledCount == state.m_RequestedTargetCount &&
				state.m_TargetExecutedCount.load(std::memory_order_relaxed) == 0;
			break;
		case Scenario::CancelRunning:
			passed &= state.m_TargetCancelledCount == state.m_RequestedTargetCount &&
				state.m_TargetStartedCount.load(std::memory_order_relaxed) ==
				state.m_RequestedTargetCount;
			break;
		case Scenario::ExplicitFailure:
		case Scenario::ExceptionFailure:
			passed &= state.m_FailedCount == state.m_ExpectedCompletions &&
				state.m_Errors.size() >= state.m_RequestedTargetCount;
			break;
		case Scenario::SessionSwitchSafety:
			return;
		}
		state.m_Passed = passed;
		state.m_Finished = true;
		if (passed)
		{
			GGLAB_LOG_INFO("TaskSystem Lab generation {} ({}) PASS: submitted={}, completed={}.",
				state.m_Generation, ScenarioText(state.m_Scenario), state.m_SubmittedCount,
				state.m_CompletedCount);
		}
		else
		{
			GGLAB_LOG_ERROR(
				"TaskSystem Lab generation {} ({}) FAIL: submitted={}, completed={}, orderingViolations={}, wrongThreadCallbacks={}.",
				state.m_Generation, ScenarioText(state.m_Scenario), state.m_SubmittedCount,
				state.m_CompletedCount, state.m_OrderingViolationCount,
				state.m_WrongThreadCompletionCount);
		}
	}

	TaskHandle TaskSystemLabSession::SubmitTask(
		TaskDesc desc, TaskWork work, bool targetTask) noexcept
	{
		if (!desc.m_Progress)
		{
			desc.m_Progress = std::make_shared<ProgressChannel>();
		}
		const ProgressReporter progress(desc.m_Progress);
		progress.Report(0.0f, "Verification task queued", desc.m_Name);
		TaskWork instrumentedWork = [work = std::move(work), progress](
			std::stop_token stopToken) mutable -> TaskResult
			{
				progress.Report(0.03f, "Verification task running");
				try
				{
					TaskResult result = work(stopToken);
					progress.Report(0.99f,
						result.m_Succeeded ? "Verification task work complete"
						: "Verification task failed",
						result.m_Error);
					return result;
				}
				catch (const std::exception& exception)
				{
					progress.Report(0.99f, "Verification task threw", exception.what());
					throw;
				}
			};
		std::weak_ptr<ScenarioState> weakState = m_State;
		const TaskHandle handle =
			m_Services.m_TaskSystem->Submit(std::move(desc), std::move(instrumentedWork),
				[weakState, targetTask](const TaskCompletionInfo& info)
				{
					const auto state = weakState.lock();
					if (!state)
					{
						return;
					}
					++state->m_CompletedCount;
					if (targetTask)
						++state->m_TargetCompletedCount;
					if (std::this_thread::get_id() != state->m_OwnerThread)
					{
						++state->m_WrongThreadCompletionCount;
					}
					state->m_MaxQueueMilliseconds =
						std::max(state->m_MaxQueueMilliseconds, info.m_QueueMilliseconds);
					state->m_MaxExecutionMilliseconds =
						std::max(state->m_MaxExecutionMilliseconds, info.m_ExecutionMilliseconds);
					switch (info.m_Status)
					{
					case TaskStatus::Succeeded:
						++state->m_SucceededCount;
						break;
					case TaskStatus::Failed:
						++state->m_FailedCount;
						state->m_Errors.push_back(info.m_Error.empty()
							? "Task failed without an error message."
							: info.m_Error);
						break;
					case TaskStatus::Cancelled:
						++state->m_CancelledCount;
						if (targetTask)
							++state->m_TargetCancelledCount;
						break;
					default:
						break;
					}
				});
		if (handle.IsValid())
		{
			m_Handles.push_back(handle);
			++m_State->m_SubmittedCount;
			++m_State->m_ExpectedCompletions;
		}
		else
		{
			++m_State->m_SubmitFailureCount;
			m_State->m_Errors.push_back("TaskSystem rejected a verification task.");
		}
		return handle;
	}

	LabId TaskSystemLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.task_system");
	}

	LabDescriptor TaskSystemLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Task System Lab",
			.m_Category = "Systems",
			.m_Description =
				"Deterministic scenarios for TaskSystem priority, cancellation, failure, completion budgeting, and session lifetime safety.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> TaskSystemLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<TaskSystemLabSession>(createInfo);
	}
}
