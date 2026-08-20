#include <Common/SurfaceEvaluation.hlsli>

// Compile contract for the gglab.surface profile shape: the hand-authored
// surface function takes the runtime MaterialData plus both texture coordinate
// sets and resolves the profile-shaped SurfaceData consumed by the existing
// Forward PBR lighting path.
//
// The MaterialData is consumed from an indexed material table, the same way
// the production Forward PBR pass reads g_Materials[IN.MaterialIndex].
//
// The entry is a pixel shader because the surface evaluation samples base
// color/metallic-roughness/emissive textures through the existing
// texture+sampler sampling helpers.
//
// For this probe the MaterialData layout is pinned by the C++ MaterialGPU
// static_assert in GPUStructures.h and by the runtime material table itself;
// it is intentionally not declared as the permanent ShaderGraph binding
// shape. The SurfaceData shape is pinned by the usage below.

StructuredBuffer<MaterialData> g_SurfaceContractMaterials;

float4 PSMain() : SV_Target
{
	const MaterialData matData = g_SurfaceContractMaterials[0];
	const SurfaceData surface = EvaluateSurface(matData, float2(0.5, 0.5), float2(0.5, 0.5));

	// Keep the profile fields observable so this contract cannot be dropped
	// wholesale by dead-code elimination.
	const float checksum = dot(surface.BaseColor, 1.0.xxx) + dot(surface.Emissive, 1.0.xxx) +
		surface.Metallic + surface.Roughness + surface.Opacity;

	return float4(surface.BaseColor + float3(checksum, 0.0, 0.0), surface.Opacity);
}
