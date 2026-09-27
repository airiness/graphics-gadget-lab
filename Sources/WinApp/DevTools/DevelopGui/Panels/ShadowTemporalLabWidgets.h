#pragma once

#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	struct DevelopGuiContext;

	void DrawShadowTemporalLabValidation(DevelopGuiContext& context) noexcept;

	class ShadowStairsValidationPanel final : public DevelopGuiPanelBase
	{
	public:
		std::string_view GetPath() const noexcept override
		{
			return "Application/Lab/Shadow Stairs Validation";
		}
		std::string_view GetTitle() const noexcept override { return "Shadow Stairs Validation"; }
		void Draw(DevelopGuiContext& context) noexcept override;
	};
}
