#pragma once
#include <Common/BufferLayout.hlsli>

struct DrawParameters
{
	uint ObjectOffset;
};

ConstantBuffer<SceneData> g_Scene : register(b0);
ConstantBuffer<DrawParameters> g_Draw : register(b1);

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);
StructuredBuffer<ViewData> g_Views : register(t3);
