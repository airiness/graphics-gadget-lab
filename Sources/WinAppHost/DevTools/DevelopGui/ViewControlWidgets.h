#pragma once

#include <string_view>

namespace gglab
{
	struct ViewControlSnapshot;
	class ViewControlActionsBase;
}

namespace gglab::devtools
{
	// Display-view observation with an optional guarded Host override release action.
	void DrawViewControlSnapshot(const ViewControlSnapshot* snapshot,
		ViewControlActionsBase* actions, std::string_view parameterPrefix) noexcept;
}
