#pragma once
#include "Graphics/Asset/TextureArtifact.h"
#include "Graphics/Asset/TextureAssetValidation.h"

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
		TextureStructureValidationError m_StructureError =
			TextureStructureValidationError::None;
		[[nodiscard]] bool Succeeded() const noexcept { return m_Artifact.IsValid(); }
	};

	class TextureArtifactCodec final
	{
	public:
		[[nodiscard]] static std::vector<std::byte> Serialize(
			const TextureArtifact& artifact) noexcept;
		[[nodiscard]] static std::vector<std::byte> SerializeTextureData(
			const TextureAssetData& textureData) noexcept;
		[[nodiscard]] static TextureArtifactDecodeResult Deserialize(
			std::span<const std::byte> payload,
			const ArtifactContentDigest& expectedContentDigest,
			TextureAssetValidationLimits limits = {}) noexcept;
	};
}
