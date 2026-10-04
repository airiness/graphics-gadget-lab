#pragma once
#include "GGLabRuntime/Graphics/GraphicsHandles.h"

namespace gglab
{
	// Pixel programs of one opaque Forward lighting recipe, one per output layout.
	struct ForwardOpaquePixelShaders
	{
		ShaderID m_Shading{};
		ShaderID m_GTAOContribution{};
		ShaderID m_MaterialDiagnostics{};
		ShaderID m_GTAOContributionMaterialDiagnostics{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Shading.IsValid() && m_GTAOContribution.IsValid();
		}

		[[nodiscard]] bool AreMaterialDiagnosticsValid() const noexcept
		{
			return m_MaterialDiagnostics.IsValid() && m_GTAOContributionMaterialDiagnostics.IsValid();
		}
	};

	struct ForwardPBRShaderSet
	{
		ShaderID m_CoverageVertexShader{};
		ShaderID m_AlphaTestPixelShader{};
		ShaderID m_VelocityOpaquePixelShader{};
		ShaderID m_VelocityAlphaTestPixelShader{};
		// Opaque shading consumes Forward+ light lists.
		ForwardOpaquePixelShaders m_ForwardPlus{};
		// Set only by a pipeline composed with the Lab-owned HDR-diff validation
		// recipe. Production validity never depends on validation programs.
		ForwardOpaquePixelShaders m_ForwardPlusValidation{};
		bool m_IncludesHdrDiffValidation = false;
		// Transparent shading evaluates every scene light.
		ShaderID m_AllLightsShadingPixelShader{};
		ShaderID m_AllLightsMaterialDiagnosticsPixelShader{};

		[[nodiscard]] bool AreMaterialDiagnosticsValid() const noexcept
		{
			return m_AllLightsMaterialDiagnosticsPixelShader.IsValid() &&
				m_ForwardPlus.AreMaterialDiagnosticsValid() &&
				(!m_IncludesHdrDiffValidation || m_ForwardPlusValidation.AreMaterialDiagnosticsValid());
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_CoverageVertexShader.IsValid() && m_AllLightsShadingPixelShader.IsValid() &&
				m_ForwardPlus.IsValid() &&
				m_AlphaTestPixelShader.IsValid() && m_VelocityOpaquePixelShader.IsValid() &&
				m_VelocityAlphaTestPixelShader.IsValid() &&
				(!m_IncludesHdrDiffValidation || m_ForwardPlusValidation.IsValid());
		}
	};
}
