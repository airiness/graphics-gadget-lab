#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer SceneCB : register(b0)
{
	SceneData g_Scene;
};

cbuffer ObjectCB : register(b1)
{
	uint g_ObjectIndex;
	uint g_ViewIndex;
};

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);
StructuredBuffer<ViewData> g_Views : register(t3);