#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldObservation.h"

#include <string>

namespace gglab
{
	class GGLabAppRuntime;

	class GIProbeInspectionControlBase
	{
	public:
		virtual ~GIProbeInspectionControlBase() = default;
		[[nodiscard]] virtual bool Inspect(GIProbeInspectionRequest request) noexcept = 0;
		virtual void Cancel() noexcept = 0;
		[[nodiscard]] virtual bool IsPending() const noexcept = 0;
		[[nodiscard]] virtual std::shared_ptr<const GIProbeInspectionResult> GetResult() const noexcept = 0;
		[[nodiscard]] virtual const std::string& GetStatus() const noexcept = 0;
	};

	// AppRuntime's GUI client owns only its own job/receipt. Session jobs are independent.
	class ToolingGIProbeInspection final : public GIProbeInspectionControlBase
	{
	public:
		explicit ToolingGIProbeInspection(GGLabAppRuntime& runtime) noexcept : m_Runtime(runtime) {}
		[[nodiscard]] bool Inspect(GIProbeInspectionRequest request) noexcept override;
		void Cancel() noexcept override;
		void Update() noexcept;
		[[nodiscard]] bool IsPending() const noexcept override { return m_JobId != 0; }
		[[nodiscard]] std::shared_ptr<const GIProbeInspectionResult> GetResult() const noexcept override
		{
			return m_Result;
		}
		[[nodiscard]] const std::string& GetStatus() const noexcept override { return m_Status; }

	private:
		GGLabAppRuntime& m_Runtime;
		uint64_t m_JobId = 0;
		bool m_Discard = false;
		std::shared_ptr<const GIProbeInspectionResult> m_Result;
		std::string m_Status = "No probe inspection requested.";
	};
}
