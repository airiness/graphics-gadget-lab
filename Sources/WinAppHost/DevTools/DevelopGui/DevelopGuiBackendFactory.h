#pragma once
#include <memory>

namespace gglab
{
	class DevelopGuiPlatformBackend;
	class DevelopGuiRenderBackend;
	class PlatformWindow;
	class RHIContext;

	[[nodiscard]] std::unique_ptr<DevelopGuiPlatformBackend> CreateDevelopGuiPlatformBackend(
		PlatformWindow& window) noexcept;
	[[nodiscard]] std::unique_ptr<DevelopGuiRenderBackend> CreateDevelopGuiRenderBackend(
		RHIContext& context) noexcept;
}
