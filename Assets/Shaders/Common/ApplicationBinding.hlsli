#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer SceneCB : register(b0)
{
	SceneData g_Scene;
};

cbuffer LocalConstants : register(b1)
{
	uint4 g_LocalConstants0;
	uint4 g_LocalConstants1;
};

#define g_LocalParam0 g_LocalConstants0.x
#define g_LocalParam1 g_LocalConstants0.y
#define g_LocalParam2 g_LocalConstants0.z
#define g_LocalParam3 g_LocalConstants0.w
#define g_LocalParam4 g_LocalConstants1.x
#define g_LocalParam5 g_LocalConstants1.y
#define g_LocalParam6 g_LocalConstants1.z
#define g_LocalParam7 g_LocalConstants1.w

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);
StructuredBuffer<ViewData> g_Views : register(t3);
