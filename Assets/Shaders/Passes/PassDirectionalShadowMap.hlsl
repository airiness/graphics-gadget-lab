#include <Common/Common.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/VertexTransform.hlsli>

cbuffer DirectionalShadowPassConstants : register(b2)
{
	uint g_ViewIndex;
	uint3 g_DirectionalShadowPassPadding;
};

struct VSOutput
{
	float4 PositionCS : SV_POSITION;
	float2 UV0 : TEXCOORD0;
	float2 UV1 : TEXCOORD1;
	nointerpolation uint MaterialIndex : TEXCOORD2;
};

VSOutput VSMain(VertexInputP3N3T2T2Tan4 IN)
{
	VSOutput OUT;

	const ObjectData objData = LoadCurrentObjectData();
	const ViewData viewData = LoadViewData(g_ViewIndex);

	const float4 posWS = TransformPositionWS(IN.Position, objData);
	const float4 posVS = TransformPositionVS(posWS, viewData);
	OUT.PositionCS = TransformPositionCS(posVS, viewData);
	OUT.UV0 = IN.UV0;
	OUT.UV1 = IN.UV1;
	OUT.MaterialIndex = g_Scene.MaterialBaseIndex + objData.MaterialIndex;

	return OUT;
}

void PSMain(VSOutput IN)
{
	const MaterialData matData = g_Materials[IN.MaterialIndex];
	ApplyMaterialAlphaClip(matData, IN.UV0, IN.UV1);
}
