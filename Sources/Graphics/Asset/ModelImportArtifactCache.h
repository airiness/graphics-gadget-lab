#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/ArtifactCacheCore.h"
#include "Graphics/Asset/ModelImportArtifact.h"

namespace gglab
{
	struct ModelImportArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};

	using ModelImportArtifactCacheStatistics = ArtifactCacheCoreStatistics;

	class ModelImportArtifactCache final
	{
	public:
		explicit ModelImportArtifactCache(
			const ModelImportArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ModelImportArtifactCache);
		~ModelImportArtifactCache() = default;

		[[nodiscard]] ModelImportArtifactHandle Admit(
			ModelImportArtifactHandle artifact) noexcept;
		[[nodiscard]] ModelImportArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(
			const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] ModelImportArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		using Core = ArtifactCacheCore<
			ArtifactContentDigest,
			ModelImportArtifact,
			ArtifactContentDigestHash>;

		Core m_Core;
	};
}
