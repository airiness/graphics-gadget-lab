#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/TextureAsset.h"
#include "Graphics/IBLBakeTypes.h"

#include <memory>

namespace gglab
{
	inline constexpr uint32_t IBLBundleArtifactSchemaVersion = 1;
	inline constexpr uint32_t IBLBundleProducerCompatibilityVersion = 1;
	inline constexpr uint32_t IBLBakeAlgorithmVersion = 3;

	struct IBLBundleArtifact
	{
		TextureAssetData m_Environment;
		TextureAssetData m_Irradiance;
		TextureAssetData m_PrefilteredSpecular;
		TextureAssetData m_BrdfLut;
		ArtifactContentDigest m_ContentDigest{};

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool MatchesConfig(const IBLBakeConfig& config) const noexcept;
		[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept;
	};

	using IBLBundleArtifactHandle = std::shared_ptr<const IBLBundleArtifact>;

	[[nodiscard]] ArtifactContentDigest ComputeIBLBundleArtifactContentDigest(
		const IBLBundleArtifact& artifact) noexcept;
	[[nodiscard]] IBLBundleArtifactHandle CreateIBLBundleArtifact(
		IBLBundleArtifact&& artifact) noexcept;
}
