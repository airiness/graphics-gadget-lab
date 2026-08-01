#pragma once

#include "Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	struct RGForwardPlusValidationResources
	{
		RGTextureId m_LegacyReferenceColor{};
		RGBufferId m_TileMetrics{};
		RGBufferId m_FrameMetrics{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_LegacyReferenceColor.IsValid();
		}
	};

	inline constexpr const char* ForwardPlusValidationResourcesName =
		"ForwardPlus.ValidationResources";
}
