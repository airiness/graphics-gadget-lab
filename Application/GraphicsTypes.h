#pragma once
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
}