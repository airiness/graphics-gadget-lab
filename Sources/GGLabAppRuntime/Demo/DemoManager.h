#pragma once
#include "Demo/DemoBase.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIFence.h"

namespace gglab
{
	class Renderer;

	class DemoManager
	{
	public:
		using DemoFactory = std::function<std::unique_ptr<DemoBase>()>;

		explicit DemoManager(Renderer* renderer) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DemoManager);
		~DemoManager();

		DemoBase* GetDemo(uint32_t index) const noexcept;
		std::string_view GetDemoName(uint32_t index) const noexcept;
		bool IsDemoCreated(uint32_t index) const noexcept;
		DemoBase* GetActiveDemo() const noexcept { return m_ActiveDemo; }
		uint32_t RegisterDemo(std::string name, DemoFactory factory) noexcept;
		void SetBootstrapDemo(std::unique_ptr<DemoBase> demo) noexcept;
		void RequestActiveDemo(uint32_t index) noexcept;
		bool TickTransitions() noexcept;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept;
		[[nodiscard]] std::optional<LoadingProgress> GetLoadingProgress() const noexcept;
		uint32_t GetActiveIndex() const noexcept { return m_ActiveDemoIndex; }
		uint32_t GetTemporalSessionSerial() const noexcept { return m_TemporalSessionSerial; }
		uint32_t GetPendingActiveIndex() const noexcept;
		bool HasPendingActiveDemo() const noexcept
		{
			return m_RequestedDemoIndex != InvalidDemoIndex || m_PendingDemo != nullptr;
		}
		uint32_t GetRetiringDemoCount() const noexcept
		{
			return static_cast<uint32_t>(m_RetiringDemos.size());
		}
		uint32_t GetDemoCount() const noexcept { return static_cast<uint32_t>(m_DemoSlots.size()); }
		void PrepareForAssetShutdown() noexcept;

		void OnResize(uint32_t width, uint32_t height) noexcept;

	private:
		void BeginRequestedTransition() noexcept;
		bool CommitPendingDemo() noexcept;
		void PollRetiringDemos() noexcept;

		static constexpr uint32_t InvalidDemoIndex = std::numeric_limits<uint32_t>::max();

	private:
		struct DemoSlot
		{
			std::string m_Name;
			DemoFactory m_Factory;
		};

		struct RetiringDemo
		{
			uint32_t m_Index = InvalidDemoIndex;
			std::unique_ptr<DemoBase> m_Instance;
			RHIFencePoint m_RetireFence{};
		};

		Renderer* m_Renderer = nullptr;
		std::vector<DemoSlot> m_DemoSlots;
		std::unique_ptr<DemoBase> m_ActiveInstance;
		DemoBase* m_ActiveDemo = nullptr;
		std::unique_ptr<DemoBase> m_PendingDemo;
		std::vector<RetiringDemo> m_RetiringDemos;
		uint32_t m_ActiveDemoIndex = InvalidDemoIndex;
		uint32_t m_PendingDemoIndex = InvalidDemoIndex;
		uint32_t m_RequestedDemoIndex = InvalidDemoIndex;
		DemoFrameFeedback m_LastActiveFrame{};
		bool m_HasLastActiveFrame = false;
		bool m_IsPreparedForAssetShutdown = false;
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;
		uint32_t m_TemporalSessionSerial = 0;
	};
}
