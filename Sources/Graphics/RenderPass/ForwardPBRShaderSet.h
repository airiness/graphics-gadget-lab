#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	struct ForwardPBRShaderSet
	{
		ShaderID m_CoverageVertexShader{};
		ShaderID m_ShadingPixelShader{};
		ShaderID m_AlphaTestPixelShader{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() &&
				m_ShadingPixelShader.IsValid() &&
				m_AlphaTestPixelShader.IsValid();
		}
	};
}
