#pragma once

#include <Common/DepthReconstruction.hlsli>

static const uint GTAO_MAX_DIRECTION_COUNT = 8;
static const uint GTAO_MAX_STEP_COUNT = 8;

struct GTAOSurface
{
	uint2 FullPixel;
	uint2 SelectedOffset;
	float RawDepth;
	float ViewZ;
	float3 PositionVS;
	float3 NormalVS;
	bool IsValid;
	bool HasValidNormal;
};

float GTAOInterleavedGradientNoise(uint2 pixel)
{
	return frac(52.9829189 * frac(0.06711056 * float(pixel.x) + 0.00583715 * float(pixel.y)));
}

bool LoadGTAOPosition(Texture2D<float> depthTexture, uint2 fullPixel, uint2 fullExtent,
	ViewData viewData, out float rawDepth, out float viewZ, out float3 positionVS)
{
	fullPixel = min(fullPixel, fullExtent - 1);
	rawDepth = depthTexture.Load(int3(fullPixel, 0));
	if (IsDepthBackground(rawDepth, viewData.DepthConvention))
	{
		viewZ = 0.0;
		positionVS = 0.0.xxx;
		return false;
	}

	positionVS = ReconstructViewPosition(
		PixelCenterToUV(fullPixel, fullExtent), rawDepth, viewData.InvProjMat);
	viewZ = positionVS.z;
	return all(isfinite(positionVS)) && isfinite(viewZ) && viewZ > 0.0;
}

bool ReconstructGTAONormal(Texture2D<float> depthTexture, uint2 centerPixel, uint2 fullExtent,
	ViewData viewData, float centerViewZ, float3 centerPositionVS, float radius,
	out float3 normalVS)
{
	const uint2 leftPixel = uint2(centerPixel.x > 0 ? centerPixel.x - 1 : 0, centerPixel.y);
	const uint2 rightPixel = uint2(min(centerPixel.x + 1, fullExtent.x - 1), centerPixel.y);
	const uint2 upPixel = uint2(centerPixel.x, centerPixel.y > 0 ? centerPixel.y - 1 : 0);
	const uint2 downPixel = uint2(centerPixel.x, min(centerPixel.y + 1, fullExtent.y - 1));

	float ignoredDepth;
	float leftViewZ;
	float rightViewZ;
	float upViewZ;
	float downViewZ;
	float3 leftPosition;
	float3 rightPosition;
	float3 upPosition;
	float3 downPosition;
	const bool leftValid = LoadGTAOPosition(
		depthTexture, leftPixel, fullExtent, viewData, ignoredDepth, leftViewZ, leftPosition);
	const bool rightValid = LoadGTAOPosition(
		depthTexture, rightPixel, fullExtent, viewData, ignoredDepth, rightViewZ, rightPosition);
	const bool upValid = LoadGTAOPosition(
		depthTexture, upPixel, fullExtent, viewData, ignoredDepth, upViewZ, upPosition);
	const bool downValid = LoadGTAOPosition(
		depthTexture, downPixel, fullExtent, viewData, ignoredDepth, downViewZ, downPosition);

	float3 derivativeX = 0.0.xxx;
	float3 derivativeY = 0.0.xxx;
	float deltaX = 0.0;
	float deltaY = 0.0;
	if (leftValid && (!rightValid || abs(leftViewZ - centerViewZ) <= abs(rightViewZ - centerViewZ)))
	{
		derivativeX = centerPositionVS - leftPosition;
		deltaX = abs(leftViewZ - centerViewZ);
	}
	else if (rightValid)
	{
		derivativeX = rightPosition - centerPositionVS;
		deltaX = abs(rightViewZ - centerViewZ);
	}
	else
	{
		normalVS = 0.0.xxx;
		return false;
	}

	if (upValid && (!downValid || abs(upViewZ - centerViewZ) <= abs(downViewZ - centerViewZ)))
	{
		derivativeY = centerPositionVS - upPosition;
		deltaY = abs(upViewZ - centerViewZ);
	}
	else if (downValid)
	{
		derivativeY = downPosition - centerPositionVS;
		deltaY = abs(downViewZ - centerViewZ);
	}
	else
	{
		normalVS = 0.0.xxx;
		return false;
	}

	const float discontinuityLimit = max(radius * 4.0, centerViewZ * 0.25);
	const float3 unnormalizedNormal = cross(derivativeX, derivativeY);
	const float normalLengthSquared = dot(unnormalizedNormal, unnormalizedNormal);
	if (deltaX > discontinuityLimit || deltaY > discontinuityLimit ||
		!isfinite(normalLengthSquared) || normalLengthSquared <= 1.0e-12)
	{
		normalVS = 0.0.xxx;
		return false;
	}

	normalVS = unnormalizedNormal * rsqrt(normalLengthSquared);
	if (dot(normalVS, centerPositionVS) > 0.0)
	{
		normalVS = -normalVS;
	}
	return all(isfinite(normalVS));
}

