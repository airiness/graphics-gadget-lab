#pragma once

#include "GGLabRuntime/Graphics/IBLBakeConfig.h"

#include <cstdint>

namespace gglab
{
	// Non-owning, render-thread controls for requested CPU settings. Tooling runs after
	// frame capture; edits affect later captures, never an already captured frame or bake.
	// Quality/sample changes request a later bake. The Runtime scheduler retains source
	// leases, staging allocation, submission, fence completion and atomic publication.
	class EnvironmentLightingControlBase
	{
	public:
		virtual ~EnvironmentLightingControlBase() = default;
		// Non-finite inputs are ignored. Intensity is nonnegative; yaw wraps to one turn.
		virtual void SetIntensity(float intensity) noexcept = 0;
		virtual void SetRotationRadians(float rotationRadians) noexcept = 0;
		// Only concrete presets are accepted; Custom and out-of-range values are ignored.
		virtual void SetQualityPreset(IBLQualityPreset preset) noexcept = 0;
		// Values clamp to [1, 4096] samples and [1, 65000] luminance; changes select Custom.
		virtual void SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept = 0;
		virtual void SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept = 0;
		virtual void SetSkyboxEnabled(bool enabled) noexcept = 0;
		// Each request advances the generation; ignoreCache applies only to that generation.
		virtual void RequestRebake(bool ignoreCache = false) noexcept = 0;
	};
}
