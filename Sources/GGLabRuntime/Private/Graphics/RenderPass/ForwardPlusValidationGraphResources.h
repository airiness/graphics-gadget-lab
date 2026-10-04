#pragma once

#include "GGLabRuntime/Graphics/Pipeline/ViewRenderFeatureStatus.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	// Published only by a pipeline composed with the Lab-owned HDR-diff validation
	// recipe. Production compositions never create this record.
	struct RGForwardPlusValidationResources
	{
		ViewRenderFeatureStatus m_Status{};
		RGTextureId m_AllLightsReferenceColor{};
		RGBufferId m_TileMetrics{};
		RGBufferId m_FrameMetrics{};

		[[nodiscard]] bool IsActive() const noexcept
		{
			return m_Status.m_State == ViewRenderFeatureState::Active;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_AllLightsReferenceColor.IsValid();
		}
	};

	inline constexpr const char* ForwardPlusValidationResourcesName =
		"ForwardPlus.ValidationResources";
}
