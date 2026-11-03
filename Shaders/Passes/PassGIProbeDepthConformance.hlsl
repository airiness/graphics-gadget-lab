#include <Lighting/GIProbeDepth.hlsli>

cbuffer Parameters : register(b2)
{
	uint2 CandidateBits;
	uint SceneBits;
	uint Reversed;
	uint SceneEnabled;
	uint AdmissionEnabled;
	uint BaseID;
	uint CaseIndex;
	uint SceneTextureIndex;
	uint Batches;
	uint2 Padding;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	nointerpolation uint ProbeID : TEXCOORD0;
};

VertexOutput VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
	const float2 corners[6] = {
		float2(-1, -1), float2(-1, 1), float2(1, 1),
		float2(-1, -1), float2(1, 1), float2(1, -1) };
	VertexOutput output;
	output.Position = float4(corners[vertex], 0.5, 1);
	output.ProbeID = BaseID + instance;
	return output;
}

struct SceneOutput
{
	float Depth : SV_Target0;
	uint Inputs : SV_Target1;
};

SceneOutput PSScene(VertexOutput input)
{
	const uint words[16] = { CaseIndex, Reversed, SceneEnabled, AdmissionEnabled,
		CandidateBits.x, CandidateBits.y, SceneBits, 2, Batches, 1, 0, 0, 0, 0, 0, 0 };
	SceneOutput output;
	output.Depth = asfloat(SceneBits);
	output.Inputs = words[(uint)(input.Position.x) % 16];
	return output;
}

struct ProbeOutput
{
	uint Winner : SV_Target0;
	float Depth : SV_Depth;
};

ProbeOutput PSProbe(VertexOutput input)
{
	const float depth = asfloat(CandidateBits[input.ProbeID]);
	if (AdmissionEnabled != 0 && !GIProbeDepthAdmitted(depth, Reversed != 0))
	{
		discard;
	}
	if (SceneEnabled != 0)
	{
		Texture2D<float> scene = ResourceDescriptorHeap[NonUniformResourceIndex(SceneTextureIndex)];
		if (!GIProbeSceneDepthVisible(depth, scene.Load(int3((uint2)(input.Position.xy), 0)), Reversed != 0))
		{
			discard;
		}
	}
	ProbeOutput output;
	output.Winner = input.ProbeID + 1;
	output.Depth = depth;
	return output;
}
