#include <Common/BindlessResources.hlsli>
#include <Common/FullscreenTriangle.hlsli>

static const uint CoordinateModeWinding = 0;
static const uint CoordinateModeMarkerSampling = 1;
static const uint CoordinateModeDepthVisualization = 2;
static const uint CoordinateModePosition = 3;
static const uint CoordinateModeDepthProbe = 4;
static const float CoordinateDepthProbeTolerance = 0.05;

struct CoordinateConformanceParameters
{
	uint TextureIndex;
	uint SamplerIndex;
	uint Mode;
	float Depth;
	float2 TargetExtent;
	float DepthOverride;
	float Padding;
};

ConstantBuffer<CoordinateConformanceParameters> g_Pass : register(b2);
StructuredBuffer<uint> g_ComputeInput : register(t1);
RWStructuredBuffer<uint> g_ComputeOutput : register(u1);

[numthreads(1, 1, 1)]
void CSStorageDependency(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	if (dispatchThreadId.x == 0)
	{
		g_ComputeOutput[0] += g_ComputeInput[0];
	}
}

struct GeometryVertexInput
{
	float3 Position : POSITION;
	float2 UV : TEXCOORD0;
};

struct CoordinateConformanceVSOutput
{
	float4 PositionCS : SV_Position;
	float2 UV : TEXCOORD0;
};

CoordinateConformanceVSOutput VSGeometry(GeometryVertexInput input)
{
	CoordinateConformanceVSOutput output;
	output.PositionCS = float4(input.Position, 1.0);
	output.UV = input.UV;
	return output;
}

CoordinateConformanceVSOutput VSFullscreen(uint vertexId : SV_VertexID)
{
	const FullscreenTriangleVSOutput fullscreen = FullscreenTriangleVS(vertexId);
	CoordinateConformanceVSOutput output;
	output.PositionCS = fullscreen.PositionCS;
	output.PositionCS.z = g_Pass.Depth;
	output.UV = fullscreen.UV;
	return output;
}

float4 PSMarker(CoordinateConformanceVSOutput input) : SV_Target0
{
	const uint2 texel = uint2(input.PositionCS.xy);
	if (texel.y == 0)
	{
		return texel.x == 0 ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0);
	}
	return texel.x == 0 ? float4(0.0, 0.0, 1.0, 1.0) : float4(1.0, 1.0, 0.0, 1.0);
}

float PSDepthOverride() : SV_Depth
{
	return g_Pass.DepthOverride;
}

float4 PSConformance(CoordinateConformanceVSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
	if (g_Pass.Mode == CoordinateModeWinding)
	{
		return isFrontFace
			? float4(0.1, 0.9, 0.2, 1.0)
			: float4(0.9, 0.1, 0.8, 1.0);
	}
	if (g_Pass.Mode == CoordinateModeMarkerSampling)
	{
		Texture2D<float4> marker = GetTexture2DFloat4(g_Pass.TextureIndex);
		SamplerState markerSampler = GetSamplerState(g_Pass.SamplerIndex);
		return marker.SampleLevel(markerSampler, input.UV, 0.0);
	}
	if (g_Pass.Mode == CoordinateModeDepthVisualization)
	{
		return g_Pass.Depth > 0.5
			? float4(0.1, 0.9, 0.2, 1.0)
			: float4(0.9, 0.1, 0.15, 1.0);
	}
	if (g_Pass.Mode == CoordinateModeDepthProbe)
	{
		Texture2D<float> depthProbe = GetTexture2DFloat(g_Pass.TextureIndex);
		SamplerState depthSampler = GetSamplerState(g_Pass.SamplerIndex);
		const float depth = depthProbe.SampleLevel(depthSampler, input.UV, 0.0);
		return abs(depth - g_Pass.DepthOverride) < CoordinateDepthProbeTolerance
			? float4(0.1, 0.9, 0.2, 1.0)
			: float4(0.9, 0.1, 0.15, 1.0);
	}
	if (g_Pass.Mode == CoordinateModePosition)
	{
		const float2 normalizedPosition =
			input.PositionCS.xy / max(g_Pass.TargetExtent, float2(1.0, 1.0));
		return float4(normalizedPosition.x, normalizedPosition.y, 1.0 - normalizedPosition.y, 1.0);
	}
	return float4(1.0, 0.0, 1.0, 1.0);
}
