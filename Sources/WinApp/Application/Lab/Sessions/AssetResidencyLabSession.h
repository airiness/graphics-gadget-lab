#pragma once
#include "Lab/LabSessionBase.h"

namespace gglab
{
	// GPU acceptance for residency on the active device: fence-completed release,
	// reload to a generation-safe resident view and retirement. Residency policy,
	// eviction, cache and DDC contracts are covered headlessly by the asset-residency suite.
	class AssetResidencyLabSession final : public LabSessionBase
	{
	public:
		explicit AssetResidencyLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~AssetResidencyLabSession() override;

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

		void Fail(std::string error) noexcept;
		void Complete() noexcept;

		std::unique_ptr<State> m_State;
	};
}
