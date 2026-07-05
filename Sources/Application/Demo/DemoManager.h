#pragma once
#include "Application/Demo/DemoBase.h"

namespace gglab
{
	class DemoManager
	{
	public:
		DemoManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DemoManager);
		~DemoManager();

		DemoBase* GetDemo(uint32_t index) const noexcept;
		DemoBase* GetActiveDemo() const noexcept { return m_ActiveDemo; }
		void RequestActiveDemo(uint32_t index) noexcept;
		void ApplyPendingActiveDemo() noexcept;
		uint32_t GetActiveIndex() const noexcept { return m_ActiveDemoIndex; }
		uint32_t GetPendingActiveIndex() const noexcept { return m_PendingActiveDemoIndex; }
		bool HasPendingActiveDemo() const noexcept
		{
			return m_PendingActiveDemoIndex != InvalidDemoIndex;
		}
		uint32_t GetDemoCount() const noexcept { return static_cast<uint32_t>(m_Demos.size()); }

		template<typename DemoType, typename... ARGS>
		uint32_t CreateDemo(ARGS&&... args) noexcept
		{
			auto demo = std::make_unique<DemoType>(std::forward<ARGS>(args)...);
			DemoType* demoPtr = demo.get();
			const uint32_t createdIndex = static_cast<uint32_t>(m_Demos.size());
			m_Demos.emplace_back(std::move(demo));

			if (!m_ActiveDemo)
			{
				m_ActiveDemo = demoPtr;
				m_ActiveDemoIndex = createdIndex;
				m_ActiveDemo->OnEnter();
				if (m_WindowWidth > 0 && m_WindowHeight > 0)
				{
					m_ActiveDemo->OnResize(m_WindowWidth, m_WindowHeight);
				}
			}

			return createdIndex;
		}

		void OnResize(uint32_t width, uint32_t height) noexcept;

	private:
		void SetActiveDemo(uint32_t index) noexcept;

		static constexpr uint32_t InvalidDemoIndex = std::numeric_limits<uint32_t>::max();

	private:
		std::vector<std::unique_ptr<DemoBase>> m_Demos;
		DemoBase* m_ActiveDemo = nullptr;
		uint32_t m_ActiveDemoIndex = InvalidDemoIndex;
		uint32_t m_PendingActiveDemoIndex = InvalidDemoIndex;
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
	};
}
