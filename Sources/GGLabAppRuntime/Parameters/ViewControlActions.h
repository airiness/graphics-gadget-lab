#pragma once
#include "Lab/LabParameter.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/ParameterBatch.h"

#include <optional>
#include <string>

namespace gglab
{
	class GGLabAppRuntime;

	struct ViewOverrideReleaseStatus
	{
		LabParameterId m_Parameter;
		HostViewControlStamp m_Expected;
		std::optional<ParameterBatchStatus> m_Batch;
		std::string m_Error;
		std::optional<ParameterBatchOwner> m_BlockingOwner;

		[[nodiscard]] bool IsPending() const noexcept
		{
			return m_Batch && !m_Batch->IsTerminal();
		}
	};

	class ViewControlActionsBase
	{
	public:
		virtual ~ViewControlActionsBase() = default;
		// The caller supplies the displayed observation's complete stamp, not a fresh query.
		[[nodiscard]] virtual bool ReleaseOverride(const HostViewControlStamp& expected,
			const LabParameterId& parameter, LabParameterType type) noexcept = 0;
		[[nodiscard]] virtual const ViewOverrideReleaseStatus& GetReleaseStatus() const noexcept = 0;
	};

	// One in-process client of SessionParameterControl, owned by AppRuntime.
	class ToolingViewControl final : public ViewControlActionsBase
	{
	public:
		ToolingViewControl(const ToolingViewControl&) = delete;
		ToolingViewControl& operator=(const ToolingViewControl&) = delete;
		[[nodiscard]] bool ReleaseOverride(const HostViewControlStamp& expected,
			const LabParameterId& parameter, LabParameterType type) noexcept override;
		[[nodiscard]] const ViewOverrideReleaseStatus& GetReleaseStatus() const noexcept override
		{
			return m_Status;
		}

	private:
		friend class GGLabAppRuntime;
		explicit ToolingViewControl(GGLabAppRuntime& runtime) noexcept : m_Runtime(runtime) {}
		void Update() noexcept;
		GGLabAppRuntime& m_Runtime;
		ViewOverrideReleaseStatus m_Status;
	};
}
