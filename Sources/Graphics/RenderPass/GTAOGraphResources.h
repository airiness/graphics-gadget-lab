#pragma once

#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	inline constexpr const char* GTAOResourcesName = "GTAO.Resources";

	struct RGGTAOResources
	{
		RGTextureId m_RawAO{};
		RGTextureId m_HalfDepthViewZ{};
		RGTextureId m_ReconstructedNormal{};
		RGTextureId m_SelectedSurfaceOffset{};
		uint32_t m_FullWidth = 0;
		uint32_t m_FullHeight = 0;
		uint32_t m_HalfWidth = 0;
		uint32_t m_HalfHeight = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_RawAO.IsValid() && m_HalfDepthViewZ.IsValid() &&
				m_ReconstructedNormal.IsValid() && m_SelectedSurfaceOffset.IsValid() &&
				m_HalfWidth > 0 && m_HalfHeight > 0;
		}
	};
}
