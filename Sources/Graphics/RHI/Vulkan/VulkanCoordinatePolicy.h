#pragma once
#include "Graphics/RHI/RHICoordinatePolicy.h"

namespace gglab
{
	struct VulkanCoordinatePolicy
	{
		RHICoordinatePolicy m_RHIPolicy = GGLabCoordinatePolicy;
		bool m_UsePositiveViewportHeight = true;
		bool m_InvertVertexProducingStageY = true;
		bool m_UseDxPositionW = true;
		bool m_BackendAppliesAdditionalReversedZ = false;
	};

	inline constexpr VulkanCoordinatePolicy GGLabVulkanCoordinatePolicy{};
}
