#pragma once
#if defined(__cplusplus)
#define NF_HDR_SITE(site)
#define NF_HDR_FORWARD
#else
#include <Common/NonFiniteCounters.hlsli>
#define NF_HDR_SITE(site) , uint nonFiniteSite = site
#define NF_HDR_FORWARD , nonFiniteSite
#endif

static const float MAX_FP16_FINITE = 65504.0f;

float SanitizeSceneRadianceChannel(float value NF_HDR_SITE(NF_SITE_HDRCOLOR_MATH_SANITIZE_SCENE_RADIANCE_CHANNEL_ISFINITE))
{
#if !defined(__cplusplus)
	NonFiniteRecordChannel(nonFiniteSite, value, true, false);
#endif
	return isfinite(value) && value > 0.0f ? value : 0.0f;
}

float SanitizeHDRChannel(float value NF_HDR_SITE(NF_SITE_HDRCOLOR_MATH_SANITIZE_HDRCHANNEL_ISFINITE))
{
#if !defined(__cplusplus)
	NonFiniteRecordChannel(nonFiniteSite, value, true, true);
#endif
	return isfinite(value) ? clamp(value, 0.0f, MAX_FP16_FINITE) : 0.0f;
}

float EncodeSceneColorChannel(float sceneLinearColor, float preExposure NF_HDR_SITE(NF_SITE_HDRCOLOR_MATH_ENCODE_SCENE_COLOR_CHANNEL_SANITIZE_HDRCHANNEL))
{
	// Apply the frame storage transform in float before the FP16 clamp.
	return SanitizeHDRChannel(sceneLinearColor * preExposure NF_HDR_FORWARD);
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

#undef NF_HDR_SITE
#undef NF_HDR_FORWARD
