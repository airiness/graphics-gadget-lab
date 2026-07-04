#pragma once

namespace gglab
{
	class PlatformWindow;

	class DevelopGuiPlatformBackend
	{
	public:
		DevelopGuiPlatformBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiPlatformBackend);
		virtual ~DevelopGuiPlatformBackend() = default;

		[[nodiscard]] virtual bool Initialize(PlatformWindow& window) noexcept = 0;
		virtual void Finalize() noexcept = 0;
		virtual void NewFrame() noexcept = 0;
	};
}
