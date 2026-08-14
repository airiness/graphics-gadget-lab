#pragma once
#include "DevTools/DevelopGui/DevelopGuiPanel.h"

namespace gglab
{
	class DemoManager;

	class DemoPanel final : public DevelopGuiPanelBase
	{
	public:
		explicit DemoPanel(DemoManager* demoManager) noexcept : m_DemoManager(demoManager) {}

		std::string_view GetPath() const noexcept override { return "Application/Demo"; }
		std::string_view GetTitle() const noexcept override { return "Demo Selection"; }
		void Draw(DevelopGuiContext& context) noexcept override;
		int32_t GetOrder() const noexcept override { return -100; }
		bool IsDefaultOpen() const noexcept override { return true; }

	private:
		DemoManager* m_DemoManager = nullptr;
	};
}
