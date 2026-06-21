#pragma once
#include "Graphics/RenderGraph/RGResource.h"
#include "Graphics/RenderView.h"

namespace gglab
{
	struct RGViewTargets
	{
		RGTextureId m_SceneColor{};
		RGTextureId m_BackBuffer{};
		RGTextureId m_Depth{};

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	struct RGViewTargetsTable
	{
		std::array<RGViewTargets, utils::ToIndex(RenderViewID::Count)> m_Views{};

		RGViewTargets& GetViewTargets(RenderViewID viewId) noexcept
		{
			return m_Views[utils::ToIndex(viewId)];
		}

		const RGViewTargets& GetViewTargets(RenderViewID viewId) const noexcept
		{
			return m_Views[utils::ToIndex(viewId)];
		}
	};

	inline constexpr const char* ViewTargetsTableName = "RGViewTargetsTable";
}
