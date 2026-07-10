#pragma once
#include "Graphics/GraphicsTypes.h"

#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	class RenderResourceRegistry;
	class TextureRegistry;

	struct EnvironmentMapEntry
	{
		std::filesystem::path m_Path;
		std::string m_DisplayName;
		TextureID m_TextureId{};
		bool m_LoadAttempted = false;
	};

	struct EnvironmentLightingSettings
	{
		float m_Intensity = 1.0f;
		float m_RotationRadians = 0.0f;
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
		bool SelectEnvironment(size_t entryIndex) noexcept;

		[[nodiscard]] std::span<const EnvironmentMapEntry> GetEntries() const noexcept
		{
			return m_Entries;
		}

		[[nodiscard]] const EnvironmentMapEntry* GetActiveEnvironment() const noexcept;
		[[nodiscard]] TextureID GetActiveTextureId() const noexcept;

		[[nodiscard]] const EnvironmentLightingSettings& GetSettings() const noexcept { return m_Settings; }
		void SetIntensity(float intensity) noexcept;
		void SetRotationRadians(float rotationRadians) noexcept;
		void SetSkyboxEnabled(bool enabled) noexcept { m_Settings.m_EnableSkybox = enabled; }

	private:
		static constexpr size_t InvalidEntryIndex = std::numeric_limits<size_t>::max();

		TextureRegistry* m_TextureRegistry = nullptr;
		RenderResourceRegistry* m_RenderResourceRegistry = nullptr;
		std::vector<EnvironmentMapEntry> m_Entries;
		size_t m_ActiveEntryIndex = InvalidEntryIndex;
		EnvironmentLightingSettings m_Settings{};
	};
}
