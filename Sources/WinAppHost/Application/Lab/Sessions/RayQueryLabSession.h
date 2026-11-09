#pragma once
#include "Lab/LabSessionBase.h"

#include <memory>

namespace gglab
{
	struct RayQueryLabState;
	class RayQueryLabSession final : public LabSessionBase
	{
	public:
		explicit RayQueryLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~RayQueryLabSession() override = default;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;
		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(const LabSessionCreateInfo& createInfo) noexcept;

	private:
		std::shared_ptr<RayQueryLabState> m_State;
	};
}
