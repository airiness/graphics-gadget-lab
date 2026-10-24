#pragma once
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentSourceControl.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/WorldLightingControlBase.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
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
		std::optional<WorldLightingState> m_Provenance;
	};

	class EnvironmentLightingSystem : public EnvironmentLightingViewBase,
		public EnvironmentLightingControlBase,
		public EnvironmentSourceControl,
		public WorldLightingControlBase
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
			const noexcept override
		{
			return m_Settings;
		}
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
		[[nodiscard]] std::unique_ptr<EnvironmentLightingOverrideBase> AcquireOverride() noexcept override;
		void SetIntensity(float intensity) noexcept override;
		void SetRotationRadians(float rotationRadians) noexcept override;
		void SetQualityPreset(IBLQualityPreset preset) noexcept override;
		void SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept override;
		void SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept override;
		void RequestRebake(bool ignoreCache = false) noexcept override;
		void SetSkyboxEnabled(bool enabled) noexcept override;
		void SetBackgroundMode(EnvironmentBackgroundMode mode) noexcept override;
		void ResolveWorldLighting(const std::optional<AtmosphereSettings>& atmosphere,
			const std::optional<ResolvedWorldSun>& sun, uint64_t sunEntityKey,
			uint64_t sessionIdentity = 0, const WorldSunSettings* authoredSun = nullptr) noexcept;
		void SynchronizeWorldLighting(World& world, uint64_t sessionIdentity) noexcept override;
		[[nodiscard]] std::unique_ptr<PreparedWorldLightingMutationBase> PrepareWorldLightingInputs(
			World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs,
			std::string& error) noexcept override;
		[[nodiscard]] std::unique_ptr<PreparedWorldLightingMutationBase> PrepareWorldSunDirection(
			World& world, uint64_t sessionIdentity, const Vector3& direction,
			std::string& error) noexcept override;
		[[nodiscard]] LightingMutationResult ApplyWorldLightingInputs(
			World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs) noexcept override;
		[[nodiscard]] WorldLightingPublication GetWorldLightingPublication() const noexcept override;
		[[nodiscard]] const std::optional<PhysicalSkySource>& GetRequestedPhysicalSky() const noexcept { return m_RequestedPhysicalSky; }
		[[nodiscard]] const std::optional<PhysicalSkySource>& GetActivePhysicalSky() const noexcept { return m_ActivePhysicalSky; }
		[[nodiscard]] const EnvironmentLightingSettings& GetRenderSettings() const noexcept { return m_RenderSettings; }
		[[nodiscard]] bool PublishWorldLighting(const std::optional<PhysicalSkySource>& source, uint64_t generation) noexcept;
		[[nodiscard]] double GetPublicationMilliseconds() const noexcept { return m_PublicationMilliseconds; }

	private:
		class PreparedWorldLightingMutation;
		class OverrideLease;
		enum class WorldLightingMutationKind : uint8_t
		{
			Inputs,
			SunDirection,
		};
		enum class OverrideField : uint8_t
		{
			Intensity,
			Rotation,
			Skybox,
			Background,
			BakeConfiguration,
			Count,
		};
		struct OverrideLifetime
		{
			EnvironmentLightingSystem* m_Owner = nullptr;
		};
		void RecordSettingsWrite(OverrideField field) noexcept;
		[[nodiscard]] std::unique_ptr<PreparedWorldLightingMutationBase> PrepareWorldLightingMutation(
			World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs,
			WorldLightingMutationKind kind, std::string& error) noexcept;
		void RefreshRenderSettings() noexcept;
		std::shared_ptr<OverrideLifetime> m_OverrideLifetime;
		std::array<uint64_t, static_cast<size_t>(OverrideField::Count)> m_OverrideOwners{};
		uint64_t m_NextOverrideOwner = 1;
		uint64_t m_ActiveOverrideOwner = 0;
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		EnvironmentTextureSource m_Source{};
		EnvironmentLightingSettings m_Settings{};
		uint64_t m_BakeRequestGeneration = 0;
		uint64_t m_IgnoreCacheGeneration = 0;
		std::optional<PhysicalSkySource> m_RequestedPhysicalSky;
		std::optional<PhysicalSkySource> m_ActivePhysicalSky;
		uint64_t m_ActiveWorldLightingGeneration = 0;
		EnvironmentLightingSettings m_RenderSettings{};
		std::chrono::steady_clock::time_point m_RequestTime{};
		double m_PublicationMilliseconds = 0.0;
	};
}
