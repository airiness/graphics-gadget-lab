#pragma once

// struct member name without m_ for GPU using

namespace graphicsGadgetLab
{
	struct alignas(16) LightGPU
	{
		Vector4 position;
		Vector4 direction;
		Vector4 color;
		uint8_t lightType;
	};
}