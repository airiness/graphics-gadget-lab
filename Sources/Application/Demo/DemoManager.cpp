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
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: GetDemo, invalid index:{}, size:{}.",
				index, m_DemoSlots.size());

			return nullptr;
		}

		return m_DemoSlots[index].m_Instance.get();
	}

	std::string_view DemoManager::GetDemoName(uint32_t index) const noexcept
	{
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: GetDemoName, invalid index:{}, size:{}.",
				index, m_DemoSlots.size());
			return {};
		}
		return m_DemoSlots[index].m_Name;
	}

	bool DemoManager::IsDemoCreated(uint32_t index) const noexcept
	{
		return index < m_DemoSlots.size() && m_DemoSlots[index].m_Instance != nullptr;
	}

	uint32_t DemoManager::RegisterDemo(std::string name, DemoFactory factory) noexcept
	{
		if (name.empty() || !factory)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DemoManager: cannot register an invalid demo slot.");
			return InvalidDemoIndex;
		}

		const uint32_t registeredIndex = static_cast<uint32_t>(m_DemoSlots.size());
		m_DemoSlots.push_back({
			.m_Name = std::move(name),
			.m_Factory = std::move(factory),
		});
		return registeredIndex;
	}

	void DemoManager::RequestActiveDemo(uint32_t index) noexcept
	{
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: RequestActiveDemo, invalid index:{}, size:{}.",
				index, m_DemoSlots.size());

			return;
		}
		m_PendingActiveDemoIndex = index == m_ActiveDemoIndex ? InvalidDemoIndex : index;
	}

	bool DemoManager::ApplyPendingActiveDemo() noexcept
	{
		if (m_PendingActiveDemoIndex == InvalidDemoIndex)
		{
			return true;
		}

		const uint32_t requestedIndex = m_PendingActiveDemoIndex;
		m_PendingActiveDemoIndex = InvalidDemoIndex;
		return SetActiveDemo(requestedIndex);
	}

	DemoBase* DemoManager::EnsureDemoCreated(uint32_t index) noexcept
	{
		GGLAB_ASSERT(index < m_DemoSlots.size());
		if (index >= m_DemoSlots.size())
		{
			return nullptr;
		}

		DemoSlot& slot = m_DemoSlots[index];
		if (slot.m_Instance)
		{
			return slot.m_Instance.get();
		}

		slot.m_Instance = slot.m_Factory();
		if (!slot.m_Instance)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DemoManager: failed to create demo '{}'.", slot.m_Name);
			return nullptr;
		}

		return slot.m_Instance.get();
	}

	bool DemoManager::SetActiveDemo(uint32_t index) noexcept
	{
		GGLAB_ASSERT(index < m_DemoSlots.size());
		if (index >= m_DemoSlots.size())
		{
			return false;
		}

		DemoBase* selectedDemo = EnsureDemoCreated(index);
		if (!selectedDemo)
		{
			return false;
		}
		if (selectedDemo == m_ActiveDemo)
		{
			return true;
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
		return true;
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
