#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetCacheConfig.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactCacheCore.h"
#include "GGLabRuntime/Graphics/Asset/ModelImportArtifact.h"

#include <cstdint>

namespace gglab
{
	using ModelImportArtifactCacheStatistics = ArtifactCacheCoreStatistics;

	class ModelImportArtifactCache final
	{
	public:
		explicit ModelImportArtifactCache(
			const ModelImportArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ModelImportArtifactCache);
		~ModelImportArtifactCache() = default;

		[[nodiscard]] ModelImportArtifactHandle Admit(ModelImportArtifactHandle artifact) noexcept;
		[[nodiscard]] ModelImportArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] ModelImportArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		using Core = ArtifactCacheCore<ArtifactContentDigest, ModelImportArtifact,
			ArtifactContentDigestHash>;

		Core m_Core;
	};
}
