#include "Core/Precompiled.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Resource/RenderResourceRegistry.h"

#include <numbers>

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
				source.m_SourcePath == m_Source.m_SourcePath))
		{
			return;
		}
		m_Source = std::move(source);
		RequestRebake();
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

	void EnvironmentLightingSystem::SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept
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
		if (ignoreCache)
		{
			m_IgnoreCacheGeneration = m_BakeRequestGeneration;
		}
	}
}
