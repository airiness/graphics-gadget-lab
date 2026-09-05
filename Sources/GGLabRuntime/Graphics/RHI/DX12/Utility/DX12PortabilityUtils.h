#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

namespace gglab
{
	// Single source of truth for the DX12 portability capability policy. DX12
	// supports every conditional portability capability; Vulkan applies the
	// configured GGLab Vulkan Device Profile subset instead.
	constexpr inline RHIPortabilityCapabilities DX12PortabilityCapabilities{
		.m_ImageViewMinLod = true,
		.m_CustomBorderColor = true,
		.m_VertexAttributeDivisor = true,
		.m_FillModeNonSolid = true,
		.m_DepthClamp = true,
		.m_DepthBiasClamp = true,
		.m_IndependentBlend = true,
		.m_SampleQuality = true,
	};
}
