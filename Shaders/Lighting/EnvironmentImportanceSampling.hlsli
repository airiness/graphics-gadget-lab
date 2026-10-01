#pragma once
#include <Common/Cubemap.hlsli>
#include <Lighting/SpecularIntegrationMath.hlsli>

uint GetImportanceRootMip(uint resolution)
{
	return (uint)firstbithigh(resolution);
}

float LoadImportanceMass(Texture2DArray<float> importance, uint face, uint2 texel, uint mip)
{
	return importance.Load(int4(texel, face, mip));
}

float GetEnvironmentImportanceMass(Texture2DArray<float> importance, uint resolution)
{
	float totalMass = 0.0;
	[unroll]
	for (uint face = 0u; face < CUBEMAP_FACE_COUNT; ++face)
	{
		totalMass += LoadImportanceMass(importance, face, uint2(0u, 0u), GetImportanceRootMip(resolution));
	}
	return totalMass;
}

float3 SampleEnvironmentImportance(Texture2DArray<float> importance, uint resolution, float totalMass, float2 Xi, out float pdf)
{
	const uint rootMip = GetImportanceRootMip(resolution);
	float target = Xi.x * totalMass;
	uint selectedFace = 0u;
	float selectedMass = 0.0;
	float lowerMass = 0.0;
	float cumulativeMass = 0.0;
	[unroll]
	for (uint face = 0u; face < CUBEMAP_FACE_COUNT; ++face)
	{
		const float mass = LoadImportanceMass(importance, face, uint2(0u, 0u), rootMip);
		if (mass > 0.0 && target >= cumulativeMass)
		{
			selectedFace = face;
			selectedMass = mass;
			lowerMass = cumulativeMass;
		}
		cumulativeMass += mass;
	}

	float residual = min((target - lowerMass) / selectedMass, 0.99999994);
	uint2 texel = uint2(0u, 0u);
	for (uint mip = rootMip; mip > 0u; --mip)
	{
		texel *= 2u;
		float4 masses;
		[unroll]
		for (uint child = 0u; child < 4u; ++child)
		{
			masses[child] = LoadImportanceMass(importance, selectedFace,
				texel + uint2(child & 1u, child >> 1u), mip - 1u);
		}
		// Recompute this node's sum in the same order as the reduction producer.
		target = residual * (masses.x + masses.y + masses.z + masses.w);
		uint selectedChild = 0u;
		lowerMass = 0.0;
		cumulativeMass = 0.0;
		[unroll]
		for (uint child = 0u; child < 4u; ++child)
		{
			if (masses[child] > 0.0 && target >= cumulativeMass)
			{
				selectedChild = child;
				selectedMass = masses[child];
				lowerMass = cumulativeMass;
			}
			cumulativeMass += masses[child];
		}
		residual = min((target - lowerMass) / selectedMass, 0.99999994);
		texel += uint2(selectedChild & 1u, selectedChild >> 1u);
	}
	const float2 uv = (float2(texel) + float2(residual, Xi.y)) / (float)resolution;
	pdf = GetSpecularEnvironmentPdf(selectedMass, totalMass, resolution, uv.x, uv.y);
	return CubemapFaceUvToDirection(selectedFace, uv);
}

float EvaluateEnvironmentImportancePdf(Texture2DArray<float> importance, uint resolution, float totalMass, float3 direction)
{
	const float3 magnitude = abs(direction);
	uint face;
	if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
		face = direction.x >= 0.0 ? CUBEMAP_FACE_POSITIVE_X : CUBEMAP_FACE_NEGATIVE_X;
	else if (magnitude.y >= magnitude.z)
		face = direction.y >= 0.0 ? CUBEMAP_FACE_POSITIVE_Y : CUBEMAP_FACE_NEGATIVE_Y;
	else
		face = direction.z >= 0.0 ? CUBEMAP_FACE_POSITIVE_Z : CUBEMAP_FACE_NEGATIVE_Z;
	const float major = max(magnitude.x, max(magnitude.y, magnitude.z));
	const float2 uv = float2(dot(direction, GetCubemapFaceRight(face)),
		-dot(direction, GetCubemapFaceUp(face))) / major * 0.5 + 0.5;
	const uint2 texel = min((uint2)(uv * (float)resolution), resolution - 1u);
	return GetSpecularEnvironmentPdf(LoadImportanceMass(importance, face, texel, 0u),
		totalMass, resolution, uv.x, uv.y);
}
