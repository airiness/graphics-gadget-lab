#pragma once

#include <cstdint>

namespace gglab
{
	enum class IBLPreviewType : uint8_t
	{
		Environment,
		Irradiance,
		PrefilteredSpecular,

		Count
	};

	enum class IBLPreviewLayout : uint32_t
	{
		Grid2x3,
		Cross,

		Count
	};
}
