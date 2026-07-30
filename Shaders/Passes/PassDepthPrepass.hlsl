#include <Common/Common.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/VertexTransform.hlsli>

struct DepthPrepassParameters
{
	uint ViewIndex;
	uint3 Padding;
};

ConstantBuffer<DepthPrepassParameters> g_Pass : register(b2);

struct VSOutput
{
	float4 PositionCS : SV_POSITION;
	float2 UV0 : TEXCOORD0;
	float2 UV1 : TEXCOORD1;
	nointerpolation uint MaterialIndex : TEXCOORD2;
};

VSOutput VSMain(VertexInputP3N3T2T2Tan4 input)
{
	const ObjectData objectData = LoadCurrentObjectData();
	const ViewData viewData = LoadViewData(g_Pass.ViewIndex);
	const RigidCoveragePosition position =
		ResolveRigidCoveragePosition(input.Position, objectData, viewData);

	VSOutput output;
	output.PositionCS = position.PositionCS;
	output.UV0 = input.UV0;
	output.UV1 = input.UV1;
	output.MaterialIndex =
		g_Scene.MaterialBaseIndex + objectData.MaterialIndex;
	return output;
}

void PSAlphaTest(VSOutput input)
{
	const MaterialData materialData =
		g_Materials[input.MaterialIndex];
	ApplyMaterialAlphaClip(materialData, input.UV0, input.UV1);
}
