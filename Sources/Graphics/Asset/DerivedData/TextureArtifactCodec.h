#pragma once
#include "Graphics/Asset/TextureArtifact.h"

#include <span>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr std::string_view TextureArtifactType = "gglab.texture";

	struct TextureArtifactDecodeResult
	{
		TextureArtifact m_Artifact;
		std::string m_Error;
		[[nodiscard]] bool Succeeded() const noexcept { return m_Artifact.IsValid(); }
	};

	class TextureArtifactCodec final
	{
	public:
		[[nodiscard]] static std::vector<std::byte> Serialize(
			const TextureArtifact& artifact) noexcept;
		[[nodiscard]] static TextureArtifactDecodeResult Deserialize(
			std::span<const std::byte> payload,
			const ArtifactContentDigest& expectedContentDigest) noexcept;
	};
}
