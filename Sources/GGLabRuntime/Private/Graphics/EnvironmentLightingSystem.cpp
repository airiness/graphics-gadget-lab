#include "Graphics/EnvironmentLightingSystem.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <cstring>

namespace gglab
{
	EnvironmentLightingSystem::EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept :
		m_RenderResourceRegistry(createInfo.m_RenderResourceRegistry)
	{
		GGLAB_ASSERT_NOT_NULL(m_RenderResourceRegistry);
	}

	void EnvironmentLightingSystem::CommitEnvironmentSource(
		EnvironmentTextureSource source) noexcept
	{
		GGLAB_ASSERT_MSG(source.IsValid(),
			"EnvironmentLightingSystem requires a valid committed texture source.");
		if (!source.IsValid() ||
			(source.m_Content == m_Source.m_Content && source.m_Type == m_Source.m_Type &&
				source.m_ContentFingerprint == m_Source.m_ContentFingerprint))
		{
			return;
		}
		m_Source = std::move(source);
		if (!m_RequestedPhysicalSky) RequestRebake();
	}

	void EnvironmentLightingSystem::SetIntensity(float intensity) noexcept
	{
		if (std::isfinite(intensity))
		{
			const float clampedIntensity = std::max(intensity, 0.0f);
			if (m_Settings.m_Intensity != clampedIntensity)
			{
				m_Settings.m_Intensity = clampedIntensity;
				m_RenderResourceRegistry->MarkAllIBLPreviewsDirty();
			}
		}
	}

	void EnvironmentLightingSystem::SetRotationRadians(float rotationRadians) noexcept
	{
		if (std::isfinite(rotationRadians))
		{
			constexpr float FullRotation = 2.0f * std::numbers::pi_v<float>;
			const float wrappedRotation = std::remainder(rotationRadians, FullRotation);
			if (m_Settings.m_RotationRadians != wrappedRotation)
			{
				m_Settings.m_RotationRadians = wrappedRotation;
				m_RenderResourceRegistry->MarkAllIBLPreviewsDirty();
			}
		}
	}

	void EnvironmentLightingSystem::SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept
	{
		constexpr uint32_t MinSampleCount = 1;
		constexpr uint32_t MaxSampleCount = 4096;
		const uint32_t clampedSampleCount = std::clamp(sampleCount, MinSampleCount, MaxSampleCount);
		if (m_Settings.m_BakeConfig.m_PrefilteredSpecularSampleCount == clampedSampleCount)
		{
			return;
		}

		m_Settings.m_BakeConfig.m_PrefilteredSpecularSampleCount = clampedSampleCount;
		m_Settings.m_QualityPreset = IBLQualityPreset::Custom;
		RequestRebake();
	}

	void EnvironmentLightingSystem::SetPrefilteredSpecularMaxSampleLuminance(
		float maxSampleLuminance) noexcept
	{
		if (!std::isfinite(maxSampleLuminance))
		{
			return;
		}

		constexpr float MinLuminance = 1.0f;
		constexpr float MaxLuminance = 65000.0f;
		const float clampedLuminance = std::clamp(maxSampleLuminance, MinLuminance, MaxLuminance);
		if (m_Settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance == clampedLuminance)
		{
			return;
		}

		m_Settings.m_BakeConfig.m_PrefilteredSpecularMaxSampleLuminance = clampedLuminance;
		m_Settings.m_QualityPreset = IBLQualityPreset::Custom;
		RequestRebake();
	}

	void EnvironmentLightingSystem::SetQualityPreset(IBLQualityPreset preset) noexcept
	{
		if (preset >= IBLQualityPreset::Custom || m_Settings.m_QualityPreset == preset)
		{
			return;
		}

		m_Settings.m_QualityPreset = preset;
		m_Settings.m_BakeConfig = GetIBLBakeConfig(preset);
		RequestRebake();
	}

	void EnvironmentLightingSystem::RequestRebake(bool ignoreCache) noexcept
	{
		++m_BakeRequestGeneration;
		m_RequestTime = std::chrono::steady_clock::now();
		if (ignoreCache)
		{
			m_IgnoreCacheGeneration = m_BakeRequestGeneration;
		}
	}

	void EnvironmentLightingSystem::SetBackgroundMode(EnvironmentBackgroundMode mode) noexcept
	{
		if (mode == EnvironmentBackgroundMode::TextureEnvironment ||
			mode == EnvironmentBackgroundMode::PhysicalAtmospherePreview ||
			mode == EnvironmentBackgroundMode::PhysicalSky)
		{
			m_Settings.m_BackgroundMode = mode;
		}
	}

	void EnvironmentLightingSystem::ResolveWorldLighting(const std::optional<AtmosphereSettings>& atmosphere,
		const std::optional<ResolvedWorldSun>& sun, uint64_t sunEntityKey, uint64_t sessionIdentity) noexcept
	{
		std::optional<PhysicalSkySource> requested;
		if (m_Settings.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky && atmosphere && sun)
		{
			// A fixed radial +Y observer, one meter above ground. Camera pose and EV are not bake inputs.
			const auto resolved = ResolveAtmosphere(*atmosphere, *sun, Vector3::Zero);
			const Vector3 observerMeters = Vector3(resolved.m_World.m_X, resolved.m_World.m_Y,
				resolved.m_World.m_Z) * 1000.0f + Vector3::UnitY * (resolved.m_Radii.m_X * 1000.0f + 1.0f);
			const Vector3 observerWorld = observerMeters / (resolved.m_World.m_W * 1000.0f);
			requested = PhysicalSkySource{ *atmosphere, *sun, sunEntityKey, sessionIdentity, observerWorld,
				ResolveAtmosphere(*atmosphere, *sun, observerWorld) };
		}
		const bool changed = requested.has_value() != m_RequestedPhysicalSky.has_value() ||
			(requested && (requested->m_SunEntityKey != m_RequestedPhysicalSky->m_SunEntityKey ||
				requested->m_SessionIdentity != m_RequestedPhysicalSky->m_SessionIdentity ||
				requested->m_Sun.m_Direction.m_X != m_RequestedPhysicalSky->m_Sun.m_Direction.m_X ||
				requested->m_Sun.m_Direction.m_Y != m_RequestedPhysicalSky->m_Sun.m_Direction.m_Y ||
				requested->m_Sun.m_Direction.m_Z != m_RequestedPhysicalSky->m_Sun.m_Direction.m_Z ||
				std::memcmp(&requested->m_Parameters, &m_RequestedPhysicalSky->m_Parameters, sizeof(AtmosphereGPU)) != 0));
		if (changed)
		{
			m_RequestedPhysicalSky = requested;
			RequestRebake();
		}
		m_RenderSettings = m_Settings;
		if (m_ActivePhysicalSky)
		{
			m_RenderSettings.m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky;
			m_RenderSettings.m_Intensity = 1.0f;
			m_RenderSettings.m_RotationRadians = 0.0f;
		}
		else if (m_Settings.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky)
		{
			m_RenderSettings.m_BackgroundMode = EnvironmentBackgroundMode::TextureEnvironment;
		}
	}

	bool EnvironmentLightingSystem::PublishWorldLighting(const std::optional<PhysicalSkySource>& source,
		uint64_t generation) noexcept
	{
		if (generation == 0 || generation != m_BakeRequestGeneration) return false;
		m_ActivePhysicalSky = source;
		m_PublicationMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - m_RequestTime).count();
		return true;
	}
}
