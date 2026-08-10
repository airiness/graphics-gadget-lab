#include <Common/BindlessResources.hlsli>
#include <Common/FullscreenTriangle.hlsli>

struct CoordinateConformanceParameters
{
	uint TextureIndex;
	uint SamplerIndex;
	uint Mode;
	float Depth;
	float2 TargetExtent;
	float2 Padding;
};

ConstantBuffer<CoordinateConformanceParameters> g_Pass : register(b2);

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

float4 PSConformance(CoordinateConformanceVSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target0
{
	if (g_Pass.Mode == 0)
	{
		return isFrontFace
			? float4(0.1, 0.9, 0.2, 1.0)
			: float4(0.9, 0.1, 0.8, 1.0);
	}
	if (g_Pass.Mode == 1)
	{
		Texture2D<float4> marker = GetTexture2DFloat4(g_Pass.TextureIndex);
		SamplerState markerSampler = GetSamplerState(g_Pass.SamplerIndex);
		return marker.SampleLevel(markerSampler, input.UV, 0.0);
	}
	if (g_Pass.Mode == 2)
	{
		return g_Pass.Depth > 0.5
			? float4(0.1, 0.9, 0.2, 1.0)
			: float4(0.9, 0.1, 0.15, 1.0);
	}

	const float2 normalizedPosition = input.PositionCS.xy / max(g_Pass.TargetExtent, float2(1.0, 1.0));
	return float4(normalizedPosition.x, normalizedPosition.y, 1.0 - normalizedPosition.y, 1.0);
}
