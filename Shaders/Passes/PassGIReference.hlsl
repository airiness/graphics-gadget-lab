#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/GIHitLighting.hlsli>

cbuffer Parameters : register(b2)
{
	uint DepthIndex;
	uint FaceIndex;
	uint ShadingIndex;
	uint IrradianceIndex;
	uint DiagnosticsIndex;
	uint PositionIndex;
	uint IdentityIndex;
	uint ViewIndex;
	uint Width;
	uint Height;
	uint InstanceCount;
	uint GeometryCount;
	uint MaterialCount;
	uint SampleCount;
	uint Seed;
	uint ConstantEnvironment;
	float3 ConstantRadiance;
	float MaximumDistance;
	uint TransmittanceIndex;
	uint AtmosphereSampler;
	uint ReceiverAlbedoIndex;
	uint LambertRadianceIndex;
	uint SunIrradianceIndex;
	uint EnvironmentIrradianceIndex;
	uint EnvironmentFilter;
	uint RowOffset;
	uint4 PreviousIndices;
	uint4 NextIndices;
	uint SampleBegin;
	uint ObservationEnabled;
	uint MaximumDepth;
	uint PathDiagnosticsIndex;
	uint2 PreviousCompensationIndices;
	uint2 NextCompensationIndices;
	uint2 TileOrigin;
	uint2 TileExtent;
	uint BeginSweep;
	uint EndSweep;
	uint RasterPositionIndex;
	uint TilePadding;
};

