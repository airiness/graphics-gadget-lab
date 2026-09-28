#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"

#include <array>

namespace gglab::shader_programs
{
	std::span<const ShaderProgramRef> GetRendererInitialShaderProgramDemand() noexcept
	{
		static const std::array Programs{
			ForwardCoverageVertex,
			ForwardPBRLegacyPixel,
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
			IBLBrdfLUTVertex,
			IBLBrdfLUTPixel,
			IBLCubemapPreviewVertex,
			IBLCubemapPreviewPixel,
		};
		return Programs;
	}
}
