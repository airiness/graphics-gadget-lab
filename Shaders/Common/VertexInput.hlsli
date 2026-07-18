#pragma once

struct VertexInputP3N3T2T2Tan4
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 UV0 : TEXCOORD0;
	float2 UV1 : TEXCOORD1;
	float4 Tangent : TANGENT;
};
