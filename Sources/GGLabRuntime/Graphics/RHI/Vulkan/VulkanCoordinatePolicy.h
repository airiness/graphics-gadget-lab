#pragma once
#include "Graphics/RHI/RHICoordinatePolicy.h"

namespace gglab
{
	// Runtime viewport/front-face policy. The compile-facing coordinate flags
	// (vertex-producing stage Y inversion, DX Position.W semantics) live in
	// the shared VulkanShaderRuntimeABI contract. Runtime lowering must stay
	// consistent with that contract: ToVulkanFrontFace assumes vertex stages
	// compile with Y inversion applied.
	struct VulkanCoordinatePolicy
	{
		RHICoordinatePolicy m_RHIPolicy = GGLabCoordinatePolicy;
		bool m_UsePositiveViewportHeight = true;
		bool m_BackendAppliesAdditionalReversedZ = false;
	};

	inline constexpr VulkanCoordinatePolicy GGLabVulkanCoordinatePolicy{};
}
