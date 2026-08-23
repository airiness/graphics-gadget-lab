#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

namespace gglab
{
	inline constexpr uint32_t ShaderRuntimeArtifactFileFormatVersion = 1;
	inline constexpr size_t SerializedShaderRuntimeArtifactManifestSize = 92;
	inline constexpr uintmax_t MaxLooseShaderArtifactBinarySize = 256u * 1024u * 1024u;

	using SerializedShaderRuntimeArtifactManifest =
		std::array<std::byte, SerializedShaderRuntimeArtifactManifestSize>;

	[[nodiscard]] SerializedShaderRuntimeArtifactManifest
		SerializeShaderRuntimeArtifactManifest(
			const ShaderRuntimeArtifactManifest& manifest) noexcept;
	[[nodiscard]] std::optional<ShaderRuntimeArtifactManifest>
		DeserializeShaderRuntimeArtifactManifest(std::span<const std::byte> bytes) noexcept;

	struct ShaderLooseArtifactPaths final
	{
		std::filesystem::path m_BinaryPath{};
		std::filesystem::path m_ManifestPath{};
	};

	class ShaderLooseArtifactLocator final
	{
	public:
		explicit ShaderLooseArtifactLocator(std::filesystem::path root);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] ShaderLooseArtifactPaths GetPaths(
			const ShaderArtifactRef& artifactRef) const;

	private:
		std::filesystem::path m_Root;
	};

	class ShaderLooseArtifactReader final : public ShaderArtifactReaderBase
	{
	public:
		explicit ShaderLooseArtifactReader(ShaderLooseArtifactLocator locator);

		[[nodiscard]] const ShaderLooseArtifactLocator& GetLocator() const noexcept;
		[[nodiscard]] ShaderArtifactReadResult ReadArtifact(
			const ShaderArtifactRef& artifactRef) noexcept override;

	private:
		ShaderLooseArtifactLocator m_Locator;
	};
}
