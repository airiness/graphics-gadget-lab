#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <vector>

namespace gglab
{
	// RenderView is a value-only frame description. This publication owns copies,
	// never references to the views used by frame construction or execution.
	struct RenderViewSnapshot
	{
		std::vector<RenderView> m_Views;

		[[nodiscard]] const RenderView* FindView(RenderViewID viewId) const noexcept
		{
			for (const RenderView& view : m_Views)
			{
				if (view.m_ViewId == viewId)
				{
					return &view;
				}
			}
			return nullptr;
		}
	};

	template <> struct SnapshotTraits<RenderViewSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.RenderViewSnapshot");
	};
}
