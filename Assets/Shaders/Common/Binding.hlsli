#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer FrameCB : register(b0)
{
	FrameCBData g_Frame;
};

cbuffer ObjectCB : register(b1)
{
	uint g_ObjectIndex;
};

cbuffer MaterialCB : register(b2)
{
	uint g_MaterialIndex;
};

StructuredBuffer<ObjectData> g_Objects : register(t1);
StructuredBuffer<MaterialData> g_Materials : register(t2);

SamplerState g_SamplerLinear : register(s0);