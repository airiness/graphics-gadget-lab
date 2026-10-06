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
		// Optional default directory for frame captures; empty requires every
		// capture request to name its output directory.
		std::filesystem::path m_CaptureRoot;

		[[nodiscard]] bool IsValid() const noexcept;
	};
}
