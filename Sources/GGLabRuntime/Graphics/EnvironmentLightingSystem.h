#pragma once
#include "Graphics/Asset/AssetContentFingerprint.h"
#include "Graphics/Asset/TextureAssetViews.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"

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
		AssetContentFingerprint m_ContentFingerprint{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Content.IsValid() && m_ContentFingerprint.IsValid();
		}
	};

	class EnvironmentLightingSystem : public EnvironmentLightingViewBase,
		public EnvironmentLightingControlBase
	{
	public:
		struct CreateInfo
		{
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		};

		explicit EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentLightingSystem);
		~EnvironmentLightingSystem() override = default;

		void CommitEnvironmentSource(EnvironmentTextureSource source) noexcept;
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

	private:
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		EnvironmentTextureSource m_Source{};
		EnvironmentLightingSettings m_Settings{};
		uint64_t m_BakeRequestGeneration = 0;
		uint64_t m_IgnoreCacheGeneration = 0;
	};
}
