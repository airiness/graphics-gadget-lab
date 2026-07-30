#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/MaterialUtils.hlsli>

void PSAlphaTest(ForwardCoverageVSOutput input)
{
	const MaterialData materialData =
		g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
}
