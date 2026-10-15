#pragma once
#include <Common/NonFiniteSiteIds.hlsli>

// The counter binding and 32-byte records match NonFiniteCounterABI.h.
#if defined(GGLAB_NONFINITE_COUNTERS)
RWByteAddressBuffer g_NonFiniteCounters : register(u31);

void NonFiniteIncrement(uint address, uint overflowAddress)
{
	uint previous;
	g_NonFiniteCounters.InterlockedAdd(address, 1u, previous);
	if (previous == 0xffffffffu)
	{
		g_NonFiniteCounters.InterlockedOr(overflowAddress, 1u);
	}
}
#endif

void NonFiniteRecordChannel(uint site, float value, bool negativeClamp, bool maximumClamp)
{
#if defined(GGLAB_NONFINITE_COUNTERS)
	const uint address = 32u + (GGLAB_NONFINITE_PROGRAM_INDEX * NF_SITE_COUNT + site) * 32u;
	const uint bits = asuint(value);
	if ((bits & 0x7f800000u) == 0x7f800000u)
	{
		NonFiniteIncrement(address, address + 20u);
	}
	else if (negativeClamp && value < 0.0)
	{
		NonFiniteIncrement(address + 4u, address + 20u);
		g_NonFiniteCounters.InterlockedMax(address + 12u, asuint(-value));
	}
	else if (maximumClamp && value > 65504.0)
	{
		NonFiniteIncrement(address + 8u, address + 20u);
		g_NonFiniteCounters.InterlockedMax(address + 16u, bits);
	}
#endif
}

// Observe the original operand without changing the guard's value or control flow.
float NonFiniteObserveValue(uint site, float value)
{
	NonFiniteRecordChannel(site, value, false, false);
	return value;
}
float2 NonFiniteObserveValue(uint site, float2 value)
{
	NonFiniteObserveValue(site, value.x);
	NonFiniteObserveValue(site, value.y);
	return value;
}
float3 NonFiniteObserveValue(uint site, float3 value)
{
	NonFiniteObserveValue(site, value.x);
	NonFiniteObserveValue(site, value.y);
	NonFiniteObserveValue(site, value.z);
	return value;
}
float4 NonFiniteObserveValue(uint site, float4 value)
{
	NonFiniteObserveValue(site, value.x);
	NonFiniteObserveValue(site, value.y);
	NonFiniteObserveValue(site, value.z);
	NonFiniteObserveValue(site, value.w);
	return value;
}
