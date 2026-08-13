#pragma once
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

#include <optional>

namespace gglab
{
	class LabRuntimeLocatorBase;
	class LabPanel final : public DevelopGuiPanelBase
	{
	public:
		explicit LabPanel(LabRuntimeLocatorBase* runtimeLocator) noexcept :
			m_RuntimeLocator(runtimeLocator)
		{
		}

		std::string_view GetPath() const noexcept override { return "Application/Lab"; }
		std::string_view GetTitle() const noexcept override { return "Lab Control"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		int32_t GetOrder() const noexcept override { return -90; }
		bool IsDefaultOpen() const noexcept override { return true; }

	private:
		bool DrawParameter(const LabIdSnapshot& activeLabId,
			const LabParameterSnapshot& parameter) noexcept;

		struct DeferredParameterEdit
		{
			LabIdSnapshot m_LabId;
			LabParameterIdSnapshot m_ParameterId;
			LabSnapshotValue m_Value = false;
		};

		LabRuntimeLocatorBase* m_RuntimeLocator = nullptr;
		std::optional<LabRunConfigSnapshot> m_RunConfigDraft;
		std::vector<DeferredParameterEdit> m_DeferredParameterEdits;
		LabIdSnapshot m_RunConfigLabId;
	};
}
