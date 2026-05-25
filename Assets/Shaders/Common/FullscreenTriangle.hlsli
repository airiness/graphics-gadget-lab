#pragma once

struct FullscreenTriangleVSOutput
{
	float4 PositionCS : SV_POSITION;
	float2 UV : TEXCOORD0;
};

FullscreenTriangleVSOutput FullscreenTriangleVS(uint vertexId)
{
	//float2 pos[3] =
	//{
	//	float2(-1.0, -1.0),
	//	float2(-1.0, 3.0),
	//	float2(3.0, -1.0)
	//};
	//
	// D3D-style fullscreen triangle UV:
    // top-left    : (0, 0)
    // bottom-left : (0, 1)
    // bottom-right: (1, 1)
	// 
	// NDC +y ↑
	// Render target(texture) +y ↓
	// NDC (-1,  1) -> UV (0, 0) left top
	// NDC ( 1,  1) -> UV (1, 0) right top
	// NDC (-1, -1) -> UV (0, 1) left bottom
	// NDC ( 1, -1) -> UV (1, 1) right bottom
	//
	// U = (x + 1) / 2
	// V = (1 - y) / 2
	//
	// D3D NDC -> texture UV
	// OUT.UV = float2((pos[vid].x + 1.0) * 0.5, (1.0 - pos[vid].y) * 0.5);
	//
	//float2 uv[3] =
	//{
	//	float2(0.0, 1.0),
	//	float2(0.0, -1.0),
	//	float2(2.0, 1.0)
	//};
	
	FullscreenTriangleVSOutput output;

	float2 pos = float2(
		(vertexId == 2) ? 3.0 : -1.0,
		(vertexId == 1) ? 3.0 : -1.0);

	output.PositionCS = float4(pos, 0.0, 1.0);
	output.UV = pos * float2(0.5, -0.5) + 0.5;

	return output;
}
