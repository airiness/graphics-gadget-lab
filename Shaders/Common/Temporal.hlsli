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
