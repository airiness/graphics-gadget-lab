#pragma once

#include <filesystem>

namespace gglab
{
	struct RuntimePaths
	{
		std::filesystem::path m_RuntimeRoot;
		std::filesystem::path m_AssetRoot;
		std::filesystem::path m_ShaderArtifactRoot;
		std::filesystem::path m_IblDerivedDataRoot;
		std::filesystem::path m_TextureDerivedDataRoot;
		std::filesystem::path m_EnvironmentAssetRoot;
		std::filesystem::path m_SettingsRoot;

		[[nodiscard]] bool IsValid() const noexcept;
	};
}
