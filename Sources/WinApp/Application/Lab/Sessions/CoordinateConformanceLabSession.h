#pragma once
#include "Lab/LabSessionBase.h"

#include <memory>

namespace gglab
{
	struct CoordinateConformanceLabState;

	class CoordinateConformanceLabSession final : public LabSessionBase
	{
	public:
		explicit CoordinateConformanceLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~CoordinateConformanceLabSession() override = default;

		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		std::shared_ptr<CoordinateConformanceLabState> m_State;
	};
}
