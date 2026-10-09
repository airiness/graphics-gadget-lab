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
			ForwardPBRForwardPlusGTAOPixel,
			DepthPrepassAlphaTestPixel,
			DepthPrepassVelocityOpaquePixel,
			DepthPrepassVelocityAlphaTestPixel,
			ForwardPBRAllLightsMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusMaterialDiagnosticsPixel,
			ForwardPBRForwardPlusGTAOMaterialDiagnosticsPixel,
		};
		return Programs;
	}

	std::span<const ShaderProgramRef> GetRendererInitialShaderProgramDemand() noexcept
	{
		// Every program the production recipe can require. Lab-composed validation and
		// readback programs and optional diagnostic taps are loaded by their owners.
		static const std::array Programs{
			ForwardCoverageVertex,
			ForwardPBRAllLightsPixel,
			ForwardPBRForwardPlusPixel,
			ForwardPBRForwardPlusGTAOPixel,
			DepthPrepassAlphaTestPixel,
			DepthPrepassVelocityOpaquePixel,
			DepthPrepassVelocityAlphaTestPixel,
			TemporalAAReprojectionCompute,
			TemporalAADepthHistoryCompute,
			TemporalAADisplayDepthVertex,
			TemporalAADisplayDepthPixel,
			AtmosphereLutCompute,
			AerialPerspectiveBuildCompute,
			AerialPerspectiveCompositeCompute,
			ForwardPlusCullCompute,
			GTAOEvaluateCompute,
			GTAODenoiseXCompute,
			GTAODenoiseYCompute,
			GTAOUpsampleCompute,
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
