#pragma once
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/TextureAsset.h"
#include "Graphics/IBLBakeTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace gglab
{
	inline constexpr uint32_t IBLStageArtifactSchemaVersion = 1;
	inline constexpr uint32_t IBLStageProducerCompatibilityVersion = 3;
	inline constexpr uint32_t IBLBakeAlgorithmVersion = 4;

	struct IBLStageArtifact
	{
		IBLArtifactStage m_Stage = IBLArtifactStage::Count;
		TextureAssetData m_Texture;
		ArtifactContentDigest m_ContentDigest{};

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] bool MatchesConfig(const IBLBakeConfig& config) const noexcept;
		[[nodiscard]] uint64_t GetAllocatedBytes() const noexcept;
	};

	using IBLStageArtifactHandle = std::shared_ptr<const IBLStageArtifact>;

	struct IBLStageArtifactSet
	{
		std::array<IBLStageArtifactHandle, static_cast<size_t>(IBLArtifactStage::Count)>
			m_Artifacts{};

		[[nodiscard]] const IBLStageArtifactHandle& Get(IBLArtifactStage stage) const noexcept;
		void Set(IBLArtifactStage stage, IBLStageArtifactHandle artifact) noexcept;
		[[nodiscard]] bool Has(IBLArtifactStage stage) const noexcept;
		[[nodiscard]] bool IsComplete() const noexcept;
	};

	[[nodiscard]] ArtifactContentDigest ComputeIBLStageArtifactContentDigest(
		const IBLStageArtifact& artifact) noexcept;
	[[nodiscard]] IBLStageArtifactHandle CreateIBLStageArtifact(
		IBLArtifactStage stage, TextureAssetData&& texture) noexcept;
}
