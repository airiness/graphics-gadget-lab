#pragma once
#include "Application/Lab/LabSessionBase.h"
#include "Core/Task/TaskTypes.h"

namespace gglab
{
	class TaskSystemLabSession final : public LabSessionBase
	{
	public:
		explicit TaskSystemLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~TaskSystemLabSession() override = default;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	public:
		enum class Scenario : int32_t
		{
			BurstSuccess,
			PriorityOrdering,
			CancelQueued,
			CancelRunning,
			ExplicitFailure,
			ExceptionFailure,
			CompletionBacklog,
			SessionSwitchSafety,
		};

	private:
		struct ScenarioState;

		void ApplyImmediateParameters() noexcept override;
		void StartScenario() noexcept;
		void CancelScenario() noexcept;
		void SubmitGatedTargets() noexcept;
		void EvaluateScenario() noexcept;
		TaskHandle SubmitTask(TaskDesc desc, TaskWork work, bool targetTask) noexcept;

		std::shared_ptr<ScenarioState> m_State;
		std::vector<TaskHandle> m_Handles;
		Scenario m_Scenario = Scenario::BurstSuccess;
		uint32_t m_TaskCount = 32;
		uint32_t m_WorkUnits = 8;
		uint64_t m_NextGeneration = 1;
		float m_ElapsedSeconds = 0.0f;
		bool m_Entered = false;
		bool m_GatedTargetsSubmitted = false;
		bool m_CancelRequested = false;
		bool m_AllGatesReleased = false;
	};
}
