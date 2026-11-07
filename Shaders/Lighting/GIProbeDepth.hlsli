#pragma once
#include <Common/NonFiniteCounters.hlsli>

float4 GIProbeCropDisplayPixel(float4 clip, uint2 extent, uint2 pixel)
{
	// Preserve original clip Z/W while mapping one display-pixel footprint to 1x1.
	clip.xy = clip.xy * (float2)(extent) + clip.w *
		float2((float)(extent.x) - 2 * (float)(pixel.x) - 1,
			2 * (float)(pixel.y) + 1 - (float)(extent.y));
	return clip;
}

uint GIProbeComparableDepthBits(float depth)
{
	if (!isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_DEPTH_GIPROBECOMPARABLEDEPTHBITS_ISFINITE, depth)))
	{
		return 0xffffffffu;
	}
	const uint bits = asuint(depth);
	// Match the qualified native D32 conversion before both scene rejection and
	// private-depth admission. This is format conversion, not an equality epsilon.
	return bits < 0x00800000u || bits == 0x80000000u ? 0u : bits;
}

bool GIProbeDepthAdmitted(float depth, bool reversed)
{
	const uint bits = GIProbeComparableDepthBits(depth);
	return reversed ? bits > 0u && bits <= 0x3f800000u : bits < 0x3f800000u;
}

// Both operands use the qualified D32 comparison domain. Unsigned ordering
// keeps adjacent normal values distinct and rejects negative/NaN/Inf.
// Scene equality is visible; the separate private probe depth test stays strict.
bool GIProbeSceneDepthVisible(float candidate, float scene, bool reversed)
{
	const uint probeBits = GIProbeComparableDepthBits(candidate);
	const uint sceneBits = GIProbeComparableDepthBits(scene);
	return GIProbeDepthAdmitted(candidate, reversed) && sceneBits <= 0x3f800000u &&
		(reversed ? probeBits >= sceneBits : probeBits <= sceneBits);
}
