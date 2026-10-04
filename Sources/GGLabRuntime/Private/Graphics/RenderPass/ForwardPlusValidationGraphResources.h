#pragma once

#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGForwardPlusValidationResources
	{
		RGTextureId m_AllLightsReferenceColor{};
		RGBufferId m_TileMetrics{};
		RGBufferId m_FrameMetrics{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_AllLightsReferenceColor.IsValid();
		}
	};

	inline constexpr const char* ForwardPlusValidationResourcesName =
		"ForwardPlus.ValidationResources";
}
