#pragma once
#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/VertexInput.hlsli>

uint GetObjectIndex()
{
	return g_LocalParam0;
}

uint GetViewIndex()
{
	return g_LocalParam1;
}

uint GetObjectDataIndex(uint objectOffset)
{
	return g_Scene.ObjectBaseIndex + objectOffset;
}

uint GetCurrentObjectDataIndex()
{
	return GetObjectDataIndex(GetObjectIndex());
}

uint GetViewDataIndex(uint viewOffset)
{
	return g_Scene.ViewBaseIndex + viewOffset;
}

uint GetCurrentViewDataIndex()
{
	return GetViewDataIndex(GetViewIndex());
}

ObjectData LoadObjectData(uint objectOffset)
{
	return g_Objects[GetObjectDataIndex(objectOffset)];
}

ObjectData LoadCurrentObjectData()
{
	return LoadObjectData(GetObjectIndex());
}

ViewData LoadViewData(uint viewOffset)
{
	return g_Views[GetViewDataIndex(viewOffset)];
}

ViewData LoadCurrentViewData()
{
	return LoadViewData(GetViewIndex());
}

float4 TransformPositionWS(float3 positionOS, ObjectData objData)
{
	return mul(float4(positionOS, 1.0), objData.ModelMat);
}

float4 TransformPositionVS(float4 positionWS, ViewData viewData)
{
	return mul(positionWS, viewData.ViewMat);
}

float4 TransformPositionCS(float4 positionVS, ViewData viewData)
{
	return mul(positionVS, viewData.ProjMat);
}

float3 TransformNormalWS(float3 normalOS, ObjectData objData)
{
	return SafeNormalize(
		mul(normalOS, (float3x3) objData.NormalMat),
		float3(0.0, 1.0, 0.0));
}

float4 TransformTangentWS(float4 tangentOS, ObjectData objData)
{
	const float3 tangentWS = SafeNormalize(
		mul(tangentOS.xyz, (float3x3) objData.ModelMat),
		float3(1.0, 0.0, 0.0));
	const float transformHandedness = determinant((float3x3) objData.ModelMat) < 0.0 ? -1.0 : 1.0;
	return float4(tangentWS, tangentOS.w * transformHandedness);
}
