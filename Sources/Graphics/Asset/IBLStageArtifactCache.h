#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/ArtifactCacheCore.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/Asset/IBLStageArtifact.h"

namespace gglab
{
	struct IBLStageArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 1024ull * 1024ull * 1024ull;
	};

	using IBLStageArtifactCacheStatistics = ArtifactCacheCoreStatistics;

	class IBLStageArtifactCache final
	{
	public:
		explicit IBLStageArtifactCache(
			const IBLStageArtifactCacheConfig& config = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(IBLStageArtifactCache);
		~IBLStageArtifactCache() = default;

		[[nodiscard]] IBLStageArtifactHandle Admit(
			const DerivedDataKey& key,
			IBLStageArtifactHandle artifact) noexcept;
		[[nodiscard]] IBLStageArtifactHandle Find(
			const DerivedDataKey& key) noexcept;
		[[nodiscard]] bool Contains(
			const DerivedDataKey& key) const noexcept;
		void Clear() noexcept;
		[[nodiscard]] IBLStageArtifactCacheStatistics GetStatistics() const noexcept;

	private:
		using Core = ArtifactCacheCore<
			DerivedDataKey,
			IBLStageArtifact,
			DerivedDataKeyHash>;

		Core m_Core;
	};
}
