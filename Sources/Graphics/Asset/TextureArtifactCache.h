#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/ArtifactCacheCore.h"
#include "Graphics/Asset/TextureArtifact.h"

namespace gglab
{
	struct TextureArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};

	using TextureArtifactCacheStatistics = ArtifactCacheCoreStatistics;

	class TextureArtifactCache final
	{
	public:
		explicit TextureArtifactCache(
			const TextureArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureArtifactCache);
		~TextureArtifactCache() = default;

		[[nodiscard]] TextureArtifactHandle CreateAndAdmit(
			TextureAssetData&& data) noexcept;
		[[nodiscard]] TextureArtifactHandle Admit(
			TextureArtifactHandle artifact) noexcept;
		[[nodiscard]] TextureArtifactHandle Find(
			const ArtifactContentDigest& contentDigest) noexcept;
		[[nodiscard]] bool Contains(
			const ArtifactContentDigest& contentDigest) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] TextureArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		using Core = ArtifactCacheCore<
			ArtifactContentDigest,
			TextureArtifact,
			ArtifactContentDigestHash>;

		Core m_Core;
	};
}
