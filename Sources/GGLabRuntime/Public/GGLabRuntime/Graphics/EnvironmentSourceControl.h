#pragma once
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"

namespace gglab
{
	// Narrow composition-time source control for the environment vertical. The
	// concrete environment lighting system stays Runtime-internal; the asset
	// controller and composition path commit selected sources through this
	// contract only.
	class EnvironmentSourceControl
	{
	public:
		virtual ~EnvironmentSourceControl() = default;

		virtual void CommitEnvironmentSource(EnvironmentTextureSource source) noexcept = 0;
	};
}
