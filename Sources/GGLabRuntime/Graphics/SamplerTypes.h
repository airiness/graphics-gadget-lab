#pragma once
#include "Graphics/RHI/RHISampler.h"

#include <cstdint>

namespace gglab
{
	enum class SamplerPreset : uint8_t
	{
		PointClamp,
		PointWrap,

		LinearClamp,
		LinearWrap,
		LinearWrapUClampV,

		AnisotropicClamp,
		AnisotropicWrap,

		ShadowCmpLinearClamp,

		Count
	};

	using SamplerKey = RHISamplerDesc;
}
