#include <Common/Common.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/VertexTransform.hlsli>

struct ForwardCoveragePassParameters
{
	uint ViewIndex;
	uint3 Padding;
};

ConstantBuffer<ForwardCoveragePassParameters> g_Pass : register(b2);

ForwardCoverageVSOutput VSMain(VertexInputP3N3T2T2Tan4 input)
{
	const ObjectData objectData = LoadCurrentObjectData();
	const ViewData viewData = LoadViewData(g_Pass.ViewIndex);
	const RigidCoveragePosition position =
		ResolveRigidCoveragePosition(input.Position, objectData, viewData);

	ForwardCoverageVSOutput output;
	output.PositionCS = position.PositionCS;
	output.PositionWS = position.PositionWS.xyz;
	output.NormalWS = TransformNormalWS(input.Normal, objectData);
	output.UV0 = input.UV0;
	output.UV1 = input.UV1;
	output.TangentWS = TransformTangentWS(input.Tangent, objectData);
	output.MaterialIndex = g_Scene.MaterialBaseIndex + objectData.MaterialIndex;
	output.ViewIndex = GetViewDataIndex(g_Pass.ViewIndex);
	return output;
}