GTAOSurface LoadHalfResolutionSurface(Texture2D<float> depthTexture, uint2 halfPixel,
	uint2 fullExtent, ViewData viewData, float radius)
{
	GTAOSurface surface = (GTAOSurface) 0;
	const uint2 basePixel = halfPixel * 2;
	static const uint2 offsets[4] =
	{
		uint2(0, 0), uint2(1, 0), uint2(0, 1), uint2(1, 1)
	};

	[unroll]
	for (uint candidateIndex = 0; candidateIndex < 4; ++candidateIndex)
	{
		const uint2 fullPixel = min(basePixel + offsets[candidateIndex], fullExtent - 1);
		float rawDepth;
		float viewZ;
		float3 positionVS;
		if (!LoadGTAOPosition(
			depthTexture, fullPixel, fullExtent, viewData, rawDepth, viewZ, positionVS))
		{
			continue;
		}
		if (!surface.IsValid || IsDepthNearer(rawDepth, surface.RawDepth, viewData.DepthConvention))
		{
			surface.FullPixel = fullPixel;
			surface.SelectedOffset = offsets[candidateIndex];
			surface.RawDepth = rawDepth;
			surface.ViewZ = viewZ;
			surface.PositionVS = positionVS;
			surface.IsValid = true;
		}
	}

	if (surface.IsValid)
	{
		surface.HasValidNormal = ReconstructGTAONormal(depthTexture, surface.FullPixel, fullExtent,
			viewData, surface.ViewZ, surface.PositionVS, radius, surface.NormalVS);
	}
	return surface;
}

float EvaluateGTAO(Texture2D<float> depthTexture, GTAOSurface surface, uint2 halfPixel,
	uint2 fullExtent, ViewData viewData, float radius, float falloffStart, float falloffEnd,
	float thickness, uint directionCount, uint stepCount)
{
	const float noise = GTAOInterleavedGradientNoise(halfPixel);
	const float projectedRadius = max(
		radius * abs(viewData.ProjMat._22) * float(fullExtent.y) * 0.5 / surface.ViewZ, 1.0);
	float occlusion = 0.0;
	directionCount = clamp(directionCount, 1u, GTAO_MAX_DIRECTION_COUNT);
	stepCount = clamp(stepCount, 1u, GTAO_MAX_STEP_COUNT);

	[loop]
	for (uint directionIndex = 0; directionIndex < directionCount; ++directionIndex)
	{
		const float angle = 3.14159265 *
			(float(directionIndex) + noise) / float(directionCount);
		const float2 direction = float2(cos(angle), sin(angle));
		float directionOcclusion = 0.0;
		[loop]
		for (uint stepIndex = 1; stepIndex <= stepCount; ++stepIndex)
		{
			const float stepFraction = (float(stepIndex) - 0.5 + noise * 0.5) / float(stepCount);
			const float2 pixelOffset = direction * projectedRadius * stepFraction;
			[unroll]
			for (uint side = 0; side < 2; ++side)
			{
				const float sideSign = side == 0 ? -1.0 : 1.0;
				const int2 candidatePixel = clamp(int2(round(float2(surface.FullPixel) +
					pixelOffset * sideSign)), int2(0, 0), int2(fullExtent) - 1);
				float rawDepth;
				float viewZ;
				float3 positionVS;
				if (!LoadGTAOPosition(depthTexture, uint2(candidatePixel), fullExtent,
					viewData, rawDepth, viewZ, positionVS))
				{
					continue;
				}

				const float3 delta = positionVS - surface.PositionVS;
				const float distanceToCandidate = length(delta);
				if (distanceToCandidate <= 1.0e-5 || distanceToCandidate > radius)
				{
					continue;
				}
				const float falloff = 1.0 - smoothstep(
					falloffStart, max(falloffEnd, falloffStart + 1.0e-4), distanceToCandidate);
				const float horizon = dot(surface.NormalVS, delta / distanceToCandidate);
				const float thicknessBias = thickness / max(distanceToCandidate, 1.0e-4);
				directionOcclusion = max(
					directionOcclusion, saturate(horizon - thicknessBias) * falloff);
			}
		}
		occlusion += directionOcclusion;
	}

	return saturate(1.0 - occlusion / float(directionCount));
}
