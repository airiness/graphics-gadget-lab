#pragma once

namespace gglab
{
	class PlatformWindow;

	struct PlatformWindowCreateInfo
	{
		std::wstring_view m_Title;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	enum class PlatformEventType : uint8_t
	{
		Activated,
		Deactivated,
		Suspended,
		Resumed,
		Resized,
	};

	struct PlatformEvent
	{
		PlatformEventType m_Type = PlatformEventType::Activated;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	class PlatformHost
	{
	public:
		PlatformHost() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(PlatformHost);
		virtual ~PlatformHost() = default;

		[[nodiscard]] virtual bool Initialize(
			const PlatformWindowCreateInfo& createInfo) noexcept = 0;
		virtual void Finalize() noexcept = 0;

		virtual void PumpEvents() noexcept = 0;
		[[nodiscard]] virtual bool PollEvent(PlatformEvent& event) noexcept = 0;
		virtual void WaitForEvents() noexcept = 0;
		[[nodiscard]] virtual bool IsQuitRequested() const noexcept = 0;

		[[nodiscard]] virtual PlatformWindow& GetMainWindow() noexcept = 0;
	};
}
