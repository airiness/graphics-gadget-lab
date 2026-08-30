#include <Common/ForwardCoverageVaryings.hlsli>
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewProgram.hlsli>
#include <Tests/Generated/SurfaceGeneratedV2.hlsli>
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewVisualizer.hlsli>

float4 PSMain(ForwardCoverageVSOutput IN) : SV_Target
{
	const SurfaceData surface = EvaluateSurface(g_Preview.Roughness,
		uint2(g_Preview.TextureIndex, g_Preview.SamplerIndex), IN.UV0);
	return VisualizeShaderGraphPreview(surface, IN);
}
