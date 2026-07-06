#pragma once
#include "Application/Demo/DemoBase.h"

namespace gglab
{
	class DemoManager
	{
	public:
		using DemoFactory = std::function<std::unique_ptr<DemoBase>()>;

		DemoManager() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DemoManager);
		~DemoManager();

		DemoBase* GetDemo(uint32_t index) const noexcept;
		std::string_view GetDemoName(uint32_t index) const noexcept;
		bool IsDemoCreated(uint32_t index) const noexcept;
		DemoBase* GetActiveDemo() const noexcept { return m_ActiveDemo; }
		uint32_t RegisterDemo(std::string name, DemoFactory factory) noexcept;
		void RequestActiveDemo(uint32_t index) noexcept;
		bool ApplyPendingActiveDemo() noexcept;
		uint32_t GetActiveIndex() const noexcept { return m_ActiveDemoIndex; }
		uint32_t GetPendingActiveIndex() const noexcept { return m_PendingActiveDemoIndex; }
		bool HasPendingActiveDemo() const noexcept
		{
			return m_PendingActiveDemoIndex != InvalidDemoIndex;
		}
		uint32_t GetDemoCount() const noexcept { return static_cast<uint32_t>(m_DemoSlots.size()); }

		void OnResize(uint32_t width, uint32_t height) noexcept;

	private:
		DemoBase* EnsureDemoCreated(uint32_t index) noexcept;
		bool SetActiveDemo(uint32_t index) noexcept;

		static constexpr uint32_t InvalidDemoIndex = std::numeric_limits<uint32_t>::max();

	private:
		struct DemoSlot
		{
			std::string m_Name;
			DemoFactory m_Factory;
			std::unique_ptr<DemoBase> m_Instance;
		};

		std::vector<DemoSlot> m_DemoSlots;
		DemoBase* m_ActiveDemo = nullptr;
		uint32_t m_ActiveDemoIndex = InvalidDemoIndex;
		uint32_t m_PendingActiveDemoIndex = InvalidDemoIndex;
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
	};
}
