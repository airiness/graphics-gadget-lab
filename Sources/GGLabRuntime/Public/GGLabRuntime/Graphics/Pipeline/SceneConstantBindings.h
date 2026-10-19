#pragma once
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/Pipeline/TypedConstantBinding.h"

namespace gglab
{
	template <>
	struct ConstantPayloadTraits<SceneGPU>
	{
		static constexpr LayoutPayloadTag Tag{ 1 };
	};

	template <>
	struct ConstantPayloadTraits<DirectionalShadowGPU>
	{
		static constexpr LayoutPayloadTag Tag{ 2 };
	};
}
