#pragma once
#include "DemoBase.h"

namespace gglab
{
	class DemoManager
	{
	public:
		DemoManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DemoManager);
		~DemoManager() = default;

		DemoBase* GetDemo(uint32_t index) const noexcept;
		DemoBase* GetActiveDemo() const noexcept { return m_ActiveDemo; }
		void SetActiveDemo(uint32_t index) noexcept;
		uint32_t GetActiveIndex() const noexcept { return m_ActiveDemoIndex; }
		uint32_t GetDemoCount() const noexcept { return static_cast<uint32_t>(m_Demos.size()); }

		template<typename DemoType, typename... ARGS>
		uint32_t CreateDemo(ARGS&&... args) noexcept
		{
			auto demo = std::make_unique<DemoType>(std::forward<ARGS>(args)...);
			DemoType* demoPtr = demo.get();
			m_Demos.emplace_back(std::move(demo));

			if (!m_ActiveDemo)
			{
				m_ActiveDemo = demoPtr;
				m_ActiveDemoIndex = static_cast<uint32_t>(m_Demos.size()) - 1u;
				m_ActiveDemo->OnEnter();
			}

			return m_ActiveDemoIndex;
		}

	private:
		static constexpr uint32_t InvalidDemoIndex = std::numeric_limits<uint32_t>::max();

	private:
		std::vector<std::unique_ptr<DemoBase>> m_Demos;
		DemoBase* m_ActiveDemo = nullptr;
		uint32_t m_ActiveDemoIndex = InvalidDemoIndex;
	};
}