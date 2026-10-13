#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/TemporalMotion.hlsli>

float2 ResolveVelocity(ForwardCoverageVSOutput input)
{
	return ComputeTemporalMotionUV(input.CurrentPositionCS, input.PreviousPositionCS);
}

// The alpha test uses the forward pass's material LOD bias, so both passes agree on
// coverage under the equal-depth test.
void PSAlphaTest(ForwardCoverageVSOutput input)
{
	SetMaterialTextureLodBias(input.MaterialTextureLodBias);
	const MaterialData materialData = g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
}

float2 PSVelocityOpaque(ForwardCoverageVSOutput input) : SV_Target0
{
	return ResolveVelocity(input);
}

float2 PSVelocityAlphaTest(ForwardCoverageVSOutput input) : SV_Target0
{
	SetMaterialTextureLodBias(input.MaterialTextureLodBias);
	const MaterialData materialData = g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
	return ResolveVelocity(input);
}
