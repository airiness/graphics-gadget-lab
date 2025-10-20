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
	float3 PositionWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 ViewDirWS : TEXCOORD3;
};

VSOutput VSMain(VSInput IN)
{
	VSOutput OUT;
	
	float4 posWS = mul(float4(IN.Position, 1.0), g_Object.ModelMat);
	float3 normalWS = normalize(mul(float4(IN.Normal, 0.0), g_Object.ModelMat).xyz);
	
	float4 posVS = mul(posWS, g_Frame.ViewMat);
	float4 posCS = mul(posVS, g_Frame.ProjMat);
	
	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS;
	OUT.NormalWS = normalWS;
	OUT.UV = IN.UV;
	OUT.ViewDirWS = g_Frame.CameraPosWS.xyz - posWS.xyz;

	return OUT;
}

float4 PSMain(VSOutput IN) : SV_Target
{
	return 1.0.xxxx;
}