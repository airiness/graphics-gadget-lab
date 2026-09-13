#pragma once
#include <cstdint>

namespace gglab
{
	// Managed Runtime-generated texture vocabulary. The render resource registry
	// and the explicit pass services share this stable index space.
	enum class RenderTextureIndex : uint8_t
	{
		IBL_EnvironmentCubemap,
		IBL_IrradianceCubemap,
		IBL_PrefilteredSpecularCubemap,
		IBL_BrdfLut,
		Preview_IBL_EnvironmentCubemap,
		Preview_IBL_IrradianceCubemap,
		Preview_IBL_PrefilteredSpecularCubemap,
		Preview_Shadow_DirectionalShadowMap,
		Preview_PostProcess,

		Count
	};
}
