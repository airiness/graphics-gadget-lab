#include "Graphics/EnvironmentLightingSystem.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RenderWorldExtractor.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <memory>
#include <limits>
#include <string>
#include <utility>
#include <cstring>

namespace gglab
{
	class EnvironmentLightingSystem::PreparedWorldLightingMutation final : public PreparedWorldLightingMutationBase
	{
	public:
		PreparedWorldLightingMutation(EnvironmentLightingSystem& owner, World& world,
			const RenderDirectionalLight& target, const WorldLightingInputs& inputs,
			WorldLightingState state, bool changed) noexcept :
			m_Owner(owner), m_World(world), m_Target(target), m_Inputs(inputs),
			m_Rotation(ResolveWorldLightingRotation(inputs.m_SunDirection)),
			m_Source{ inputs.m_Atmosphere, state.m_Sun, target.m_EntityKey.value(),
				state.m_SessionIdentity, state.m_ReferenceObserverWorld, state.m_Parameters, state },
			m_PreviousGeneration(owner.GetBakeRequestGeneration()), m_Changed(changed)
		{
		}

		bool HasChanges() const noexcept override { return m_Changed; }

		WorldLightingMutationResult Commit() noexcept override
		{
			const auto current = RenderWorldExtractor{}.Extract(m_World).m_MainDirectionalLight;
			GGLAB_ASSERT_MSG(!m_Committed && m_Owner.GetBakeRequestGeneration() == m_PreviousGeneration &&
				current.m_EntityKey == m_Target.m_EntityKey && current.m_Transform == m_Target.m_Transform &&
				current.m_Light == m_Target.m_Light && current.m_WorldSun,
				"A prepared lighting mutation must commit once without intervening callbacks or World edits.");
			m_Committed = true;
			if (!m_Changed)
			{
				return { WorldLightingMutationOutcome::Unchanged, m_PreviousGeneration, m_PreviousGeneration };
			}
			m_Target.m_Transform->m_Rotation = m_Rotation;
			m_Target.m_Light->m_WorldSun = m_Inputs.m_Sun;
			m_World.m_Atmosphere = m_Inputs.m_Atmosphere;
			m_Owner.m_Settings.m_BackgroundMode = m_Inputs.m_BackgroundMode;
			m_Owner.m_RequestedPhysicalSky = std::move(m_Source);
			m_Owner.RequestRebake();
			m_Owner.RefreshRenderSettings();
			return { WorldLightingMutationOutcome::Requested, m_PreviousGeneration,
				m_Owner.GetBakeRequestGeneration() };
		}