uint GIHash(uint value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float GIRandom(inout uint state)
{
	state = GIHash(state + 0x9e3779b9u);
	// 23 bits keep the half-unit representable: neither endpoint can round to 1.
	return ((float)(state >> 9) + 0.5) / 8388608.0;
}

float GISecondaryRandom(uint pixel, uint sampleIndex, uint bounce, uint dimension)
{
	// A separate counter stream leaves the serial primary sequence unchanged.
	// Neither maximum depth nor frame/submission partitioning enters this key.
	uint state = GIHash(Seed ^ 0xa511e9b3u);
	state = GIHash(state ^ GIHash(pixel));
	state = GIHash(state ^ GIHash(sampleIndex));
	state = GIHash(state ^ GIHash(bounce));
	state = GIHash(state ^ GIHash(dimension));
	return ((float)(state >> 9) + 0.5) / 8388608.0;
}

bool GIPathFinite(float3 value)
{
	// Rejection is also counted: an invalid deep sample must not disappear from E.
	return all(isfinite(NonFiniteObserveValue(NF_SITE_PASS_GI_REFERENCE_GIPATHFINITE_ISFINITE, value)));
}

void GIAccumulate(float3 contribution, inout float3 sum, inout float3 compensation)
{
	if (MaximumDepth == 1)
	{
		// Retain the qualified D=1 serial accumulation exactly.
		sum += contribution;
		return;
	}
	// Tiny late-bounce terms otherwise disappear into a large FP32 running sum.
	// Both values persist and settle together, preserving arbitrary batch partitions.
	precise float3 adjusted = contribution - compensation;
	precise float3 next = sum + adjusted;
	precise float3 error = (next - sum) - adjusted;
	compensation = error;
	sum = next;
}

float3 GICosineDirection(float3 normal, float2 sample)
{
	const float3 tangent = normalize(cross(abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0), normal));
	const float3 bitangent = cross(normal, tangent);
	const float radius = sqrt(sample.x);
	float sine;
	float cosine;
	sincos(6.28318530717958647692 * sample.y, sine, cosine);
	return tangent * (radius * cosine) + bitangent * (radius * sine) + normal * sqrt(1.0 - sample.x);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint2 pixel = uint2(id.x, id.y + RowOffset);
	if (any(pixel >= uint2(Width, Height)))
	{
		return;
	}
	RWTexture2D<float4> irradiance = GetRWTexture2DFloat4(IrradianceIndex);
	RWTexture2D<float4> diagnostics = GetRWTexture2DFloat4(DiagnosticsIndex);
	RWTexture2D<float4> positions = GetRWTexture2DFloat4(PositionIndex);
	RWTexture2D<float4> identities = GetRWTexture2DFloat4(IdentityIndex);
	RWTexture2D<float4> lambertRadiance = GetRWTexture2DFloat4(LambertRadianceIndex);
	RWTexture2D<float4> sunIrradiance = GetRWTexture2DFloat4(SunIrradianceIndex);
	RWTexture2D<float4> environmentIrradiance = GetRWTexture2DFloat4(EnvironmentIrradianceIndex);
	RWTexture2D<float4> pathDiagnostics = GetRWTexture2DFloat4(PathDiagnosticsIndex);
	pathDiagnostics[pixel] = 0.0.xxxx;
	sunIrradiance[pixel] = 0.0.xxxx;
	environmentIrradiance[pixel] = 0.0.xxxx;
	lambertRadiance[pixel] = 0.0.xxxx;
	irradiance[pixel] = 0.0.xxxx;
	diagnostics[pixel] = 0.0.xxxx;
	positions[pixel] = 0.0.xxxx;
	identities[pixel] = -1.0.xxxx;
	if (ObservationEnabled != 0 && SampleCount != 0 && BeginSweep != 0)
	{
		// Define every pending pixel on the first submitted tile. Later tiles use
		// ReadWrite access and preserve this version until the complete sweep commits.
		GetRWTexture2DFloat4(NextIndices.x)[pixel] = SampleBegin != 0 ?
			GetTexture2DFloat4(PreviousIndices.x).Load(int3(pixel, 0)) : 0.0.xxxx;
		GetRWTexture2DFloat4(NextIndices.y)[pixel] = SampleBegin != 0 ?
			GetTexture2DFloat4(PreviousIndices.y).Load(int3(pixel, 0)) : 0.0.xxxx;
		GetRWTexture2DFloat4(NextIndices.z)[pixel] = SampleBegin != 0 ?
			GetTexture2DFloat4(PreviousIndices.z).Load(int3(pixel, 0)) : 0.0.xxxx;
		GetRWTexture2DUint(NextIndices.w)[pixel] = SampleBegin != 0 ?
			GetTexture2DUint(PreviousIndices.w).Load(int3(pixel, 0)) : 0u;
		GetRWTexture2DFloat4(NextCompensationIndices.x)[pixel] = SampleBegin != 0 ?
			GetTexture2DFloat4(PreviousCompensationIndices.x).Load(int3(pixel, 0)) : 0.0.xxxx;
		GetRWTexture2DFloat4(NextCompensationIndices.y)[pixel] = SampleBegin != 0 ?
			GetTexture2DFloat4(PreviousCompensationIndices.y).Load(int3(pixel, 0)) : 0.0.xxxx;
	}
	const float4 face = GetTexture2DFloat4(FaceIndex).Load(int3(pixel, 0));
	if (face.w == 0.0)
	{
		return;
	}
	irradiance[pixel] = float4(0.0, 0.0, 0.0, -1.0);
	if (face.w != 1.0)
	{
		return;
	}
	const ViewData view = LoadViewData(ViewIndex);
	const float rawDepth = GetTexture2DFloat(DepthIndex).Load(int3(pixel, 0));
	const float3 positionVS = ReconstructViewPosition(PixelCenterToUV(pixel, uint2(Width, Height)), rawDepth, view.InvProjMat);
	const float3 position = mul(float4(positionVS, 1.0), view.InvViewMat).xyz;
	const uint3 counts = uint3(InstanceCount, GeometryCount, MaterialCount);
	GIHit receiver;
	const float4 rasterPosition = GetTexture2DFloat4(RasterPositionIndex).Load(int3(pixel, 0));
	const uint receiverMatch = rasterPosition.w == face.w &&
		GIPathFinite(rasterPosition.xyz) && GIPathFinite(position) && GIPathFinite(face.xyz) ?
		GIMatchReceiver(pixel, uint2(Width, Height), position, rasterPosition.xyz,
			face.xyz, view, MaximumDistance, counts, receiver) : 3u;
	if (receiverMatch != 1 && receiverMatch != 2 && receiverMatch != 6)
	{
		irradiance[pixel] = float4(0.0, 0.0, 0.0, -2.0);
		return;
	}
	positions[pixel] = float4(receiver.Position, 1.0);
	identities[pixel] = float4((float)(receiver.Instance), (float)(receiver.Primitive),
		receiver.FaceNormal.y, receiverMatch == 6 ? 3.0 : (float)(receiverMatch));
	const float3 normal = normalize(GetTexture2DFloat4(ShadingIndex).Load(int3(pixel, 0)).xyz);
	uint randomState = GIHash(Seed ^ GIHash(pixel.x + Width * pixel.y));
	precise float3 sunSum = 0.0.xxx;
	precise float3 environmentSum = 0.0.xxx;
	precise float3 sunCompensation = 0.0.xxx;
	precise float3 environmentCompensation = 0.0.xxx;
	const float environmentMip = ConstantEnvironment != 0 ? -1.0 : GIEnvironmentMip(EnvironmentFilter);
	uint4 outcomes = 0;
	float invalidPaths = 0.0;
	float truncatedPaths = 0.0;
	if (ObservationEnabled != 0 && SampleBegin != 0)
	{
		const float4 previousSun = GetTexture2DFloat4(PreviousIndices.x).Load(int3(pixel, 0));
		const float4 previousEnvironment = GetTexture2DFloat4(PreviousIndices.y).Load(int3(pixel, 0));
		sunSum = previousSun.rgb;
		environmentSum = previousEnvironment.rgb;
		invalidPaths = previousSun.a;
		truncatedPaths = previousEnvironment.a;
		outcomes = (uint4)(GetTexture2DFloat4(PreviousIndices.z).Load(int3(pixel, 0)));
		randomState = GetTexture2DUint(PreviousIndices.w).Load(int3(pixel, 0));
		sunCompensation = GetTexture2DFloat4(PreviousCompensationIndices.x).Load(int3(pixel, 0)).rgb;
		environmentCompensation = GetTexture2DFloat4(PreviousCompensationIndices.y).Load(int3(pixel, 0)).rgb;
	}
	const bool samplePixel = all(pixel >= TileOrigin) && all(pixel - TileOrigin < TileExtent);
	const uint pixelSamples = samplePixel ? SampleCount : 0u;
	for (uint index = 0; index < pixelSamples; ++index)
	{
		const float u = GIRandom(randomState);
		const float v = GIRandom(randomState);
		float3 direction = GICosineDirection(normal, float2(u, v));
		GIHit current = receiver;
		float3 throughput = 1.0.xxx;
		for (uint bounce = 0; bounce < MaximumDepth; ++bounce)
		{
			const float3 origin = GIHitOrigin(current);
			if (!GIPathFinite(origin) || !GIPathFinite(direction) || !GIPathFinite(throughput))
			{
				invalidPaths += 1.0;
				break;
			}
			// Primary uses the shading normal; continuation uses the vertex normal.
			// Both reject below-face directions without changing the sample denominator.
			if (dot(direction, current.FaceNormal) <= 0.0)
			{
				break;
			}
			RayDesc ray;
			ray.Origin = origin;
			ray.Direction = direction;
			ray.TMin = 0.0;
			ray.TMax = MaximumDistance;
			GIHit hit;
			const uint outcome = GITraceHit(ray, counts, hit, false);
			if (bounce == 0)
			{
				outcomes[outcome] += 1;
			}
			if (outcome == GIHitInvalid)
			{
				invalidPaths += 1.0;
				break;
			}
			if (outcome == GIHitBackface)
			{
				break;
			}
			if (outcome == GIHitMiss)
			{
				const float3 environment = EvaluateGIMiss(direction,
					ConstantEnvironment != 0, ConstantRadiance, environmentMip);
				const float3 contribution = throughput * environment;
				if (!GIPathFinite(environment) || !GIPathFinite(contribution) || any(environment < 0.0))
				{
					invalidPaths += 1.0;
					break;
				}
				GIAccumulate(contribution, environmentSum, environmentCompensation);
				break;
			}
			if (!GIPathFinite(hit.Position) || !GIPathFinite(hit.FaceNormal) ||
				!GIPathFinite(hit.VertexNormal) || !GIPathFinite(hit.Albedo) ||
				!GIPathFinite(hit.OriginError.xxx) || any(hit.Albedo < 0.0))
			{
				invalidPaths += 1.0;
				break;
			}
			// This helper already contains the current hit's albedo / pi.
			const float3 sunlight = EvaluateGIHitSun(hit, MaximumDistance, TransmittanceIndex, AtmosphereSampler);
			const float3 contribution = throughput * sunlight;
			if (!GIPathFinite(sunlight) || !GIPathFinite(contribution) || any(sunlight < 0.0))
			{
				invalidPaths += 1.0;
				break;
			}
			GIAccumulate(contribution, sunSum, sunCompensation);
			if (bounce + 1 == MaximumDepth)
			{
				// A final surface contributes sun NEE, never an environment substitute.
				truncatedPaths += 1.0;
				break;
			}
			throughput *= hit.Albedo;
			if (bounce == 0)
			{
				// Debug qualification injects only after a valid first hit/NEE, so the
				// persistent deep-invalid flag is tested with clean first-query outcomes.
				throughput = NonFiniteInject(NF_INJECT_PASS_GI_REFERENCE_CSMAIN_THROUGHPUT, pixel, throughput);
			}
			current = hit;
			const uint sampleIndex = SampleBegin + index;
			const uint pixelIndex = pixel.x + Width * pixel.y;
			direction = GICosineDirection(hit.VertexNormal, float2(
				GISecondaryRandom(pixelIndex, sampleIndex, bounce + 1, 0),
				GISecondaryRandom(pixelIndex, sampleIndex, bounce + 1, 1)));
		}
	}
	// Scene-referred E: no pre-exposure, receiver albedo, direct receiver sun, TAA,
	// denoiser or cross-observation reuse. Sums and primary RNG preserve sample order.
	if (ObservationEnabled != 0 && pixelSamples != 0)
	{
		GetRWTexture2DFloat4(NextIndices.x)[pixel] = float4(sunSum, invalidPaths);
		GetRWTexture2DFloat4(NextIndices.y)[pixel] = float4(environmentSum, truncatedPaths);
		GetRWTexture2DFloat4(NextIndices.z)[pixel] = (float4)(outcomes);
		GetRWTexture2DUint(NextIndices.w)[pixel] = randomState;
		GetRWTexture2DFloat4(NextCompensationIndices.x)[pixel] = float4(sunCompensation, 0.0);
		GetRWTexture2DFloat4(NextCompensationIndices.y)[pixel] = float4(environmentCompensation, 0.0);
	}
	if (ObservationEnabled != 0 && SampleCount != 0)
	{
		if (EndSweep != 0 && !samplePixel)
		{
			// These pixels were completed by earlier tiles of the same pending sweep.
			const float4 pendingSun = GetRWTexture2DFloat4(NextIndices.x)[pixel];
			const float4 pendingEnvironment = GetRWTexture2DFloat4(NextIndices.y)[pixel];
			sunSum = pendingSun.rgb;
			environmentSum = pendingEnvironment.rgb;
			invalidPaths = pendingSun.a;
			truncatedPaths = pendingEnvironment.a;
			outcomes = (uint4)(GetRWTexture2DFloat4(NextIndices.z)[pixel]);
		}
		else if (EndSweep == 0)
		{
			// A partial sweep cannot change the whole-image estimator's denominator.
			sunSum = 0.0.xxx;
			environmentSum = 0.0.xxx;
			invalidPaths = 0.0;
			truncatedPaths = 0.0;
			outcomes = 0;
			if (SampleBegin != 0)
			{
				const float4 committedSun = GetTexture2DFloat4(PreviousIndices.x).Load(int3(pixel, 0));
				const float4 committedEnvironment = GetTexture2DFloat4(PreviousIndices.y).Load(int3(pixel, 0));
				sunSum = committedSun.rgb;
				environmentSum = committedEnvironment.rgb;
				invalidPaths = committedSun.a;
				truncatedPaths = committedEnvironment.a;
				outcomes = (uint4)(GetTexture2DFloat4(PreviousIndices.z).Load(int3(pixel, 0)));
			}
		}
	}
	const uint resolvedSamples = SampleBegin + (EndSweep != 0 ? SampleCount : 0u);
	const uint totalSamples = max(resolvedSamples, 1u);
	const float validity = resolvedSamples == 0 ?
		0.0 :
		outcomes.w == 0 && invalidPaths == 0.0 ? 1.0 : -3.0;
	const float weight = 3.14159265358979323846 / (float)(totalSamples);
	const float3 sunEstimate = sunSum * weight;
	const float3 environmentEstimate = environmentSum * weight;
	const float3 estimate = sunEstimate + environmentEstimate;
	// Alpha records the actual attenuation/filter path, not receiver validity.
	sunIrradiance[pixel] = float4(sunEstimate, TransmittanceIndex != 0xffffffffu ? 1.0 : 0.0);
	environmentIrradiance[pixel] = float4(environmentEstimate, environmentMip);
	irradiance[pixel] = float4(estimate, validity);
	// Reduced diffuse diagnostic only, not the complete Forward material or Final.
	const float3 albedo = GetTexture2DFloat4(ReceiverAlbedoIndex).Load(int3(pixel, 0)).rgb;
	lambertRadiance[pixel] = float4(albedo * estimate / 3.14159265358979323846, validity);
	diagnostics[pixel] = (float4)(outcomes) / (float)(totalSamples);
	// First-query diagnostics retain their original meaning. These independent
	// channels report any-depth invalid paths, depth truncation and actual depth.
	pathDiagnostics[pixel] = float4(invalidPaths / (float)(totalSamples),
		truncatedPaths / (float)(totalSamples), (float)(MaximumDepth), 1.0);
}
