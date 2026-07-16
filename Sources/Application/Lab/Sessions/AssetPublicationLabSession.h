#pragma once
#include "Application/Lab/LabSessionBase.h"
#include "Graphics/AssetUploadScheduler.h"

namespace gglab
{
	class AssetPublicationLabSession final : public LabSessionBase
	{
	public:
		enum class Scenario : int32_t
		{
			IncrementalSuccess,
			CancelTextures,
			FailMaterials,
			CancelMeshes,
			FailMeshInstances,
			CancelDependencies,
		};

		explicit AssetPublicationLabSession(
			const LabSessionCreateInfo& createInfo) noexcept;
		~AssetPublicationLabSession() override = default;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(
			LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		struct ScenarioState;

		void ApplyImmediateParameters() noexcept override;
		void StartScenario() noexcept;
		void StopScenario() noexcept;
		void EvaluateScenario(const AssetUploadStatistics& statistics) noexcept;

		Scenario m_Scenario = Scenario::IncrementalSuccess;
		uint32_t m_FaultOccurrence = 1;
		std::unique_ptr<ScenarioState> m_State;
		AssetStreamingFrameBudget m_OriginalBudget{};
		bool m_HasOriginalBudget = false;
		bool m_Entered = false;
	};
}
