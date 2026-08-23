#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/RHIContext.h"

#include <Windows.h>

namespace gglab
{
	// Windows composition leaf for the selected backend and presentation window.
	// Native window interpretation does not cross into the portable RHI descriptor.
	class Win32RHIContextFactory final : public RHIContextFactoryBase
	{
	public:
		[[nodiscard]] static std::unique_ptr<Win32RHIContextFactory> Create(
			RHIBackendType backend, void* nativeWindowHandle) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Win32RHIContextFactory);
		~Win32RHIContextFactory() override = default;

		[[nodiscard]] std::unique_ptr<RHIContext> CreateContext(
			const RHIContextDesc& desc) const noexcept override;

	private:
		Win32RHIContextFactory(RHIBackendType backend, HINSTANCE instance, HWND window) noexcept;

		RHIBackendType m_Backend = RHIBackendType::Unknown;
		HINSTANCE m_Instance = nullptr;
		HWND m_Window = nullptr;
	};
}
