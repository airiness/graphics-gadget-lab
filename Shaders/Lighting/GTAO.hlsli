#pragma once

#include <Common/DepthReconstruction.hlsli>

static const uint GTAO_MAX_DIRECTION_COUNT = 8;
static const uint GTAO_MAX_STEP_COUNT = 8;

float ApplyGTAOPower(float visibility, float power)
{
	return pow(saturate(visibility), max(power, 0.1));
}

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
	float ignoredDepth;
	float leftViewZ = 0.0;
	float rightViewZ = 0.0;
	float upViewZ = 0.0;
	float downViewZ = 0.0;
	float3 leftPosition = 0.0.xxx;
	float3 rightPosition = 0.0.xxx;
	float3 upPosition = 0.0.xxx;
	float3 downPosition = 0.0.xxx;
	bool leftValid = false;
	bool rightValid = false;
	bool upValid = false;
	bool downValid = false;
	if (centerPixel.x > 0)
	{
		leftValid = LoadGTAOPosition(depthTexture, centerPixel - uint2(1, 0), fullExtent,
			viewData, ignoredDepth, leftViewZ, leftPosition);
	}
	if (centerPixel.x + 1 < fullExtent.x)
	{
		rightValid = LoadGTAOPosition(depthTexture, centerPixel + uint2(1, 0), fullExtent,
			viewData, ignoredDepth, rightViewZ, rightPosition);
	}
	if (centerPixel.y > 0)
	{
		upValid = LoadGTAOPosition(depthTexture, centerPixel - uint2(0, 1), fullExtent,
			viewData, ignoredDepth, upViewZ, upPosition);
	}
	if (centerPixel.y + 1 < fullExtent.y)
	{
		downValid = LoadGTAOPosition(depthTexture, centerPixel + uint2(0, 1), fullExtent,
			viewData, ignoredDepth, downViewZ, downPosition);
	}

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

float DenoiseGTAO(Texture2D<float> sourceAO, Texture2D<float> halfDepth,
	uint2 pixel, uint2 extent, int2 axis, uint radius)
{
	const float centerDepth = halfDepth.Load(int3(pixel, 0));
	if (!isfinite(centerDepth) || centerDepth <= 0.0)
	{
		return 1.0;
	}

	const float centerAO = saturate(sourceAO.Load(int3(pixel, 0)));
	float weightedAO = centerAO;
	float weightSum = 1.0;
	const float spatialSigma = max(float(radius) * 0.5, 1.0);
	const float discontinuityLimit = max(0.05, centerDepth * 0.02);
	[loop]
	for (int tap = -int(radius); tap <= int(radius); ++tap)
	{
		if (tap == 0)
		{
			continue;
		}
		const int2 neighborPixel = int2(pixel) + axis * tap;
		if (any(neighborPixel < 0) || any(neighborPixel >= int2(extent)))
		{
			continue;
		}
		const float neighborDepth = halfDepth.Load(int3(neighborPixel, 0));
		if (!isfinite(neighborDepth) || neighborDepth <= 0.0)
		{
			continue;
		}
		const float depthDelta = abs(neighborDepth - centerDepth);
		if (depthDelta > discontinuityLimit)
		{
			continue;
		}
		const float spatialWeight = exp(-0.5 * float(tap * tap) /
			(spatialSigma * spatialSigma));
		const float depthWeight = exp2(-32.0 * depthDelta / max(centerDepth, 1.0e-4));
		const float weight = spatialWeight * depthWeight;
		weightedAO += saturate(sourceAO.Load(int3(neighborPixel, 0))) * weight;
		weightSum += weight;
	}
	return saturate(weightedAO / max(weightSum, 1.0e-5));
}

float UpsampleGTAO(Texture2D<float> denoisedAO, Texture2D<float> halfDepth,
	Texture2D<float> fullDepth, uint2 fullPixel, uint2 fullExtent, uint2 halfExtent,
	ViewData viewData)
{
	float fullRawDepth;
	float fullViewZ;
	float3 fullPositionVS;
	if (!LoadGTAOPosition(fullDepth, fullPixel, fullExtent, viewData,
		fullRawDepth, fullViewZ, fullPositionVS))
	{
		return 1.0;
	}

	const float2 halfPosition = (float2(fullPixel) + 0.5) * 0.5 - 0.5;
	const int2 basePixel = int2(floor(halfPosition));
	float weightedAO = 0.0;
	float weightSum = 0.0;
	const float discontinuityLimit = max(0.05, fullViewZ * 0.02);
	[unroll]
	for (uint candidateIndex = 0; candidateIndex < 4; ++candidateIndex)
	{
		const int2 candidateOffset = int2(candidateIndex & 1, candidateIndex >> 1);
		const int2 candidatePixel = clamp(basePixel + candidateOffset, int2(0, 0),
			int2(halfExtent) - 1);
		const float candidateDepth = halfDepth.Load(int3(candidatePixel, 0));
		if (!isfinite(candidateDepth) || candidateDepth <= 0.0 ||
			abs(candidateDepth - fullViewZ) > discontinuityLimit)
		{
			continue;
		}
		const float2 distanceToCandidate = abs(halfPosition - float2(candidatePixel));
		const float spatialWeight =
			max(1.0 - distanceToCandidate.x, 0.0) * max(1.0 - distanceToCandidate.y, 0.0);
		const float depthWeight = exp2(
			-32.0 * abs(candidateDepth - fullViewZ) / max(fullViewZ, 1.0e-4));
		const float weight = spatialWeight * depthWeight;
		weightedAO += saturate(denoisedAO.Load(int3(candidatePixel, 0))) * weight;
		weightSum += weight;
	}
	return weightSum > 1.0e-5 ? saturate(weightedAO / weightSum) : 1.0;
}
