#include <Common/BufferLayout.hlsli>
#include <Common/TemporalAA.hlsli>

// Exercise the production HLSL, rather than a CPU copy of its implementation.
// Each entry returns zero on success; the integration suite inspects optimized DXIL.
float4 CheckClip(float3 history, float3 mean, float3 boxMin, float3 boxMax, float3 expected)
{
	const float3 clipped = ClipTemporalHistoryTowardMean(history, mean, boxMin, boxMax);
	// A comparison with finite expected values also rejects NaN and infinity.
	const bool matches = all(abs(clipped - expected) <= 1.0e-6.xxx);
	return matches ? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClipPreservesGrayHistory() : SV_Target0
{
	return CheckClip(float3(0.75, 0, 0), float3(0.5, 0, 0),
		float3(0.25, 0, 0), float3(1, 0, 0), float3(0.75, 0, 0));
}

float4 TestClipGrayHistoryAtBoundary() : SV_Target0
{
	return CheckClip(float3(2, 0, 0), float3(0.5, 0, 0),
		float3(0.25, 0, 0), float3(1, 0, 0), float3(1, 0, 0));
}

float4 TestClipRejectsOutsideCollapsedAxis() : SV_Target0
{
	// Nonzero chroma outside a zero-width chroma interval must still constrain Y.
	return CheckClip(float3(0.75, 0.25, 0), float3(0.5, 0, 0),
		float3(0.25, 0, 0), float3(1, 0, 0), float3(0.5, 0, 0));
}

float4 TestClipMeanIsFinite() : SV_Target0
{
	return CheckClip(float3(0.5, -0.25, 0.25), float3(0.5, -0.25, 0.25),
		float3(0.5, -0.25, 0.25), float3(0.5, -0.25, 0.25), float3(0.5, -0.25, 0.25));
}

float4 TestClipPointBox() : SV_Target0
{
	return CheckClip(float3(2, -1, 4), 0.0.xxx, 0.0.xxx, 0.0.xxx, 0.0.xxx);
}

float4 TestClipAsymmetricPositiveBoundary() : SV_Target0
{
	// The y upper plane is reached first at t=1/4; x must not be independently clamped.
	return CheckClip(float3(2, 4, -2), 0.0.xxx,
		float3(-1, -2, -1), float3(1, 1, 2), float3(0.5, 1, -0.5));
}

float4 TestClipAsymmetricNegativeBoundary() : SV_Target0
{
	return CheckClip(float3(-4, 2, 4), 0.0.xxx,
		float3(-1, -2, -1), float3(1, 1, 2), float3(-1, 0.5, 1));
}

float4 TestClipPreservesTinyInsideDirection() : SV_Target0
{
	// Binary-exact values below the old denominator epsilon; normalize the error
	// so an absolute tolerance cannot hide a complete loss of tiny history.
	const float extent = 1.0 / 67108864.0; // 2^-26, a normal float.
	const float3 history = float3(extent * 0.5, 0, 0);
	const float3 actual = ClipTemporalHistoryTowardMean(history, 0.0.xxx, -extent.xxx, extent.xxx);
	return all(abs((actual - history) / extent) <= 1.0e-6.xxx)
		? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClipTinyOutsideDirection() : SV_Target0
{
	const float extent = 1.0 / 67108864.0;
	const float3 actual = ClipTemporalHistoryTowardMean(float3(extent * 2, 0, 0),
		0.0.xxx, float3(-extent, 0, 0), float3(extent, 0, 0));
	return all(abs(actual / extent - float3(1, 0, 0)) <= 1.0e-6.xxx)
		? 0.0.xxxx : 1.0.xxxx;
}

float4 TestClipPlanarAxisPermutations() : SV_Target0
{
	float4 failures = 0.0.xxxx;
	failures += CheckClip(float3(0, 2, 4), 0.0.xxx, float3(0, -1, -1),
		float3(0, 1, 1), float3(0, 0.5, 1));
	failures += CheckClip(float3(4, 0, 2), 0.0.xxx, float3(-1, 0, -1),
		float3(1, 0, 1), float3(1, 0, 0.5));
	failures += CheckClip(float3(2, 4, 0), 0.0.xxx, float3(-1, -1, 0),
		float3(1, 1, 0), float3(0.5, 1, 0));
	return failures;
}

float4 TestClipSegmentProperties() : SV_Target0
{
	const float3 mean = float3(0.5, -0.25, 0.25);
	const float3 boxMin = float3(0.25, -0.25, -0.25);
	const float3 boxMax = float3(1, -0.25, 0.5);
	bool matches = true;
	[unroll]
	for (int x = -2; x <= 2; ++x)
	{
		[unroll]
		for (int z = -2; z <= 2; ++z)
		{
			const float3 history = mean + float3(x, 0, z) * 0.25;
			const float3 actual = ClipTemporalHistoryTowardMean(history, mean, boxMin, boxMax);
			// Independent interval search: find the furthest point in the box on
			// the mean-to-history segment, without reproducing the axis-scale formula.
			float low = 0;
			float high = 1;
			[unroll]
			for (uint iteration = 0; iteration < 20; ++iteration)
			{
				const float middle = (low + high) * 0.5;
				const float3 candidate = lerp(mean, history, middle);
				if (all(candidate >= boxMin) && all(candidate <= boxMax))
				{
					low = middle;
				}
				else
				{
					high = middle;
				}
			}
			const float3 expected = lerp(mean, history, low);
			const float3 twice = ClipTemporalHistoryTowardMean(actual, mean, boxMin, boxMax);
			matches = matches && all(actual >= boxMin) && all(actual <= boxMax) &&
				all(abs(actual - expected) <= 1.0e-6.xxx) && all(abs(twice - actual) <= 1.0e-6.xxx);
		}
	}
	return matches ? 0.0.xxxx : 1.0.xxxx;
}
