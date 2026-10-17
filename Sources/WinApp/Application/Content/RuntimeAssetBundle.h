#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	struct RuntimeAssetBundle
	{
		std::string m_Id;
		std::string m_Revision;
		std::filesystem::path m_EntryPoint;
	};

	// Validates installed file bytes before the Demo starts asynchronous loading.
	// This contract has no authoring repository or exporter dependency.
	[[nodiscard]] std::optional<RuntimeAssetBundle> LoadRuntimeAssetBundle(
		const std::filesystem::path& manifestPath, std::string& failure) noexcept;
}
