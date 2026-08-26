#pragma once

float2 TemporalClipPositionToUV(float4 clipPosition)
{
	const float inverseW = rcp(clipPosition.w);
	const float2 ndc = clipPosition.xy * inverseW;
	return float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
}

float2 ComputeTemporalMotionUV(float4 currentClip, float4 previousClip)
{
	return TemporalClipPositionToUV(currentClip) - TemporalClipPositionToUV(previousClip);
}
