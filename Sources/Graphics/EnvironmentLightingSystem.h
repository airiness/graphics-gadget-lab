#pragma once
#include "Graphics/Asset/TextureAssetViews.h"
#include "Graphics/IBLBakeTypes.h"

#include <filesystem>

namespace gglab
{
	class RenderResourceRegistry;

	enum class EnvironmentTextureSourceType : uint8_t
	{
		Equirectangular,
		Cubemap,
	};

	struct EnvironmentTextureSource
	{
		TextureContentRef m_Content{};
		EnvironmentTextureSourceType m_Type = EnvironmentTextureSourceType::Equirectangular;
		// Temporary cache provenance until PR6 supplies a decoded-content fingerprint.
		std::filesystem::path m_SourcePath;

		[[nodiscard]] bool IsValid() const noexcept { return m_Content.IsValid(); }
	};

	struct EnvironmentLightingSettings
	{
		float m_Intensity = 1.0f;
		float m_RotationRadians = 0.0f;
		IBLQualityPreset m_QualityPreset = IBLQualityPreset::Medium;
		IBLBakeConfig m_BakeConfig = GetIBLBakeConfig(IBLQualityPreset::Medium);
		bool m_EnableSkybox = true;
	};

	class EnvironmentLightingSystem
	{
	public:
		struct CreateInfo
		{
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		};

		explicit EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentLightingSystem);
		~EnvironmentLightingSystem() = default;

		void CommitEnvironmentSource(EnvironmentTextureSource source) noexcept;
		[[nodiscard]] const EnvironmentTextureSource& GetBakeSource() const noexcept
		{
			return m_Source;
		}

		[[nodiscard]] const EnvironmentLightingSettings& GetSettings() const noexcept { return m_Settings; }
		[[nodiscard]] const IBLBakeConfig& GetBakeConfig() const noexcept { return m_Settings.m_BakeConfig; }
		[[nodiscard]] uint64_t GetBakeRequestGeneration() const noexcept { return m_BakeRequestGeneration; }
		[[nodiscard]] bool ShouldIgnoreCache(uint64_t generation) const noexcept
		{
			return generation == m_IgnoreCacheGeneration;
		}
		void SetIntensity(float intensity) noexcept;
		void SetRotationRadians(float rotationRadians) noexcept;
		void SetQualityPreset(IBLQualityPreset preset) noexcept;
		void SetPrefilteredSpecularSampleCount(uint32_t sampleCount) noexcept;
		void SetPrefilteredSpecularMaxSampleLuminance(float maxSampleLuminance) noexcept;
		void RequestRebake(bool ignoreCache = false) noexcept;
		void SetSkyboxEnabled(bool enabled) noexcept { m_Settings.m_EnableSkybox = enabled; }

	private:
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		EnvironmentTextureSource m_Source{};
		EnvironmentLightingSettings m_Settings{};
		uint64_t m_BakeRequestGeneration = 0;
		uint64_t m_IgnoreCacheGeneration = 0;
	};
}
