#include <Common/ForwardCoverageVaryings.hlsli>
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewProgram.hlsli>
#include <Tests/Generated/SurfaceGeneratedV1.hlsli>
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewVisualizer.hlsli>

float4 PSMain(ForwardCoverageVSOutput IN) : SV_Target
{
	const SurfaceData surface = EvaluateSurface(g_Preview.Metal, g_Preview.Tint, IN.UV0);
	return VisualizeShaderGraphPreview(surface, IN.NormalWS);
}
