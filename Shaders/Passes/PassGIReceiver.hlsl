#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Lighting/GIHitMaterial.hlsli>
#include <Lighting/GIShaderConstants.hlsli>
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

float4 PSPosition(ForwardCoverageVSOutput input) : SV_Target0
{
	const MaterialData data = g_Materials[input.MaterialIndex];
	SetMaterialTextureLodBias(input.MaterialTextureLodBias);
	const SurfaceData surface = EvaluateSurface(data, input.UV0, input.UV1);
	ResolveMaterialAlpha(data, surface.Opacity);
	return float4(input.PositionWS,
		data.AlphaMode == MaterialAlphaModeMask ? GIReceiverMasked : GIReceiverOpaque);
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
	const bool flip = (data.Flags & MaterialFlagDoubleSided) != 0 && !frontFace;
	const float3 vertexNormal = SafeNormalize(input.NormalWS, float3(0.0, 1.0, 0.0)) * (flip ? -1.0 : 1.0);
	// World-space derivative area shrinks with pixel footprint. SafeNormalize's
	// absolute length floor would replace valid close-up faces with smooth vertex
	// normals. Scale each derivative before taking the cross product instead.
	const float3 derivativeX = ddx_fine(input.PositionWS);
	const float3 derivativeY = ddy_fine(input.PositionWS);
	const float scaleX = max(abs(derivativeX.x), max(abs(derivativeX.y), abs(derivativeX.z)));
	const float scaleY = max(abs(derivativeY.x), max(abs(derivativeY.y), abs(derivativeY.z)));
	const float3 face = cross(derivativeX / max(scaleX, 1.0e-20),
		derivativeY / max(scaleY, 1.0e-20));
	const float faceScale = max(abs(face.x), max(abs(face.y), abs(face.z)));
	float3 faceNormal = faceScale == 0.0 ? vertexNormal : normalize(face / faceScale);
	faceNormal *= dot(faceNormal, vertexNormal) < 0.0 ? -1.0 : 1.0;
	ReceiverOutput output;
	// BLEND does not participate in receiver coverage.
	output.FaceNormal = float4(faceNormal,
		data.AlphaMode == MaterialAlphaModeMask ? GIReceiverMasked : GIReceiverOpaque);
	output.VertexNormal = float4(vertexNormal, frontFace ? 1.0 : -1.0);
	output.ShadingNormal = float4(material.Base.NormalWS, surface.Metallic);
	output.DiffuseWeight = float4(material.DiffuseWeight, coat.Factor);
	output.BaseColor = float4(surface.BaseColor, (float)(input.MaterialIndex));
	output.ReflectionRoughness = float4(GetMaterialIBLReflection(material), material.Base.EffectivePerceptualRoughness);
	output.ClearcoatNormalRoughness = float4(coat.NormalWS, coat.PerceptualRoughness);
	output.HitAlbedo = float4(EvaluateGIHitAlbedo(data, input.UV0, input.UV1), 1.0);
	return output;
}
