#pragma once
//#include <cstdint>
#include <limits>
#include <compare>
#include "TypedIndex.h"

namespace gglab
{
	enum class ModelType : uint32_t
	{
		ModelType_Invalid,
		ModelType_glTF,
		ModelType_Procedual,
	};

	enum class LightType : uint32_t
	{
		LightType_Directional,
		LightType_Spot,
		LightType_Point,
	};

	// ResourceIndex for render graph.
	GGLAB_DEFINE_TYPED_INDEX(ResourceIndex, uint32_t);


}