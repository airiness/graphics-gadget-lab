#pragma once
#include "Lab/LabSessionBase.h"

#include <memory>

namespace gglab
{
	struct RenderGraphComputeLabState;

	class RenderGraphComputeLabSession final : public LabSessionBase
	{
	public:
		explicit RenderGraphComputeLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~RenderGraphComputeLabSession() override = default;

		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;

		std::shared_ptr<RenderGraphComputeLabState> m_State;
		float m_AnimationTimeSeconds = 0.0f;
	};
}
