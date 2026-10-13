#pragma once

float2 TemporalJitterPixelsToUV(float2 jitterPixels, uint2 extent)
{
	return jitterPixels / max(float2(extent), 1.0.xx);
}

float2 TemporalJitterPixelsToNDC(float2 jitterPixels, uint2 extent)
{
	const float2 jitterUV = TemporalJitterPixelsToUV(jitterPixels, extent);
	return float2(2.0 * jitterUV.x, -2.0 * jitterUV.y);
}

float4 ApplyTemporalJitterToClipPosition(float4 unjitteredClip, float2 jitterNDC)
{
	unjitteredClip.xy += jitterNDC * unjitteredClip.w;
	return unjitteredClip;
}

float2 ReprojectTemporalUV(float2 currentUV, float2 motionUV)
{
	return currentUV - motionUV;
}

bool IsTemporalUVInBounds(float2 uv)
{
	return all(isfinite(uv)) && all(uv >= 0.0.xx) && all(uv <= 1.0.xx);
}

// View Z that a current view-space position had in the previous view when only the camera
// moved. Histories compare it with the depth they stored to accept a correspondence.
float ResolveExpectedPreviousViewZ(float3 positionVS, float4x4 inverseView,
	float4x4 previousView)
{
	const float3 positionWS = mul(float4(positionVS, 1.0), inverseView).xyz;
	return mul(float4(positionWS, 1.0), previousView).z;
}

bool IsTemporalDepthCompatible(float expectedPreviousViewZ, float storedPreviousViewZ,
	float absoluteThreshold, float relativeThreshold)
{
	if (!isfinite(expectedPreviousViewZ) || !isfinite(storedPreviousViewZ) ||
		expectedPreviousViewZ <= 0.0 || storedPreviousViewZ <= 0.0 ||
		!isfinite(absoluteThreshold) || !isfinite(relativeThreshold) ||
		absoluteThreshold < 0.0 || relativeThreshold < 0.0)
	{
		return false;
	}

	const float tolerance = max(absoluteThreshold,
		relativeThreshold * expectedPreviousViewZ);
	return abs(expectedPreviousViewZ - storedPreviousViewZ) <= tolerance;
}
