#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/TemporalMotion.hlsli>

float2 ResolveVelocity(ForwardCoverageVSOutput input)
{
	return ComputeTemporalMotionUV(input.CurrentPositionCS, input.PreviousPositionCS);
}

void PSAlphaTest(ForwardCoverageVSOutput input)
{
	const MaterialData materialData = g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
}

float2 PSVelocityOpaque(ForwardCoverageVSOutput input) : SV_Target0
{
	return ResolveVelocity(input);
}

float2 PSVelocityAlphaTest(ForwardCoverageVSOutput input) : SV_Target0
{
	const MaterialData materialData = g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
	return ResolveVelocity(input);
}
