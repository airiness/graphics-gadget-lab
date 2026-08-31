#include <Common/ForwardCoverageVaryings.hlsli>
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewProgram.hlsli>
#if defined(GGLAB_SHADER_GRAPH_PREVIEW_EXTERNAL_SOURCE)
#include <Generated/SurfaceGenerated.hlsli>
#else
#include <Tests/Generated/SurfaceGeneratedV1.hlsli>
#endif
#include <Programs/ShaderGraphPreview/ShaderGraphPreviewVisualizer.hlsli>

float4 PSMain(ForwardCoverageVSOutput IN) : SV_Target
{
	const SurfaceData surface = EvaluateSurface(g_Preview.Metal, g_Preview.Tint, IN.UV0);
	return VisualizeShaderGraphPreview(surface, IN);
}
