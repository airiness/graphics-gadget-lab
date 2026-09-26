#pragma once

static const float MAX_FP16_FINITE = 65504.0f;

float SanitizeHDRChannel(float value)
{
	return isfinite(value) ? clamp(value, 0.0f, MAX_FP16_FINITE) : 0.0f;
}

float EncodeSceneColorChannel(float sceneLinearColor, float preExposure)
{
	// Apply the frame storage transform in float before the FP16 clamp.
	return SanitizeHDRChannel(sceneLinearColor * preExposure);
}

float ExposureScaleOverPreExposure(float exposureScale, float preExposure)
{
	// The frame contract guarantees a positive finite scale, including values below 1e-6.
	return exposureScale / preExposure;
}

float RescaleHistoryColorChannel(float historyColor, float currentPreExposure, float previousPreExposure)
{
	// Rescale RGB before temporal comparison and clipping; history age is not color.
	return historyColor * (currentPreExposure / previousPreExposure);
}