	private:
		EnvironmentLightingSystem& m_Owner;
		World& m_World;
		RenderDirectionalLight m_Target;
		WorldLightingInputs m_Inputs;
		Quaternion m_Rotation;
		PhysicalSkySource m_Source;
		uint64_t m_PreviousGeneration = 0;
		bool m_Changed = false;
		bool m_Committed = false;
	};

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
		if (!m_RequestedPhysicalSky)
		{
			RequestRebake();
		}
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
		const std::optional<ResolvedWorldSun>& sun, uint64_t sunEntityKey, uint64_t sessionIdentity,
		const WorldSunSettings* authoredSun) noexcept
	{
		std::optional<PhysicalSkySource> requested;
		if (m_Settings.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky && atmosphere && sun)
		{
			const WorldLightingInputs inputs{ .m_SunDirection = sun->m_Direction,
				.m_Sun = authoredSun ? *authoredSun : WorldSunSettings{}, .m_Atmosphere = *atmosphere };
			auto state = ResolveWorldLightingInputs(inputs, *sun);
			state.m_SessionIdentity = sessionIdentity;
			requested = PhysicalSkySource{ *atmosphere, *sun, sunEntityKey, sessionIdentity,
				state.m_ReferenceObserverWorld, state.m_Parameters,
				authoredSun ? std::optional(state) : std::nullopt };
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
			if (requested && requested->m_Provenance)
			{
				requested->m_Provenance->m_AuthoringHash =
					HashWorldLightingAuthoring(requested->m_Provenance->m_Inputs);
			}
			m_RequestedPhysicalSky = requested;
			RequestRebake();
		}
		RefreshRenderSettings();
	}

	void EnvironmentLightingSystem::RefreshRenderSettings() noexcept
	{
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

	void EnvironmentLightingSystem::SynchronizeWorldLighting(World& world, uint64_t sessionIdentity) noexcept
	{
		const auto light = RenderWorldExtractor{}.Extract(world).m_MainDirectionalLight;
		const auto* authoredSun = light.m_WorldSun ? &*light.m_Light->m_WorldSun : nullptr;
		ResolveWorldLighting(world.m_Atmosphere, light.m_WorldSun,
			light.m_EntityKey.value_or(0), sessionIdentity, authoredSun);
	}

	std::unique_ptr<PreparedWorldLightingMutationBase> EnvironmentLightingSystem::PrepareWorldLightingInputs(
		World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs, std::string& error) noexcept
	{
		error.clear();
		if (!ValidateWorldLightingInputs(inputs))
		{
			error = "LightingPreparationFailed: invalid or unsupported World Lighting inputs.";
			return {};
		}
		const auto target = RenderWorldExtractor{}.Extract(world).m_MainDirectionalLight;
		if (!target.m_WorldSun)
		{
			error = "LightingPreparationFailed: the current World has no designated sun.";
			return {};
		}
		const bool physical = m_Settings.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky &&
			world.m_Atmosphere.has_value();
		bool synchronized = physical == m_RequestedPhysicalSky.has_value();
		if (physical && m_RequestedPhysicalSky)
		{
			const auto current = ResolveWorldLightingInputs({ .m_SunDirection = target.m_WorldSun->m_Direction,
				.m_Sun = *target.m_Light->m_WorldSun, .m_Atmosphere = *world.m_Atmosphere }, *target.m_WorldSun);
			const WorldLightingState requested{ .m_Sun = m_RequestedPhysicalSky->m_Sun,
				.m_Parameters = m_RequestedPhysicalSky->m_Parameters };
			synchronized = m_RequestedPhysicalSky->m_SunEntityKey == target.m_EntityKey.value() &&
				m_RequestedPhysicalSky->m_SessionIdentity == sessionIdentity && EqualWorldLightingEffect(current, requested);
		}
		if (!synchronized)
		{
			error = "LightingPreparationFailed: settle existing authored edits before preparing the batch.";
			return {};
		}
		auto state = gglab::PrepareWorldLightingInputs(inputs);
		if (!state.m_AuthoringHash.IsValid())
		{
			error = "LightingPreparationFailed: authored input identity is unavailable.";
			return {};
		}
		state.m_SessionIdentity = sessionIdentity;
		const WorldLightingState requested{
			.m_Sun = m_RequestedPhysicalSky ? m_RequestedPhysicalSky->m_Sun : ResolvedWorldSun{},
			.m_Parameters = m_RequestedPhysicalSky ? m_RequestedPhysicalSky->m_Parameters : AtmosphereGPU{} };
		const bool changed = !m_RequestedPhysicalSky || !EqualWorldLightingEffect(state, requested);
		if (changed && m_BakeRequestGeneration == std::numeric_limits<uint64_t>::max())
		{
			error = "LightingPreparationFailed: Runtime request identity capacity is exhausted.";
			return {};
		}
		return std::make_unique<PreparedWorldLightingMutation>(*this, world, target, inputs, std::move(state), changed);
	}

	WorldLightingMutationResult EnvironmentLightingSystem::ApplyWorldLightingInputs(
		World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs) noexcept
	{
		// Convenience for a standalone mutation; Session batches prepare every source
		// first and call Commit only after the complete batch can be committed.
		if (!ValidateWorldLightingInputs(inputs) || !CanApplyWorldLightingInputs(world))
		{
			return {};
		}
		SynchronizeWorldLighting(world, sessionIdentity);
		std::string error;
		auto prepared = PrepareWorldLightingInputs(world, sessionIdentity, inputs, error);
		return prepared ? prepared->Commit() : WorldLightingMutationResult{};
	}

	WorldLightingPublication EnvironmentLightingSystem::GetWorldLightingPublication() const noexcept
	{
		WorldLightingPublication publication;
		if (m_RequestedPhysicalSky && m_RequestedPhysicalSky->m_Provenance)
		{
			publication.m_Requested = m_RequestedPhysicalSky->m_Provenance;
			publication.m_Requested->m_Generation = m_BakeRequestGeneration;
		}
		if (m_ActivePhysicalSky && m_ActivePhysicalSky->m_Provenance)
		{
			publication.m_Active = m_ActivePhysicalSky->m_Provenance;
			publication.m_Active->m_Generation = m_ActiveWorldLightingGeneration;
		}
		return publication;
	}

	bool EnvironmentLightingSystem::PublishWorldLighting(const std::optional<PhysicalSkySource>& source,
		uint64_t generation) noexcept
	{
		if (generation == 0 || generation != m_BakeRequestGeneration)
		{
			return false;
		}
		m_ActivePhysicalSky = source;
		m_ActiveWorldLightingGeneration = generation;
		m_PublicationMilliseconds = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - m_RequestTime).count();
		return true;
	}
}
