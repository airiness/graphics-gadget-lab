#pragma once
#include "Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	enum class OutputColorMode : uint8_t
	{
		SdrSRGB,
	};

	struct ResolvedOutputTransform
	{
		OutputColorMode m_Mode = OutputColorMode::SdrSRGB;
	};

	struct RGPostProcessOutputTarget
	{
		RGTextureId m_Texture{};
		ResolvedOutputTransform m_Transform{};
	};
}
