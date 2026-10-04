#pragma once
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

#include <array>

namespace gglab
{
	struct ForwardPBRShaderSet
	{
		// Material diagnostic variants per lighting variant and GTAO contribution output.
		static constexpr size_t MaterialDiagnosticVariantCount = 6;
		// The first four variants serve the all-lights and Forward+ production recipes.
		static constexpr size_t ProductionMaterialDiagnosticVariantCount = 4;

		ShaderID m_CoverageVertexShader{};
		ShaderID m_AllLightsShadingPixelShader{};
		ShaderID m_ForwardPlusShadingPixelShader{};
		ShaderID m_ForwardPlusValidationPixelShader{};
		ShaderID m_AllLightsGTAOContributionPixelShader{};
		ShaderID m_ForwardPlusGTAOContributionPixelShader{};
		ShaderID m_ForwardPlusValidationGTAOContributionPixelShader{};
		std::array<ShaderID, MaterialDiagnosticVariantCount> m_MaterialDiagnosticPixelShaders{};
		ShaderID m_AlphaTestPixelShader{};
		ShaderID m_VelocityOpaquePixelShader{};
		ShaderID m_VelocityAlphaTestPixelShader{};
		// Set only by a pipeline composed with the Lab-owned HDR-diff validation
		// recipe. Production validity never depends on validation programs.
		bool m_IncludesHdrDiffValidation = false;

		[[nodiscard]] bool AreMaterialDiagnosticsValid() const noexcept
		{
			const size_t requiredCount = m_IncludesHdrDiffValidation
				? MaterialDiagnosticVariantCount : ProductionMaterialDiagnosticVariantCount;
			for (size_t index = 0; index < requiredCount; ++index)
			{
				if (!m_MaterialDiagnosticPixelShaders[index].IsValid()) return false;
			}
			return true;
		}

		[[nodiscard]] bool AreHdrDiffValidationShadersValid() const noexcept
		{
			return m_ForwardPlusValidationPixelShader.IsValid() &&
				m_ForwardPlusValidationGTAOContributionPixelShader.IsValid();
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() && m_AllLightsShadingPixelShader.IsValid() &&
				m_ForwardPlusShadingPixelShader.IsValid() &&
				m_AllLightsGTAOContributionPixelShader.IsValid() &&
				m_ForwardPlusGTAOContributionPixelShader.IsValid() &&
				m_AlphaTestPixelShader.IsValid() && m_VelocityOpaquePixelShader.IsValid() &&
				m_VelocityAlphaTestPixelShader.IsValid() &&
				(!m_IncludesHdrDiffValidation || AreHdrDiffValidationShadersValid());
		}
	};
}
