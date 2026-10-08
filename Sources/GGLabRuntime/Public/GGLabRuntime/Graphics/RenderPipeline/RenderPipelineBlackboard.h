#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderView.h"

namespace gglab
{
	struct RGViewTargets
	{
		RGTextureId m_SceneColor{};
		RGTextureId m_BackBuffer{};
		// Optional display-linear diagnostic color and alpha-blended coverage.
		RGTextureId m_MaterialDiagnosticColor{};
		RGTextureId m_MaterialDiagnosticCoverage{};
		// Premultiplied scene-linear lighting to remove before diagnostic composition.
		RGTextureId m_MaterialDiagnosticLighting{};

		// Extent of the render-domain targets (scene color before the temporal resolve,
		// scene depth, motion, material diagnostics).
		uint32_t m_RenderWidth = 0;
		uint32_t m_RenderHeight = 0;
		// Extent of the display-domain targets (temporal output, post-processing, back
		// buffer).
		uint32_t m_DisplayWidth = 0;
		uint32_t m_DisplayHeight = 0;
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
