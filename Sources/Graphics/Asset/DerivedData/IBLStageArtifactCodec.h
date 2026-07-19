#pragma once
#include "Graphics/Asset/IBLStageArtifact.h"

#include <span>
#include <string>
#include <vector>

namespace gglab
{
	struct IBLStageArtifactDecodeResult
	{
		IBLStageArtifact m_Artifact;
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_Artifact.IsValid();
		}
	};

	class IBLStageArtifactCodec final
	{
	public:
		[[nodiscard]] static std::vector<std::byte> Serialize(
			const IBLStageArtifact& artifact) noexcept;
		[[nodiscard]] static IBLStageArtifactDecodeResult Deserialize(
			std::span<const std::byte> payload,
			IBLArtifactStage expectedStage,
			const ArtifactContentDigest& expectedContentDigest) noexcept;
	};
}
