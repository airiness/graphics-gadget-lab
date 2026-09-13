#pragma once

#include <cstddef>

namespace gglab
{
	// Borrowed on the application/render thread for the tooling draw. Entry indices
	// come from the current environment diagnostics catalog and must not be retained
	// across catalog initialization. Selection may start or supersede an async load;
	// only the Runtime owner's subsequent Tick can validate and commit a new source.
	class EnvironmentSelectionControlBase
	{
	public:
		virtual ~EnvironmentSelectionControlBase() = default;
		// True means accepted (including an already active/pending selection), not
		// loaded or published. Invalid indices or immediate load rejection return false.
		// A rejected replacement may still supersede the previous pending candidate.
		[[nodiscard]] virtual bool SelectEnvironment(size_t entryIndex) noexcept = 0;
	};
}
