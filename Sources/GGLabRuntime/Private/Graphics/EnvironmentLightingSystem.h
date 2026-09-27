#pragma once
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentSourceControl.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include <chrono>
#include <cstdint>
#include <optional>

namespace gglab
{
	class RenderResourceRegistry;

	// Runtime-only provenance. Procedural sources never acquire texture asset fingerprints or DDC keys.
	struct PhysicalSkySource
	{
		AtmosphereSettings m_Settings{};
		ResolvedWorldSun m_Sun{};
		uint64_t m_SunEntityKey = 0;
		uint64_t m_SessionIdentity = 0;
		Vector3 m_ReferenceObserverWorld{};
		AtmosphereGPU m_Parameters{};
	};

	class EnvironmentLightingSystem : public EnvironmentLightingViewBase,
		public EnvironmentLightingControlBase,
		public EnvironmentSourceControl
	{
	public:
		struct CreateInfo
		{
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		};

		explicit EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentLightingSystem);
		~EnvironmentLightingSystem() override = default;

		void CommitEnvironmentSource(EnvironmentTextureSource source) noexcept override;
		[[nodiscard]] const EnvironmentTextureSource& GetBakeSource() const noexcept
		{
			return m_Source;
		}

		[[nodiscard]] const EnvironmentLightingSettings& GetSettings() const noexcept
		{
			return m_Settings;
		}
		[[nodiscard]] EnvironmentLightingSettings GetEnvironmentLightingSettings()
			const noexcept override { return m_Settings; }
		[[nodiscard]] const IBLBakeConfig& GetBakeConfig() const noexcept
		{
			return m_Settings.m_BakeConfig;
		}
		[[nodiscard]] uint64_t GetBakeRequestGeneration() const noexcept
		{
			return m_BakeRequestGeneration;
		}
		[[nodiscard]] bool ShouldIgnoreCache(uint64_t generation) const noexcept
		{
			return generation == m_IgnoreCacheGeneration;
		}
		void SetIntensity(float intensity) noexcept override;
		void SetRotationRadians(float rotationRadians) noexcept override;
		void SetQualityPreset(IBLQualityPreset preset) noexcept override;
		void SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept override;
		void SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept override;
		void RequestRebake(bool ignoreCache = false) noexcept override;
		void SetSkyboxEnabled(bool enabled) noexcept override { m_Settings.m_EnableSkybox = enabled; }
		void SetBackgroundMode(EnvironmentBackgroundMode mode) noexcept override;
		void ResolveWorldLighting(const std::optional<AtmosphereSettings>& atmosphere,
			const std::optional<ResolvedWorldSun>& sun, uint64_t sunEntityKey,
			uint64_t sessionIdentity = 0) noexcept;
		[[nodiscard]] const std::optional<PhysicalSkySource>& GetRequestedPhysicalSky() const noexcept { return m_RequestedPhysicalSky; }
		[[nodiscard]] const std::optional<PhysicalSkySource>& GetActivePhysicalSky() const noexcept { return m_ActivePhysicalSky; }
		[[nodiscard]] const EnvironmentLightingSettings& GetRenderSettings() const noexcept { return m_RenderSettings; }
		[[nodiscard]] bool PublishWorldLighting(const std::optional<PhysicalSkySource>& source, uint64_t generation) noexcept;
		[[nodiscard]] double GetPublicationMilliseconds() const noexcept { return m_PublicationMilliseconds; }

	private:
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		EnvironmentTextureSource m_Source{};
		EnvironmentLightingSettings m_Settings{};
		uint64_t m_BakeRequestGeneration = 0;
		uint64_t m_IgnoreCacheGeneration = 0;
		std::optional<PhysicalSkySource> m_RequestedPhysicalSky;
		std::optional<PhysicalSkySource> m_ActivePhysicalSky;
		EnvironmentLightingSettings m_RenderSettings{};
		std::chrono::steady_clock::time_point m_RequestTime{};
		double m_PublicationMilliseconds = 0.0;
	};
}
