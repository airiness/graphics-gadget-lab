#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Lighting/GIHitMaterial.hlsli>
#include <PBR/IndirectLighting.hlsli>

cbuffer Parameters : register(b2)
{
	uint ViewIndex;
	uint3 Padding;
};

struct ReceiverOutput
{
	float4 FaceNormal : SV_Target0;
	float4 VertexNormal : SV_Target1;
	float4 ShadingNormal : SV_Target2;
	float4 DiffuseWeight : SV_Target3;
	float4 BaseColor : SV_Target4;
	float4 ReflectionRoughness : SV_Target5;
	float4 ClearcoatNormalRoughness : SV_Target6;
	float4 HitAlbedo : SV_Target7;
};

float2 ReceiverBrdfLUT(float noV, float roughness)
{
	return SampleTextureBindingLevel(MakeTextureSamplerBinding(g_Scene.IBLResource.BrdfLutBinding),
		float2(saturate(noV), saturate(roughness)), 0).rg;
}

#if defined(GGLAB_NONFINITE_COUNTERS)
[earlydepthstencil]
#endif
ReceiverOutput PSMain(ForwardCoverageVSOutput input, bool frontFace : SV_IsFrontFace)
{
	const MaterialData data = g_Materials[input.MaterialIndex];
	SetMaterialTextureLodBias(input.MaterialTextureLodBias);
	const SurfaceData surface = EvaluateSurface(data, input.UV0, input.UV1);
	ResolveMaterialAlpha(data, surface.Opacity);
	const ViewData view = g_Views[g_Scene.ViewBaseIndex + ViewIndex];
	MaterialShadingInput shading;
	shading.PositionWS = input.PositionWS;
	shading.NormalWS = input.NormalWS;
	shading.TangentWS = input.TangentWS;
	shading.UV0 = input.UV0;
	shading.UV1 = input.UV1;
	shading.ViewPositionWS = view.CameraPos.xyz;
	shading.IsFrontFace = frontFace;
	const MaterialShadingFrame frame = PrepareMaterialShadingFrame(data, surface, shading);
	ClearcoatShadingState coat = BuildClearcoatShadingState(surface, frame);
	if (coat.Factor > 0.0)
	{
		ApplyClearcoatDirectionalEnergy(coat, ReceiverBrdfLUT(coat.NoV, coat.PerceptualRoughness));
	}
	const PreparedMaterialShading material = PrepareMaterialShading(surface, frame,
		ReceiverBrdfLUT(frame.NoV, frame.Base.EffectivePerceptualRoughness), coat);
	const bool flip = (data.Flags & 1u) != 0 && !frontFace;
	const float3 vertexNormal = SafeNormalize(input.NormalWS, float3(0.0, 1.0, 0.0)) * (flip ? -1.0 : 1.0);
	float3 faceNormal = SafeNormalize(cross(ddx(input.PositionWS), ddy(input.PositionWS)), vertexNormal);
	faceNormal *= dot(faceNormal, vertexNormal) < 0.0 ? -1.0 : 1.0;
	ReceiverOutput output;
	// Class 0 is cleared background, 1 opaque, 2 MASK. BLEND is not a receiver.
	output.FaceNormal = float4(faceNormal, data.AlphaMode == MaterialAlphaModeMask ? 2.0 : 1.0);
	output.VertexNormal = float4(vertexNormal, frontFace ? 1.0 : -1.0);
	output.ShadingNormal = float4(material.Base.NormalWS, surface.Metallic);
	output.DiffuseWeight = float4(material.DiffuseWeight, coat.Factor);
	output.BaseColor = float4(surface.BaseColor, (float)(input.MaterialIndex));
	output.ReflectionRoughness = float4(GetMaterialIBLReflection(material), material.Base.EffectivePerceptualRoughness);
	output.ClearcoatNormalRoughness = float4(coat.NormalWS, coat.PerceptualRoughness);
	output.HitAlbedo = float4(EvaluateGIHitAlbedo(data, input.UV0, input.UV1), 1.0);
	return output;
}
