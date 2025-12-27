#include "Precompiled.h"
#include "DemoManager.h"

namespace gglab
{
	DemoBase* DemoManager::GetDemo(uint32_t index) const noexcept
	{
		if (index >= m_Demos.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: GetDemo, invalid index:{}, size:{}.",
				index, m_Demos.size());

			return nullptr;
		}

		return m_Demos[index].get();
	}

	void DemoManager::SetActiveDemo(uint32_t index) noexcept
	{
		if (index >= m_Demos.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: SetActiveDemo, invalid index:{}, size:{}.",
				index, m_Demos.size());

			return;
		}

		DemoBase* selectedDemo = m_Demos[index].get();
		if (selectedDemo == m_ActiveDemo)
		{
			GGLAB_LOG_GRAPHICS_INFO(
				"DemoManager: SetActive, Selected same demo with current.");
			return;
		}

		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnExit();
		}

		m_ActiveDemo = selectedDemo;
		m_ActiveDemoIndex = index;

		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnEnter();
		}
	}

	void DemoManager::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_ActiveDemo)
		{
			return;
		}

		m_ActiveDemo->OnResize(width, height);
	}
}