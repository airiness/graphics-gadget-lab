#pragma once
#include "Lab/LabSessionBase.h"

namespace gglab
{
	class EnvironmentAssetLabSession final : public LabSessionBase
	{
	public:
		explicit EnvironmentAssetLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~EnvironmentAssetLabSession() override = default;

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
