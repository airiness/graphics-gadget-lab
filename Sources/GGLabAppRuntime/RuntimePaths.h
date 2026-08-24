#pragma once

#include <filesystem>

namespace gglab
{
	struct RuntimePaths
	{
		std::filesystem::path m_RuntimeRoot;
		std::filesystem::path m_AssetRoot;
		// Development-time transitional input. Shader Artifact Runtime Phase II
		// removes source-path/compiler requirements from the runtime contract.
		std::filesystem::path m_ShaderSourceRoot;
		std::filesystem::path m_ShaderCacheRoot;
		std::filesystem::path m_ShaderArtifactRoot;
		std::filesystem::path m_IblDerivedDataRoot;
		std::filesystem::path m_TextureDerivedDataRoot;
		std::filesystem::path m_EnvironmentAssetRoot;
		std::filesystem::path m_SettingsRoot;

		[[nodiscard]] bool IsValid() const noexcept;
	};
}
