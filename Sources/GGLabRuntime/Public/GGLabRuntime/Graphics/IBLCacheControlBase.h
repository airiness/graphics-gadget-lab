#pragma once

namespace gglab
{
	// Borrowed for the tooling draw on the render thread. These synchronous
	// maintenance operations use the cache/store owner's existing locks; they do
	// not cancel a bake, release GPU resources or invalidate retained artifacts.
	// Concurrent or later bake work may repopulate either cache after a clear.
	class IBLCacheControlBase
	{
	public:
		virtual ~IBLCacheControlBase() = default;
		// Drops only the CPU cache's ownership; independently held artifacts survive.
		virtual void ClearArtifactCache() noexcept = 0;
		// Clears only the configured IBL DDC. Reports the store's maintenance result;
		// a disabled store succeeds without filesystem work. CPU entries are retained.
		[[nodiscard]] virtual bool ClearDerivedDataStore() noexcept = 0;
	};
}
