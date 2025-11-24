#pragma once
#include <Common/BufferLayout.hlsli>

cbuffer FrameCB : register(b0)
{
	FrameCBData g_Frame;
};

StructuredBuffer<ObjectData> g_Objects : register(t8);
StructuredBuffer<MaterialData> g_Materials : register(t9);

SamplerState g_SamplerLinear : register(s0);