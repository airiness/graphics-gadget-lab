#pragma once

namespace gglab
{
	class PlatformWindow
	{
	public:
		PlatformWindow() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(PlatformWindow);
		virtual ~PlatformWindow() = default;

		[[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetWidth() const noexcept = 0;
		[[nodiscard]] virtual uint32_t GetHeight() const noexcept = 0;
	};
}
