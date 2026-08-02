#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{
	struct ForwardPBRShaderSet
	{
		ShaderID m_CoverageVertexShader{};
		ShaderID m_LegacyShadingPixelShader{};
		ShaderID m_ForwardPlusShadingPixelShader{};
		ShaderID m_ForwardPlusValidationPixelShader{};
		ShaderID m_LegacyGTAOContributionPixelShader{};
		ShaderID m_ForwardPlusGTAOContributionPixelShader{};
		ShaderID m_ForwardPlusValidationGTAOContributionPixelShader{};
		ShaderID m_AlphaTestPixelShader{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() && m_LegacyShadingPixelShader.IsValid() &&
				m_ForwardPlusShadingPixelShader.IsValid() &&
				m_ForwardPlusValidationPixelShader.IsValid() &&
				m_LegacyGTAOContributionPixelShader.IsValid() &&
				m_ForwardPlusGTAOContributionPixelShader.IsValid() &&
				m_ForwardPlusValidationGTAOContributionPixelShader.IsValid() &&
				m_AlphaTestPixelShader.IsValid();
		}
	};
}
