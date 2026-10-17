#include <Common/BufferLayout.hlsli>
#include <Common/TemporalAA.hlsli>
#include <Common/FullscreenTriangle.hlsli>

// Call production HLSL on the GPU. Finite guards are not folded by every DXC target.
// Each probe writes zero on success and one on failure to an RGBA32F target.
bool NearAccumulation(float actual, float expected)
{
	return abs(actual - expected) <= 2.0e-6;
}

float4 TestZeroFeedbackWeight() : SV_Target0
{
	const float capacity = ResolveTemporalAAMaxHistorySamples(0.0);
	bool matches = capacity == 0.0;
	[unroll]
	for (uint i = 0; i < 3; ++i)
	{
		const float samples = i == 0 ? 1.0 : (i == 1 ? 32.0 : 255.0);
		matches = matches && ComputeTemporalWeightedHistoryWeight(samples, 1.0, capacity, 1.0) == 0.0;
		matches = matches && ComputeTemporalWeightedHistoryWeight(samples, 1.0, capacity, 0.25) == 0.0;
		matches = matches && ComputeTemporalWeightedHistoryWeight(samples, 1.0, capacity, 0.0) == 0.0;
		matches = matches && ComputeTemporalHistoryWeight(samples, 1.0, 0.0) == 0.0;
	}
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestFractionalHistoryCapacity() : SV_Target0
{
	return NearAccumulation(ResolveTemporalAAMaxHistorySamples(0.2), 0.25) &&
		NearAccumulation(ResolveTemporalAAMaxHistorySamples(0.25), 1.0 / 3.0) &&
		ResolveTemporalAAMaxHistorySamples(0.5) == 1.0 ?
			0.0.xxxx :
			1.0.xxxx;
}

float4 TestUnitSampleMatchesNativeFeedback() : SV_Target0
{
	const float feedbacks[7] = { 0.0, 1.0 / 65535.0, 0.01, 0.25, 0.5, 0.97, 0.99 };
	bool matches = true;
	[unroll]
	for (uint i = 0; i < 7; ++i)
	{
		[unroll]
		for (uint j = 0; j < 3; ++j)
		{
			const float samples = j == 0 ? 1.0 : (j == 1 ? 20.0 : 255.0);
			const float capacity = ResolveTemporalAAMaxHistorySamples(feedbacks[i]);
			matches = matches && NearAccumulation(
				ComputeTemporalWeightedHistoryWeight(samples, 0.75, capacity, 1.0),
				ComputeTemporalHistoryWeight(samples, 0.75, feedbacks[i]));
		}
	}
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestLowFeedbackWeightedGoldens() : SV_Target0
{
	const float capacity = ResolveTemporalAAMaxHistorySamples(0.2);
	return NearAccumulation(ComputeTemporalWeightedHistoryWeight(32.0, 1.0, capacity, 1.0), 0.2) &&
		NearAccumulation(ComputeTemporalWeightedHistoryWeight(32.0, 1.0, capacity, 0.25), 0.5) &&
		NearAccumulation(ComputeTemporalWeightedHistoryWeight(32.0, 0.5, capacity, 0.25), 0.25) &&
		ComputeTemporalWeightedHistoryWeight(32.0, 0.0, capacity, 0.25) == 0.0 ?
			0.0.xxxx :
			1.0.xxxx;
}

float4 TestLowCapacityStorageRemainsValid() : SV_Target0
{
	const float capacities[4] = { 0.0, 0.25, 0.75, 1.0 };
	bool matches = true;
	[unroll]
	for (uint i = 0; i < 4; ++i)
	{
		const float next = ResolveTemporalHistoryNextSamples(true, 32.0, 1.0, capacities[i], 0.25);
		matches = matches && next == 1.0 && IsTemporalHistoryAccumulationValid(next);
		matches = matches && ResolveTemporalAAOutputAlphas(next).y == 1.0;
		matches = matches && ResolveTemporalAAHistorySamplesPreview(next, capacities[i]) == 0.0;
	}
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestRejectedAccumulationRestarts() : SV_Target0
{
	const float capacity = ResolveTemporalAAMaxHistorySamples(0.2);
	const float nan = asfloat(0x7fc00000u);
	return ResolveTemporalHistoryNextSamples(false, 32.0, 1.0, capacity, 0.25) == 1.0 &&
		ResolveTemporalHistoryNextSamples(true, 0.0, 1.0, capacity, 0.25) == 1.0 &&
		ResolveTemporalHistoryNextSamples(true, 32.0, nan, capacity, 0.25) == 1.0 &&
		ComputeTemporalWeightedHistoryWeight(0.0, 1.0, capacity, 0.25) == 0.0 &&
		ComputeTemporalWeightedHistoryWeight(256.0, 1.0, capacity, 0.25) == 0.0 &&
		ComputeTemporalWeightedHistoryWeight(32.0, nan, capacity, 0.25) == 0.0 ?
			0.0.xxxx :
			1.0.xxxx;
}

float4 TestFeedbackMonotonicity() : SV_Target0
{
	const float feedbacks[8] = { 0.0, 1.0 / 65535.0, 0.01, 0.25, 0.499, 0.5, 0.501, 0.97 };
	bool matches = true;
	[unroll]
	for (uint j = 0; j < 3; ++j)
	{
		const float sampleWeight = j == 0 ? 0.001 : (j == 1 ? 0.25 : 1.0);
		float previous = -1.0;
		[unroll]
		for (uint i = 0; i < 8; ++i)
		{
			const float weight = ComputeTemporalWeightedHistoryWeight(255.0, 1.0,
				ResolveTemporalAAMaxHistorySamples(feedbacks[i]), sampleWeight);
			matches = matches && isfinite(weight) && weight >= 0.0 && weight < 1.0 && weight > previous;
			previous = weight;
		}
	}
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestPackedLowFeedback() : SV_Target0
{
	const float minimumFeedback = UnpackTemporalAAUnitRangePair(1u).x;
	const float quarterFeedback = UnpackTemporalAAUnitRangePair(16384u).x;
	const float minimumWeight = ComputeTemporalWeightedHistoryWeight(32.0, 1.0,
		ResolveTemporalAAMaxHistorySamples(minimumFeedback), 0.001);
	return UnpackTemporalAAUnitRangePair(0u).x == 0.0 &&
		minimumWeight > 0.0 && minimumWeight < 0.016 &&
		NearAccumulation(ComputeTemporalWeightedHistoryWeight(32.0, 1.0,
			ResolveTemporalAAMaxHistorySamples(quarterFeedback), 1.0), quarterFeedback)
		? 0.0.xxxx : 1.0.xxxx;
}

float4 TestDefaultFeedbackCompatibility() : SV_Target0
{
	const float feedback = UnpackTemporalAAUnitRangePair(0xf851u).x;
	const float capacity = ResolveTemporalAAMaxHistorySamples(feedback);
	return NearAccumulation(ComputeTemporalWeightedHistoryWeight(20.0, 1.0, capacity, 0.25), 80.0 / 81.0) &&
		ResolveTemporalHistoryNextSamples(true, 20.0, 1.0, capacity, 0.25) == 20.25 &&
		ResolveTemporalHistoryNextSamples(true, 20.0, 0.5, capacity, 1.0) == 11.0 &&
		NearAccumulation(ComputeTemporalWeightedHistoryWeight(255.0, 1.0, capacity, 1.0), feedback) &&
		ComputeTemporalWeightedHistoryWeight(255.0, 1.0, capacity, 0.001) > 0.999 ?
			0.0.xxxx :
			1.0.xxxx;
}

float4 TestFeedbackCapacityTransition() : SV_Target0
{
	const float disabled = ResolveTemporalAAMaxHistorySamples(0.0);
	const float next = ResolveTemporalHistoryNextSamples(true, 32.0, 1.0, disabled, 0.25);
	const float enabled = ResolveTemporalAAMaxHistorySamples(0.97);
	return next == 1.0 &&
		NearAccumulation(ComputeTemporalWeightedHistoryWeight(next, 1.0, enabled, 0.25), 0.8) &&
		ResolveTemporalHistoryNextSamples(true, next, 1.0, enabled, 0.25) == 1.25 ?
			0.0.xxxx :
			1.0.xxxx;
}

float4 TestInvalidFeedbackHasNoCapacity() : SV_Target0
{
	return ResolveTemporalAAMaxHistorySamples(asfloat(0x7fc00000u)) == 0.0 &&
		ResolveTemporalAAMaxHistorySamples(asfloat(0x7f800000u)) == 0.0 &&
		ResolveTemporalAAMaxHistorySamples(-1.0) == 0.0 ?
			0.0.xxxx :
			1.0.xxxx;
}

FullscreenTriangleVSOutput VSMain(uint vertexId : SV_VertexID)
{
	return FullscreenTriangleVS(vertexId);
}

float4 PSMain(FullscreenTriangleVSOutput input) : SV_Target0
{
	switch ((uint)(input.PositionCS.x))
	{
	case 0:
		return TestZeroFeedbackWeight();
	case 1:
		return TestFractionalHistoryCapacity();
	case 2:
		return TestUnitSampleMatchesNativeFeedback();
	case 3:
		return TestLowFeedbackWeightedGoldens();
	case 4:
		return TestLowCapacityStorageRemainsValid();
	case 5:
		return TestRejectedAccumulationRestarts();
	case 6:
		return TestFeedbackMonotonicity();
	case 7:
		return TestPackedLowFeedback();
	case 8:
		return TestDefaultFeedbackCompatibility();
	case 9:
		return TestFeedbackCapacityTransition();
	case 10:
		return TestInvalidFeedbackHasNoCapacity();
	default:
		return 1.0.xxxx;
	}
}
