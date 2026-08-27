#pragma once

#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	inline constexpr const char* TemporalAAResourcesName = "TAA.Resources";

	struct RGTemporalAAResources
	{
		TemporalHistoryRenderGraphResources m_History{};
		RGTextureId m_ResolvedSceneColor{};
		// RGBA = accepted, rejection reason, previous U, previous V.
		RGTextureId m_ReprojectionDiagnostics{};
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_History.IsValid() && m_ResolvedSceneColor.IsValid() &&
				m_ReprojectionDiagnostics.IsValid() && m_Width > 0 && m_Height > 0;
		}
	};
}
