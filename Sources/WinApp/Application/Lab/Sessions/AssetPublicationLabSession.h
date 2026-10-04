#pragma once
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/Asset/AssetStreamingTypes.h"

#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	// GPU acceptance for asset publication on the active device: incremental model
	// publication through real uploads, a GPU-submitted cancellation finalized by a
	// real fence, and upload conservation. Publication transaction, rollback and
	// ownership contracts are covered headlessly by the asset-publication suite.
	class AssetPublicationLabSession final : public LabSessionBase
	{
	public:
		explicit AssetPublicationLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AssetPublicationLabSession() override;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		struct State;

		void StartIncrementalPublication() noexcept;
		void UpdateIncrementalPublication() noexcept;
		void StartGpuCancellation() noexcept;
		void UpdateGpuCancellation() noexcept;
		void CompleteCase(std::string name, std::vector<std::string> errors) noexcept;
		void CompleteSuite() noexcept;
		void Stop() noexcept;

		std::unique_ptr<State> m_State;
		AssetStreamingFrameBudget m_OriginalBudget{};
		bool m_HasOriginalBudget = false;
	};
}
