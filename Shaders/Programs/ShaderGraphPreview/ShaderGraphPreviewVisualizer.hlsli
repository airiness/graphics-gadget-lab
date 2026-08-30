#ifndef GGLAB_SHADER_GRAPH_PREVIEW_VISUALIZER_HLSLI
#define GGLAB_SHADER_GRAPH_PREVIEW_VISUALIZER_HLSLI

float3 EvaluateShaderGraphPreviewLighting(float3 normalWS)
{
	const float3 normal = normalize(normalWS);
	const float3 keyDirection = normalize(float3(0.35, 0.80, 0.48));
	const float3 fillDirection = normalize(float3(-0.62, 0.28, -0.73));
	const float keyResponse = saturate(dot(normal, keyDirection));
	const float fillResponse = saturate(dot(normal, fillDirection));
	return (0.20 + 0.60 * keyResponse + 0.20 * fillResponse).xxx;
}

float4 VisualizeShaderGraphPreview(SurfaceData surface, float3 normalWS)
{
	float3 color = surface.BaseColor * EvaluateShaderGraphPreviewLighting(normalWS) +
		surface.Emissive;
	if (g_Preview.ViewMode == ShaderGraphPreviewViewModeBaseColor)
	{
		color = surface.BaseColor;
	}
	else if (g_Preview.ViewMode == ShaderGraphPreviewViewModeEmissive)
	{
		color = surface.Emissive;
	}
	else if (g_Preview.ViewMode == ShaderGraphPreviewViewModeMetallic)
	{
		color = surface.Metallic.xxx;
	}
	else if (g_Preview.ViewMode == ShaderGraphPreviewViewModeRoughness)
	{
		color = surface.Roughness.xxx;
	}
	else if (g_Preview.ViewMode == ShaderGraphPreviewViewModeOpacity)
	{
		color = surface.Opacity.xxx;
	}
	return float4(color, 1.0);
}

#endif
