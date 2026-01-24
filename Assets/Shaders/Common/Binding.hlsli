#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer SceneCB : register(b0)
{
	SceneCBData g_Scene;
};

cbuffer ObjectCB : register(b1)
{
	uint g_ObjectIndex;
};

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);

SamplerState g_SamplerLinear : register(s0);