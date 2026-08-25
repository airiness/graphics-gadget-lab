#pragma once
#include "Lab/LabSessionBase.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"

namespace gglab
{
	class AssetPublicationLabSession final : public LabSessionBase
	{
	public:
		enum class Scenario : int32_t
		{
			AcceptanceSuite,
			IncrementalSuccess,
			CancelTextures,
			CancelMaterials,
			CancelMeshes,
			CancelMeshInstances,
			CancelDependencies,
			CancelBeforeCommit,
			FailMaterials,
		};

		explicit AssetPublicationLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AssetPublicationLabSession() override = default;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		struct ScenarioState;
		struct AcceptanceSuiteState;

		void ApplyImmediateParameters() noexcept override;
		void StartScenario() noexcept;
		void StartModelScenario(Scenario scenario, uint32_t faultOccurrence) noexcept;
		void StopScenario() noexcept;
		void UpdateModelScenario(float deltaTime) noexcept;
		void EvaluateScenario(const AssetUploadStatistics& statistics) noexcept;
		void StartAcceptanceSuite() noexcept;
		void UpdateAcceptanceSuite(float deltaTime) noexcept;
		void StartAcceptanceCase() noexcept;
		void CompleteAcceptanceCase(std::string name, std::vector<std::string> errors) noexcept;
		void CompleteAcceptanceSuite() noexcept;

		Scenario m_Scenario = Scenario::AcceptanceSuite;
		uint32_t m_FaultOccurrence = 1;
		std::unique_ptr<ScenarioState> m_State;
		std::unique_ptr<AcceptanceSuiteState> m_Suite;
		AssetStreamingFrameBudget m_OriginalBudget{};
		bool m_HasOriginalBudget = false;
		bool m_Entered = false;
	};
}
