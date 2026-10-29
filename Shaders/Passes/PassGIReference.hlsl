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
	sunIrradiance[pixel] = 0.0.xxxx;
	environmentIrradiance[pixel] = 0.0.xxxx;
	lambertRadiance[pixel] = 0.0.xxxx;
	irradiance[pixel] = 0.0.xxxx;
	diagnostics[pixel] = 0.0.xxxx;
	positions[pixel] = 0.0.xxxx;
	identities[pixel] = -1.0.xxxx;
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
	const float distance = length(position - view.CameraPos.xyz);
	const float tolerance = max(0.002, distance * 0.0002);
	RayDesc primary;
	primary.Origin = view.CameraPos.xyz;
	primary.Direction = normalize(position - primary.Origin);
	primary.TMin = max(0.001, distance - tolerance);
	primary.TMax = distance + tolerance;
	const uint3 counts = uint3(InstanceCount, GeometryCount, MaterialCount);
	GIHit receiver;
	uint receiverOutcome = GITraceHit(primary, counts, receiver, true);
	bool receiverFallback = false;
	if (receiverOutcome == GIHitMiss)
	{
		// Raster edge coverage is quantized to subpixels; an ideal centre ray
		// can miss a covered silhouette. Search only a 1/128-pixel envelope,
		// retaining the raster face/depth match and an explicit fallback marker.
		// This never applies to secondary rays or converts their hits into sky.
		const float footprint = 2.0 * abs(positionVS.z * view.InvProjMat[1][1]) / (float)(Height);
		const float epsilon = max(2.0e-6, footprint / 128.0);
		const float3 right = mul(float4(1, 0, 0, 0), view.InvViewMat).xyz;
		const float3 up = mul(float4(0, 1, 0, 0), view.InvViewMat).xyz;
		const float2 offsets[8] =
		{
			float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1),
			float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
		};
		float nearest = tolerance + 4.0 * epsilon;
		for (uint attempt = 0; attempt < 8; ++attempt)
		{
			RayDesc shifted = primary;
			const float3 target = position + epsilon *
				(right * offsets[attempt].x + up * offsets[attempt].y);
			shifted.Direction = normalize(target - primary.Origin);
			GIHit candidate;
			const uint outcome = GITraceHit(shifted, counts, candidate, true);
			const float error = length(candidate.Position - position);
			if (outcome == GIHitSurface && dot(candidate.FaceNormal, face.xyz) > 0.9999 && error < nearest)
			{
				nearest = error;
				receiver = candidate;
				receiverOutcome = outcome;
				receiverFallback = true;
			}
		}
	}
	if (receiverOutcome != GIHitSurface)
	{
		irradiance[pixel] = float4(0.0, 0.0, 0.0, -2.0);
		return;
	}
	positions[pixel] = float4(receiver.Position, 1.0);
	identities[pixel] = float4((float)(receiver.Instance), (float)(receiver.Primitive), receiver.FaceNormal.y, receiverFallback ? 2.0 : 1.0);
	const float3 normal = normalize(GetTexture2DFloat4(ShadingIndex).Load(int3(pixel, 0)).xyz);
	uint randomState = GIHash(Seed ^ GIHash(pixel.x + Width * pixel.y));
	float3 sunSum = 0.0.xxx;
	float3 environmentSum = 0.0.xxx;
	const float environmentMip = ConstantEnvironment != 0 ? -1.0 : GIEnvironmentMip(EnvironmentFilter);
	uint4 outcomes = 0;
	for (uint index = 0; index < SampleCount; ++index)
	{
		const float u = GIRandom(randomState);
		const float v = GIRandom(randomState);
		const float3 direction = GICosineDirection(normal, float2(u, v));
		// Cosine sampling uses the shading normal, but directions below the geometric
		// plane contribute zero. They remain in the estimator's denominator.
		if (dot(direction, receiver.FaceNormal) <= 0.0)
		{
			continue;
		}
		RayDesc ray;
		ray.Origin = GIHitOrigin(receiver);
		ray.Direction = direction;
		ray.TMin = 0.0;
		ray.TMax = MaximumDistance;
		GIHit hit;
		const uint outcome = GITraceHit(ray, counts, hit, false);
		outcomes[outcome] += 1;
		if (outcome == GIHitMiss)
		{
			environmentSum += EvaluateGIMiss(direction, ConstantEnvironment != 0, ConstantRadiance, environmentMip);
		}
		else if (outcome == GIHitSurface)
		{
			sunSum += EvaluateGIHitSun(hit, MaximumDistance, TransmittanceIndex, AtmosphereSampler);
		}
	}
	// Scene-referred E: no pre-exposure, receiver albedo, direct receiver sun, TAA,
	// denoiser or inter-frame sum. Replaying a seed is independent of submission count.
	const float validity = outcomes.w == 0 ? 1.0 : -3.0;
	const float weight = 3.14159265358979323846 / (float)(SampleCount);
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
	diagnostics[pixel] = (float4)(outcomes) / (float)(SampleCount);
}
