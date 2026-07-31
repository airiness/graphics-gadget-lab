#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct DebugDrawPassParameters
{
	uint ViewIndex;
	uint Flags;
	uint2 Padding;
};

static const uint DebugDrawFlagScreenSpace = 1u << 0;
static const uint DebugDrawFlagOutputSRGB = 1u << 1;

ConstantBuffer<DebugDrawPassParameters> g_Pass : register(b2);

struct VSInput
{
	float3 Position : POSITION;
	float4 Color : COLOR;
};

struct VSOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	if ((g_Pass.Flags & DebugDrawFlagScreenSpace) != 0)
	{
		const float2 viewportSize = max(float2(viewData.Width, viewData.Height), float2(1.0, 1.0));
		const float2 clip = float2(input.Position.x / viewportSize.x * 2.0 - 1.0,
			1.0 - input.Position.y / viewportSize.y * 2.0);
		output.Position = float4(clip, input.Position.z, 1.0);
	}
	else
	{
		output.Position = mul(mul(float4(input.Position, 1.0), viewData.ViewMat), viewData.ProjMat);
	}
	output.Color = input.Color;
	return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
	float4 color = input.Color;
	if ((g_Pass.Flags & DebugDrawFlagOutputSRGB) != 0)
	{
		color.rgb = LinearToSRGB(color.rgb);
	}
	return color;
}
