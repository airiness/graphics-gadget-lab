#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer SceneCB : register(b0)
{
	SceneData g_Scene;
};

cbuffer DrawCB : register(b1)
{
	uint g_ObjectIndex;
	uint g_ViewIndex;
	uint g_DrawParam0;
	uint g_DrawParam1;
};

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);
StructuredBuffer<ViewData> g_Views : register(t3);
