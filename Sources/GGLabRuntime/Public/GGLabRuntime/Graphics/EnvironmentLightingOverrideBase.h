#pragma once

#include "GGLabRuntime/Graphics/EnvironmentLightingSettings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	struct EnvironmentLightingOverrideValues
	{
		std::optional<float> m_Intensity;
		std::optional<float> m_RotationRadians;
		std::optional<bool> m_EnableSkybox;
		std::optional<EnvironmentBackgroundMode> m_BackgroundMode;
		std::optional<uint32_t> m_PrefilteredSpecularSampleCount;
	};

	// One entered-session lease per control. Acquire only after the previous owner
	// exits; pending content never acquires a lease. Destruction restores only fields
	// still owned by this lease. A later ordinary control write, even of the same
	// value, takes ownership away without forcing an otherwise unnecessary bake.
	class EnvironmentLightingOverrideBase
	{
	public:
		virtual ~EnvironmentLightingOverrideBase() = default;
		[[nodiscard]] virtual EnvironmentLightingSettings GetInheritedSettings() const noexcept = 0;
		[[nodiscard]] virtual bool OwnsBakeConfiguration() const noexcept = 0;
		// Omitted fields are untouched; scalar sanitization matches the ordinary control.
		virtual void Apply(const EnvironmentLightingOverrideValues& values) noexcept = 0;
	};
}
