#pragma once
#include "Graphics/Asset/IBLBundleArtifact.h"

#include <span>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr std::string_view IBLBundleArtifactType = "gglab.ibl.bundle";

	struct IBLBundleArtifactDecodeResult
	{
		IBLBundleArtifact m_Artifact;
		std::string m_Error;

		[[nodiscard]] bool Succeeded() const noexcept
		{
			return m_Artifact.IsValid();
		}
	};

	class IBLBundleArtifactCodec final
	{
	public:
		[[nodiscard]] static std::vector<std::byte> Serialize(
			const IBLBundleArtifact& artifact) noexcept;
		[[nodiscard]] static IBLBundleArtifactDecodeResult Deserialize(
			std::span<const std::byte> payload,
			const ArtifactContentDigest& expectedContentDigest) noexcept;
	};
}
