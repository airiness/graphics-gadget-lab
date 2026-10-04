#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"

#include <array>

namespace gglab::shader_programs
{
	std::span<const ShaderProgramRef> GetForwardPBRMaterialDiagnosticsShaderProgramDemand() noexcept
	{
		static const std::array Programs{
			ForwardCoverageVertex,
			ForwardPBRAllLightsPixel,
			ForwardPBRForwardPlusPixel,
			ForwardPBRForwardPlusValidationPixel,
			ForwardPBRAllLightsGTAOPixel,
			ForwardPBRForwardPlusGTAOPixel,
			ForwardPBRForwardPlusValidationGTAOPixel,
			DepthPrepassAlphaTestPixel,
			DepthPrepassVelocityOpaquePixel,
			DepthPrepassVelocityAlphaTestPixel,
			ForwardPBRAllLightsMaterialDiagnosticsPixel,
			ForwardPBRAllLightsGTAOMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusGTAOMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusValidationMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusValidationGTAOMaterialDiagnosticsPixel,
		};
		return Programs;
	}

	std::span<const ShaderProgramRef> GetRendererInitialShaderProgramDemand() noexcept
	{
		static const std::array Programs{
			ForwardCoverageVertex,
			ForwardPBRAllLightsPixel,
			DepthPrepassAlphaTestPixel,
			DepthPrepassVelocityOpaquePixel,
			DepthPrepassVelocityAlphaTestPixel,
			TemporalAAReprojectionCompute,
			AerialPerspectiveBuildCompute,
			AerialPerspectiveCompositeCompute,
			ForwardPlusCullCompute,
			DirectionalShadowMapVertex,
			DirectionalShadowMapPixel,
			ShadowMapPreviewVertex,
			ShadowMapPreviewPixel,
			FinalColorVertex,
			FinalColorPixel,
			BloomVertex,
			BloomPixel,
			PostProcessPreviewVertex,
			PostProcessPreviewPixel,
			DebugDrawVertex,
			DebugDrawPixel,
			SkyboxVertex,
			SkyboxPixel,
			PhysicalSkyPreviewVertex,
			PhysicalSkyPreviewPixel,
			IBLEnvironmentVertex,
			IBLEnvironmentPixel,
			IBLEnvironmentMipVertex,
			IBLEnvironmentMipPixel,
			IBLIrradianceVertex,
			IBLIrradiancePixel,
			IBLPrefilteredSpecularVertex,
			IBLPrefilteredSpecularPixel,
			IBLImportanceVertex,
			IBLImportancePixel,
			IBLBrdfLUTVertex,
			IBLBrdfLUTPixel,
			IBLCubemapPreviewVertex,
			IBLCubemapPreviewPixel,
		};
		return Programs;
	}
}
