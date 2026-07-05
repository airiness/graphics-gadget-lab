#pragma once
#include "Application/Lab/LabRunConfig.h"
#include "Application/Lab/LabTypes.h"
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

#include <optional>

namespace gglab
{
	class ILabControl;
	class ILabSnapshotSource;
	struct LabParameterSnapshot;

	class LabPanel final : public DevelopGuiPanelBase
	{
	public:
		LabPanel(ILabControl* control, ILabSnapshotSource* snapshotSource) noexcept :
			m_Control(control),
			m_SnapshotSource(snapshotSource)
		{}

		std::string_view GetPath() const noexcept override { return "Application/Lab"; }
		std::string_view GetTitle() const noexcept override { return "Lab Control"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		int32_t GetOrder() const noexcept override { return -90; }
		bool IsDefaultOpen() const noexcept override { return true; }

	private:
		bool DrawParameter(const LabParameterSnapshot& parameter) noexcept;

		ILabControl* m_Control = nullptr;
		ILabSnapshotSource* m_SnapshotSource = nullptr;
		std::optional<LabRunConfig> m_RunConfigDraft;
		LabId m_RunConfigLabId;
	};
}
