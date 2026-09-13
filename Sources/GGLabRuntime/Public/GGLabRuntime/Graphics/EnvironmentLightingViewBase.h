#pragma once

#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"

namespace gglab
{
	// Non-owning, render-thread query of requested settings. The copied value does not
	// describe an in-flight bake or establish which IBL resources have been published.
	class EnvironmentLightingViewBase
	{
	public:
		virtual ~EnvironmentLightingViewBase() = default;
		[[nodiscard]] virtual EnvironmentLightingSettings GetEnvironmentLightingSettings()
			const noexcept = 0;
	};
}
