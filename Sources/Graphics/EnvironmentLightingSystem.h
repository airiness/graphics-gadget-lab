#pragma once
#include "Graphics/Asset/TextureAssetViews.h"
#include "Graphics/IBLBakeTypes.h"

#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	class RenderResourceRegistry;
	class TextureRegistry;

	enum class EnvironmentTextureSourceType : uint8_t
	{
		Equirectangular,
		Cubemap,
	};

	struct EnvironmentTextureSource
	{
		TextureContentRef m_Content{};
		EnvironmentTextureSourceType m_Type = EnvironmentTextureSourceType::Equirectangular;

		[[nodiscard]] bool IsValid() const noexcept { return m_Content.IsValid(); }
	};

	struct EnvironmentMapEntry
	{
		std::filesystem::path m_Path;
		std::string m_DisplayName;
		TextureContentRef m_Content{};
		uint64_t m_LastLoadAttemptGeneration = 0;
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
			TextureRegistry* m_TextureRegistry = nullptr;
			RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		};

		explicit EnvironmentLightingSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(EnvironmentLightingSystem);
		~EnvironmentLightingSystem() = default;

		void Initialize(const std::filesystem::path& rootDirectory) noexcept;
		bool SelectDefaultEnvironment() noexcept;
		bool SelectEnvironment(size_t entryIndex) noexcept;

		[[nodiscard]] std::span<const EnvironmentMapEntry> GetEntries() const noexcept
		{
			return m_Entries;
		}

		[[nodiscard]] const EnvironmentMapEntry* GetActiveEnvironment() const noexcept;
		[[nodiscard]] EnvironmentTextureSource GetBakeSource() const noexcept;
		[[nodiscard]] bool EnsureActiveEnvironmentTextureLoaded() noexcept;
		[[nodiscard]] AssetState GetActiveEnvironmentTextureState() const noexcept;

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
		static constexpr size_t InvalidEntryIndex = std::numeric_limits<size_t>::max();

		TextureRegistry* m_TextureRegistry = nullptr;
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		std::vector<EnvironmentMapEntry> m_Entries;
		size_t m_ActiveEntryIndex = InvalidEntryIndex;
		EnvironmentLightingSettings m_Settings{};
		uint64_t m_BakeRequestGeneration = 0;
		uint64_t m_IgnoreCacheGeneration = 0;
	};
}
