#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

namespace gglab
{
	// Running means that a supersampled reference sample publishes for presentation and
	// capture. Present only in frames that accumulate a reference sample; the producers'
	// own resource records keep describing their own outputs.
	struct RGTemporalReferenceResources
	{
		// Mean full-resolution GTAO visibility of an ambient-occlusion reference. The
		// gtao-final-ao tap presents it in place of the frame's own GTAO output.
		RGTextureId m_MeanAmbientOcclusion{};
	};

	inline constexpr const char* TemporalReferenceResourcesName = "RGTemporalReferenceResources";
}
