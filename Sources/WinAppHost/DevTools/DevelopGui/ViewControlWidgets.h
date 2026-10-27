#pragma once

#include <string_view>

namespace gglab
{
	struct ViewControlSnapshot;
}

namespace gglab::devtools
{
	// Read-only display-view observation; no parameter or tooling writes.
	void DrawViewControlSnapshot(const ViewControlSnapshot* snapshot,
		std::string_view parameterPrefix) noexcept;
}
