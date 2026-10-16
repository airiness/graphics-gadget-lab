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

// Debug verification injection. The counter clear pass writes the frame's request
// after the site records (NonFiniteCounterABI.h): point + 1 (zero is none), IEEE
// bits, x, y and component, followed by the applied count. Ordinary variants have
// no counter buffer, so every injection point returns its operand unchanged.
static const uint NF_INJECTION_ADDRESS = 32u + NF_SITE_COUNT * NF_PROGRAM_COUNT * 32u;

bool NonFiniteInjectionRequested(uint injectionPoint, uint2 location, out uint bits, out uint component)
{
	bits = 0u;
	component = 0u;
#if defined(GGLAB_NONFINITE_COUNTERS)
	const uint4 request = g_NonFiniteCounters.Load4(NF_INJECTION_ADDRESS);
	if (request.x == injectionPoint + 1u && request.z == location.x && request.w == location.y)
	{
		bits = request.y;
		component = g_NonFiniteCounters.Load(NF_INJECTION_ADDRESS + 16u);
		return true;
	}
#endif
	return false;
}

void NonFiniteRecordInjection()
{
#if defined(GGLAB_NONFINITE_COUNTERS)
	uint previous;
	g_NonFiniteCounters.InterlockedAdd(NF_INJECTION_ADDRESS + 20u, 1u, previous);
#endif
}

// Replaces one component of a point's operand at one location with the requested value.
float NonFiniteInject(uint injectionPoint, uint2 location, float value)
{
	uint bits;
	uint component;
	if (NonFiniteInjectionRequested(injectionPoint, location, bits, component) && component == 0u)
	{
		NonFiniteRecordInjection();
		value = asfloat(bits);
	}
	return value;
}
float2 NonFiniteInject(uint injectionPoint, uint2 location, float2 value)
{
	uint bits;
	uint component;
	if (NonFiniteInjectionRequested(injectionPoint, location, bits, component) && component < 2u)
	{
		NonFiniteRecordInjection();
		value[component] = asfloat(bits);
	}
	return value;
}
float3 NonFiniteInject(uint injectionPoint, uint2 location, float3 value)
{
	uint bits;
	uint component;
	if (NonFiniteInjectionRequested(injectionPoint, location, bits, component) && component < 3u)
	{
		NonFiniteRecordInjection();
		value[component] = asfloat(bits);
	}
	return value;
}
float4 NonFiniteInject(uint injectionPoint, uint2 location, float4 value)
{
	uint bits;
	uint component;
	if (NonFiniteInjectionRequested(injectionPoint, location, bits, component) && component < 4u)
	{
		NonFiniteRecordInjection();
		value[component] = asfloat(bits);
	}
	return value;
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
