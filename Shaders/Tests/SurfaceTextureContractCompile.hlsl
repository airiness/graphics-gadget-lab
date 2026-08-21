// Compile contract for the gglab.surface texture signature (profileVersion
// 2 of the gglab.surface profile line): a generated surface program whose
// graph declares a Texture2D parameter.
//
// The contractual shape pinned here:
//
// - a Texture2D graph parameter appears in the generated signature as a
//   single uint2 pair parameter (texture binding index, sampler binding
//   index), one such parameter per texture parameter, in graph-parameter
//   order ahead of the graph-visible inputs;
// - sampling reads the pair's indices through the compiler-provided bindless
//   heap builtins (NonUniformResourceIndex wrapping) and performs
//   texture.Sample(sampler, uv), yielding the resource class' float4 type;
// - the generated function returns the profile's required-output object,
//   whose fields are the descriptor's required outputs in list order.
//
// Everything else in this file (parameter names, the factor buffer, the
// fixed coordinate) is illustrative: the contractual content is the shape
// and the expression, not the identifiers. The pair indices are supplied at
// draw time by the material binding layer; this file is intentionally not
// the runtime material struct shape.

struct SurfaceData
{
	float3 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness;
	float Opacity;
};

float4 SampleSurfaceTexture2D(uint2 baseColorBinding, float2 uv0)
{
	Texture2D<float4> texture =
		ResourceDescriptorHeap[NonUniformResourceIndex(baseColorBinding.x)];
	SamplerState sampler =
		SamplerDescriptorHeap[NonUniformResourceIndex(baseColorBinding.y)];
	return texture.Sample(sampler, uv0);
}

SurfaceData EvaluateSurface(
	uint2 gglab_p_baseColorTexture,
	float gglab_p_metallic,
	float gglab_p_roughness,
	float2 uv0)
{
	SurfaceData surface;

	const float4 baseColorSampled =
		SampleSurfaceTexture2D(gglab_p_baseColorTexture, uv0);
	surface.BaseColor = baseColorSampled.rgb;
	// The sampled alpha stays Opacity; alpha mode/cutoff policy is pass-owned.
	surface.Opacity = baseColorSampled.a;

	// The shared texture channel layout (B = metallic, G = roughness) is
	// scaled by the graph factors.
	surface.Metallic = saturate(gglab_p_metallic * baseColorSampled.b);
	surface.Roughness = saturate(gglab_p_roughness * baseColorSampled.g);
	surface.Emissive = float3(0.0, 0.0, 0.0);

	return surface;
}

// The binding pairs and scalar factors arrive in draw-time state, the same
// way the runtime material table feeds the hand-authored seam.
StructuredBuffer<uint2> g_SurfaceTextureBindings;
StructuredBuffer<float> g_SurfaceFactors;

float4 PSMain() : SV_Target
{
	const uint2 baseColorBinding = g_SurfaceTextureBindings[0];
	const float metallicFactor = g_SurfaceFactors[0];
	const float roughnessFactor = g_SurfaceFactors[1];
	const float2 uv0 = float2(0.5, 0.5);

	const SurfaceData surface = EvaluateSurface(
		baseColorBinding, metallicFactor, roughnessFactor, uv0);

	// Keep the profile fields observable so this contract cannot be dropped
	// wholesale by dead-code elimination.
	const float checksum = dot(surface.BaseColor, 1.0.xxx) +
		dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Opacity;

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
