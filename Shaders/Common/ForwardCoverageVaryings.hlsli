#pragma once

struct ForwardCoverageVSOutput
{
	float4 PositionCS : SV_POSITION;
	float3 PositionWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV0 : TEXCOORD2;
	float2 UV1 : TEXCOORD3;
	float4 TangentWS : TEXCOORD4;

	nointerpolation uint MaterialIndex : TEXCOORD5;
};
