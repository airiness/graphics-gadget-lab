#pragma once
#include "Application/Lab/LabParameter.h"
#include "Application/Lab/LabRunConfig.h"
#include "Application/Lab/LabTypes.h"
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

#include <optional>

namespace gglab
{
	class LabRuntimeLocatorBase;
	struct LabParameterSnapshot;

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
		bool DrawParameter(
			const LabId& activeLabId, const LabParameterSnapshot& parameter) noexcept;

		struct DeferredParameterEdit
		{
			LabId m_LabId;
			LabParameterId m_ParameterId;
			LabValue m_Value = false;
		};

		LabRuntimeLocatorBase* m_RuntimeLocator = nullptr;
		std::optional<LabRunConfig> m_RunConfigDraft;
		std::vector<DeferredParameterEdit> m_DeferredParameterEdits;
		LabId m_RunConfigLabId;
	};
}
