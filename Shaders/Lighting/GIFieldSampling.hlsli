#pragma once
#include <Lighting/GIShaderConstants.hlsli>

// Matches Graphics/Pipeline/GIFieldSampling.h, including every 16-byte row.
struct GIFieldSamplingParameters
{
	uint3 Counts;
	uint IrradianceInteriorWidth;
	float3 Minimum;
	float Spacing;
	uint Irradiance;
	uint Distance;
	uint Classification;
	float ViewBias;
};

struct GIFieldQuery
{
	float3 Irradiance;
	float Coverage;
	float Weight;
	uint Support;
	uint Compatible;
};

float2 GIFieldOctahedralUV(float3 direction)
{
	direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
	float2 uv = direction.xy;
	if (direction.z < 0.0)
	{
		uv = (1.0 - abs(uv.yx)) * float2(uv.x >= 0.0 ? 1.0 : -1.0,
			uv.y >= 0.0 ? 1.0 : -1.0);
	}
	return uv * 0.5 + 0.5;
}

float3 GIFieldAtlasPosition(uint3 probe, float3 direction, uint interior)
{
	const uint width = interior + 2 * GIProbeBorderWidth;
	const float2 texel = (float2)(probe.xz * width + GIProbeBorderWidth) +
		GIFieldOctahedralUV(direction) * (float)(interior);
	return float3(texel - 0.5, (float)(probe.y));
}

float3 GIFieldIrradiance(Texture2DArray<float4> atlas, uint3 probe, float3 direction, uint interior)
{
	const float3 position = GIFieldAtlasPosition(probe, direction, interior);
	const int3 texel = (int3)(floor(position));
	const float2 fraction = frac(position.xy);
	return lerp(lerp(atlas.Load(int4(texel, 0)).rgb,
		atlas.Load(int4(texel + int3(1, 0, 0), 0)).rgb, fraction.x),
		lerp(atlas.Load(int4(texel + int3(0, 1, 0), 0)).rgb,
			atlas.Load(int4(texel + int3(1, 1, 0), 0)).rgb, fraction.x), fraction.y);
}

float2 GIFieldDistanceStatistics(Texture2DArray<float2> atlas, uint3 probe, float3 direction)
{
	const float3 position = GIFieldAtlasPosition(probe, direction, GIProbeDistanceInteriorWidth);
	const int3 texel = (int3)(floor(position));
	const float2 fraction = frac(position.xy);
	const float2 a = atlas.Load(int4(texel, 0));
	const float2 b = atlas.Load(int4(texel + int3(1, 0, 0), 0));
	const float2 c = atlas.Load(int4(texel + int3(0, 1, 0), 0));
	const float2 d = atlas.Load(int4(texel + int3(1, 1, 0), 0));
	const float4 means = float4(a.x, b.x, c.x, d.x);
	const float4 squares = float4(a.y, b.y, c.y, d.y);
	const float4 weights = float4((1.0 - fraction.x) * (1.0 - fraction.y),
		fraction.x * (1.0 - fraction.y), (1.0 - fraction.x) * fraction.y, fraction.x * fraction.y);
	const float mean = dot(weights, means);
	// Explicit filtering avoids sampler subtexel quantization in cubed visibility.
	// The total-variance identity also avoids subtracting two interpolated large
	// moments; small variance errors would be amplified again by weight crushing.
	const float4 delta = means - mean;
	const float variance = dot(weights, mad(-means, means, squares) + delta * delta);
	return float2(mean, variance);
}

GIFieldQuery SampleGIField(GIFieldSamplingParameters field, float3 position,
	float3 shadingNormal, float3 vertexNormal, float3 viewDirection)
{
	GIFieldQuery result = (GIFieldQuery)(0);
	result.Support = GIFieldOutsideCoverage;
	result.Compatible = 1;
	if (field.IrradianceInteriorWidth == 0)
	{
		return result;
	}
	Texture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(field.Classification)];
	if ((classification.Load(int4(0, 0, 0, 0)) & GIProbeFieldInvalidBit) != 0)
	{
		// Never consume partially written, invalid in-place contents. The CPU
		// readback rejects production preparation once completion is observed.
		// Diagnostic markers distinguish this contract failure from query support.
		result.Coverage = 1.0;
		result.Support = 0;
		result.Compatible = 0;
		return result;
	}
	// Coverage belongs to the surface, not its biased visibility-test position.
	const float3 grid = (position - field.Minimum) / field.Spacing;
	const float3 edge = min(grid, (float3)(field.Counts - 1) - grid);
	const float edgeWeight = saturate(min(edge.x, min(edge.y, edge.z)));
	result.Coverage = edgeWeight * edgeWeight * (3.0 - 2.0 * edgeWeight);
	if (result.Coverage == 0.0)
	{
		return result;
	}
	const float3 biased = position + field.Spacing * (0.1 * vertexNormal + field.ViewBias * viewDirection);
	const float3 cage = clamp((biased - field.Minimum) / field.Spacing,
		0.0.xxx, (float3)(field.Counts - 1));
	const uint3 lower = min((uint3)(floor(cage)), field.Counts - 2);
	const float3 fraction = saturate(cage - (float3)(lower));
	Texture2DArray<float4> irradiance = ResourceDescriptorHeap[NonUniformResourceIndex(field.Irradiance)];
	Texture2DArray<float2> distance = ResourceDescriptorHeap[NonUniformResourceIndex(field.Distance)];
	[unroll]
	for (uint corner = 0; corner < 8; ++corner)
	{
		const uint3 offset = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
		const uint3 probe = lower + offset;
		if (classification.Load(int4(probe.xzy, 0)) != GIProbeActive)
		{
			continue;
		}
		const float3 probePosition = field.Minimum + field.Spacing * (float3)(probe);
		const float3 toProbe = probePosition - position;
		const float3 directionToProbe = toProbe / max(length(toProbe), 1.0e-10);
		const float3 trilinear = lerp(1.0 - fraction, fraction, (float3)(offset));
		const float backface = (dot(directionToProbe, shadingNormal) + 1.0) * 0.5;
		float weight = trilinear.x * trilinear.y * trilinear.z * (backface * backface + 0.2);
		const float3 probeToBiased = biased - probePosition;
		const float receiverDistance = length(probeToBiased);
		const float3 visibilityDirection = receiverDistance > 1.0e-10 ?
			probeToBiased / receiverDistance : shadingNormal;
		const float2 statistics = GIFieldDistanceStatistics(distance, probe, visibilityDirection);
		if (receiverDistance > statistics.x)
		{
			const float variance = max(statistics.y,
				1.0e-4 * field.Spacing * field.Spacing);
			const float delta = receiverDistance - statistics.x;
			const float visibility = variance / (variance + delta * delta);
			weight *= visibility * visibility * visibility;
		}
		if (weight < 0.2)
		{
			weight = weight * weight * weight / (0.2 * 0.2);
		}
		result.Weight += weight;
		result.Irradiance += weight * GIFieldIrradiance(irradiance, probe, shadingNormal,
			field.IrradianceInteriorWidth);
	}
	if (result.Weight == 0.0)
	{
		result.Support = GIFieldNoValidSupport;
		return result;
	}
	// Divide the weighted sum first: pi/W can overflow for a valid tiny cage.
	result.Irradiance = (result.Irradiance / result.Weight) * 3.14159265358979323846;
	result.Support = result.Weight < 0.05 ? GIFieldLowConfidence : GIFieldValid;
	return result;
}
