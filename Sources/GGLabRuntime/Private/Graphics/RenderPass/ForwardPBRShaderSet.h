#pragma once
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <array>

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
		std::array<ShaderID, 6> m_MaterialDiagnosticPixelShaders{};
		ShaderID m_AlphaTestPixelShader{};
		ShaderID m_VelocityOpaquePixelShader{};
		ShaderID m_VelocityAlphaTestPixelShader{};

		[[nodiscard]] bool AreMaterialDiagnosticsValid() const noexcept
		{
			for (const auto id : m_MaterialDiagnosticPixelShaders)
			{
				if (!id.IsValid()) return false;
			}
			return true;
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() && m_LegacyShadingPixelShader.IsValid() &&
				m_ForwardPlusShadingPixelShader.IsValid() &&
				m_ForwardPlusValidationPixelShader.IsValid() &&
				m_LegacyGTAOContributionPixelShader.IsValid() &&
				m_ForwardPlusGTAOContributionPixelShader.IsValid() &&
				m_ForwardPlusValidationGTAOContributionPixelShader.IsValid() &&
				m_AlphaTestPixelShader.IsValid() && m_VelocityOpaquePixelShader.IsValid() &&
				m_VelocityAlphaTestPixelShader.IsValid();
		}
	};
}
