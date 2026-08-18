#pragma once

namespace gglab
{
	class DevelopGuiRegistry;
	class RHIContext;

	namespace devtools
	{
		void RegisterDefaultDevelopGuiPanels(
			DevelopGuiRegistry& registry, RHIContext& rhiContext) noexcept;
	}
}
