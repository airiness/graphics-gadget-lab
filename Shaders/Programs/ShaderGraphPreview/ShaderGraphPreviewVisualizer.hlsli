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

float4 VisualizeShaderGraphPreview(SurfaceData surface, ForwardCoverageVSOutput input)
{
	float3 color = surface.BaseColor * EvaluateShaderGraphPreviewLighting(input.NormalWS) +
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
	else
	{
		// Preserve the complete forward-coverage interface for Vulkan pipeline
		// compatibility. Runtime clamps ViewMode to the supported range, so this
		// fingerprint is visible only if the CPU/GPU contract is violated.
		const float contractFingerprint = input.PositionCS.x + input.PositionWS.x +
			input.NormalWS.x + input.UV0.x + input.UV1.x + input.TangentWS.x +
			float(input.MaterialIndex) + input.CurrentPositionCS.x +
			input.PreviousPositionCS.x;
		color = float3(1.0, 0.0, frac(abs(contractFingerprint)));
	}
	return float4(color, 1.0);
}

#endif
