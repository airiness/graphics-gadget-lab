#include <Common/CBLayout.hlsli>
#include <Common/MathCommon.hlsli>

cbuffer FrameCB : register(b0)
{
	FrameCBData g_Frame;
};

cbuffer ObjectCB : register(b1)
{
	ObjectCBData g_Object;
};

cbuffer MaterialCB : register(b2)
{
	MaterialCBData g_Material;
};

// texture and sampler
Texture2D g_BaseColorTex : register(t0);
SamplerState g_Sampler : register(s0);

struct VSInput
{
	float3 Position : POSITIONT;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD;
};

struct VSOutput
{
	float4 PositionCS : SV_Position;
};