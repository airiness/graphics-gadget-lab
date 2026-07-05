#include "Core/Precompiled.h"
#include "Application/Demo/DemoManager.h"

namespace gglab
{
	DemoManager::~DemoManager()
	{
		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnExit();
		}
	}

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

	void DemoManager::RequestActiveDemo(uint32_t index) noexcept
	{
		if (index >= m_Demos.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: RequestActiveDemo, invalid index:{}, size:{}.",
				index, m_Demos.size());

			return;
		}
		m_PendingActiveDemoIndex = index == m_ActiveDemoIndex ? InvalidDemoIndex : index;
	}

	void DemoManager::ApplyPendingActiveDemo() noexcept
	{
		if (m_PendingActiveDemoIndex == InvalidDemoIndex)
		{
			return;
		}

		const uint32_t requestedIndex = m_PendingActiveDemoIndex;
		m_PendingActiveDemoIndex = InvalidDemoIndex;
		SetActiveDemo(requestedIndex);
	}

	void DemoManager::SetActiveDemo(uint32_t index) noexcept
	{
		GGLAB_ASSERT(index < m_Demos.size());
		if (index >= m_Demos.size())
		{
			return;
		}

		DemoBase* selectedDemo = m_Demos[index].get();
		if (selectedDemo == m_ActiveDemo)
		{
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
			if (m_WindowWidth > 0 && m_WindowHeight > 0)
			{
				m_ActiveDemo->OnResize(m_WindowWidth, m_WindowHeight);
			}
		}
	}

	void DemoManager::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_WindowWidth = width;
		m_WindowHeight = height;
		if (!m_ActiveDemo)
		{
			return;
		}

		m_ActiveDemo->OnResize(width, height);
	}
}
