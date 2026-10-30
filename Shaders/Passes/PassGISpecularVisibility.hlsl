#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Common/Sampling.hlsli>
#include <Lighting/GIRayScene.hlsli>
#include <PBR/BRDF.hlsli>

cbuffer Parameters : register(b2)
{
	uint4 ReceiverIndices;
	uint2 ReflectionIndices;
	uint DepthIndex;
	uint BrdfLutIndex;
	uint4 OutputIndices;
	uint4 DiagnosticIndices;
	uint4 PreviousIndices;
	uint4 NextIndices;
	uint2 Size;
	uint2 TileOrigin;
	uint2 TileExtent;
	uint ViewIndex;
	uint Seed;
	uint SampleBegin;
	uint SampleCount;
	uint SubmittedSamples;
	uint BeginSweep;
	uint EndSweep;
	uint RowOffset;
	float MaximumDistance;
	uint BrdfSampler;
	uint InstanceCount;
	uint GeometryCount;
	uint MaterialCount;
	uint RasterMaterialCapacity;
};

uint VisibilityHash(uint value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float VisibilityRandom(uint pixel, uint sampleIndex, uint lobe, uint dimension)
{
	uint state = VisibilityHash(Seed ^ 0x681b923du);
	state = VisibilityHash(state ^ VisibilityHash(pixel));
	state = VisibilityHash(state ^ VisibilityHash(sampleIndex));
	state = VisibilityHash(state ^ VisibilityHash(lobe));
	state = VisibilityHash(state ^ VisibilityHash(dimension));
	return ((float)(state >> 9) + 0.5) / 8388608.0;
}

bool VisibilityFinite(float4 value)
{
	return all(isfinite(NonFiniteObserveValue(NF_SITE_GI_SPECULAR_VISIBILITY_FINITE_ISFINITE, value)));
}

void VisibilityAccumulate(float4 value, inout float4 sum, inout float4 compensation)
{
	precise float4 adjusted = value - compensation;
	precise float4 next = sum + adjusted;
	precise float4 error = (next - sum) - adjusted;
	compensation = error;
	sum = next;
}

uint VisibilityReceiver(float3 position, float3 face, ViewData view, out GIHit receiver)
{
	const float distance = length(position - view.CameraPos.xyz);
	const float tolerance = max(0.002, distance * 0.0002);
	RayDesc ray;
	ray.Origin = view.CameraPos.xyz;
	ray.Direction = normalize(position - ray.Origin);
	ray.TMin = max(0.001, distance - tolerance);
	ray.TMax = distance + tolerance;
	const uint3 counts = uint3(InstanceCount, GeometryCount, MaterialCount);
	const uint outcome = GITraceHit(ray, counts, receiver, true);
	if (outcome == GIHitSurface)
	{
		return dot(receiver.FaceNormal, face) > 0.9999 ? 0u : 4u;
	}
	if (outcome != GIHitMiss)
	{
		return 3u;
	}
	// Same bounded raster-silhouette envelope as the diffuse reference. This is
	// primary matching only; visibility rays never search for a replacement miss.
	const float3 positionVS = mul(float4(position, 1), view.ViewMat).xyz;
	const float footprint = 2.0 * abs(positionVS.z * view.InvProjMat[1][1]) / (float)(Size.y);
	const float epsilon = max(2.0e-6, footprint / 128.0);
	const float3 right = mul(float4(1, 0, 0, 0), view.InvViewMat).xyz;
	const float3 up = mul(float4(0, 1, 0, 0), view.InvViewMat).xyz;
	const float2 offsets[8] =
	{
		float2(-1, 0), float2(1, 0), float2(0, -1), float2(0, 1),
		float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
	};
	float nearest = tolerance + 4.0 * epsilon;
	bool matched = false;
	for (uint index = 0; index < 8; ++index)
	{
		ray.Direction = normalize(position + epsilon *
			(right * offsets[index].x + up * offsets[index].y) - ray.Origin);
		GIHit candidate;
		const uint status = GITraceHit(ray, counts, candidate, true);
		const float error = length(candidate.Position - position);
		if (status == GIHitSurface && dot(candidate.FaceNormal, face) > 0.9999 && error < nearest)
		{
			receiver = candidate;
			nearest = error;
			matched = true;
		}
	}
	return matched ? 0u : 5u;
}

float4 VisibilitySample(GIHit receiver, float3 reflection, float roughness,
	uint pixel, uint sampleIndex, uint lobe)
{
	const float2 xi = float2(VisibilityRandom(pixel, sampleIndex, lobe, 0),
		VisibilityRandom(pixel, sampleIndex, lobe, 1));
	const float3 halfVector = TangentToWorld(
		ImportanceSampleGGX(xi, PerceptualRoughnessToAlpha(roughness)), reflection);
	const float3 direction = normalize(2.0 * dot(reflection, halfVector) * halfVector - reflection);
	const float weight = saturate(dot(reflection, direction));
	if (!VisibilityFinite(float4(direction, weight)))
	{
		return float4(0, 0, 0, 1);
	}
	if (weight == 0.0)
	{
		return 0.0.xxxx;
	}
	if (dot(receiver.FaceNormal, direction) <= 0.0)
	{
		// Keep the prefilter kernel denominator, even below the geometric face.
		return float4(0, weight, 1, 0);
	}
	RayDesc ray;
	ray.Origin = GIHitOrigin(receiver);
	ray.Direction = direction;
	ray.TMin = 0;
	ray.TMax = MaximumDistance;
	RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
	query.TraceRayInline(GIRayScene, RAY_FLAG_NONE, 1, ray);
	while (query.Proceed())
	{
	}
	return float4(query.CommittedStatus() == COMMITTED_NOTHING ? weight : 0.0, weight, 0, 0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint2 pixel = uint2(id.x, id.y + RowOffset);
	if (any(pixel >= Size))
	{
		return;
	}
	RWTexture2D<float4> visibility = GetRWTexture2DFloat4(OutputIndices.x);
	RWTexture2D<float4> baseOutput = GetRWTexture2DFloat4(OutputIndices.y);
	RWTexture2D<float4> coatOutput = GetRWTexture2DFloat4(OutputIndices.z);
	RWTexture2D<float4> weightsOutput = GetRWTexture2DFloat4(OutputIndices.w);
	RWTexture2D<float4> positions = GetRWTexture2DFloat4(DiagnosticIndices.x);
	RWTexture2D<float4> diagnostics = GetRWTexture2DFloat4(DiagnosticIndices.y);
	RWTexture2D<float4> f0Output = GetRWTexture2DFloat4(DiagnosticIndices.z);
	RWTexture2D<float4> energyOutput = GetRWTexture2DFloat4(DiagnosticIndices.w);
	visibility[pixel] = 0.0.xxxx;
	baseOutput[pixel] = 0.0.xxxx;
	coatOutput[pixel] = 0.0.xxxx;
	weightsOutput[pixel] = 0.0.xxxx;
	positions[pixel] = 0.0.xxxx;
	diagnostics[pixel] = 0.0.xxxx;
	f0Output[pixel] = 0.0.xxxx;
	energyOutput[pixel] = 0.0.xxxx;
	float4 sums[4];
	[unroll]
	for (uint surface = 0; surface < 4; ++surface)
	{
		sums[surface] = SubmittedSamples != 0 ?
			GetTexture2DFloat4(PreviousIndices[surface]).Load(int3(pixel, 0)) : 0.0.xxxx;
		if (SampleCount != 0 && BeginSweep != 0)
		{
			GetRWTexture2DFloat4(NextIndices[surface])[pixel] = sums[surface];
		}
	}
	const float4 face = GetTexture2DFloat4(ReceiverIndices.x).Load(int3(pixel, 0));
	if (face.w == 0)
	{
		return;
	}
	visibility[pixel] = float4(0, 0, 0, -1);
	diagnostics[pixel] = float4(-1, 0, SubmittedSamples, 0);
	if (face.w != 1 || !VisibilityFinite(face))
	{
		return;
	}
	const ViewData view = LoadViewData(ViewIndex);
	const float depth = GetTexture2DFloat(DepthIndex).Load(int3(pixel, 0));
	const float3 positionVS = ReconstructViewPosition(PixelCenterToUV(pixel, Size), depth, view.InvProjMat);
	const float3 position = mul(float4(positionVS, 1), view.InvViewMat).xyz;
	GIHit receiver;
	if (!VisibilityFinite(float4(position, depth)))
	{
		diagnostics[pixel] = float4(-2, 0, SubmittedSamples, 0);
		return;
	}
	const uint match = VisibilityReceiver(position, face.xyz, view, receiver);
	if (match != 0)
	{
		diagnostics[pixel] = float4(-(float)(match), 0, SubmittedSamples, 0);
		return;
	}
	const float4 shading = GetTexture2DFloat4(ReceiverIndices.y).Load(int3(pixel, 0));
	const float4 diffuse = GetTexture2DFloat4(ReceiverIndices.z).Load(int3(pixel, 0));
	const float4 baseColor = GetTexture2DFloat4(ReceiverIndices.w).Load(int3(pixel, 0));
	const float4 base = GetTexture2DFloat4(ReflectionIndices.x).Load(int3(pixel, 0));
	const float4 coat = GetTexture2DFloat4(ReflectionIndices.y).Load(int3(pixel, 0));
	// DX12 root SRVs do not carry a queryable structured-buffer capacity.
	// The raster table is sparse: live count is not an index bound. Use the
	// capacity of the very buffer bound for this frame, not RayScene slots.
	// sqrt(alpha) in production specular AA may round the clamped minimum
	// one ULP down. Admit that representation without modifying the kernel.
	const float minimumRoughness = asfloat(asuint(MIN_PERCEPTUAL_ROUGHNESS) - 1u);
	if (!VisibilityFinite(shading) || !VisibilityFinite(diffuse) || !VisibilityFinite(baseColor) ||
		!VisibilityFinite(base) || !VisibilityFinite(coat) ||
		baseColor.w < (float)(g_Scene.MaterialBaseIndex) ||
		baseColor.w >= (float)(RasterMaterialCapacity) || baseColor.w != floor(baseColor.w) ||
		dot(base.xyz, base.xyz) < 0.5 || dot(coat.xyz, coat.xyz) < 0.5 ||
		dot(shading.xyz, shading.xyz) < 0.5 || shading.w < 0 || shading.w > 1 ||
		diffuse.w < 0 || diffuse.w > 1 || base.w < minimumRoughness ||
		coat.w < minimumRoughness || base.w > 1 || coat.w > 1)
	{
		diagnostics[pixel] = float4(-6, 0, SubmittedSamples, 0);
		return;
	}
	const MaterialData material = g_Materials[(uint)(baseColor.w)];
	const float ior = material.Ior == 0 ? 0 : max(material.Ior, 1.0);
	const float reflectance = (ior - 1.0) / (ior + 1.0);
	const float3 f0 = lerp((reflectance * reflectance).xxx, baseColor.rgb, shading.w);
	const float3 viewDirection = normalize(view.CameraPos.xyz - position);
	const float noV = saturate(dot(shading.xyz, viewDirection));
	const float coatNoV = saturate(dot(coat.xyz, viewDirection));
	Texture2D<float2> lut = GetTexture2DFloat2(BrdfLutIndex);
	const float2 baseLut = lut.SampleLevel(GetSamplerState(BrdfSampler), float2(noV, base.w), 0);
	const float2 coatLut = lut.SampleLevel(GetSamplerState(BrdfSampler), float2(coatNoV, coat.w), 0);
	const float3 baseAlbedo = saturate((f0 * baseLut.x + baseLut.y) * GGXEnergyCompensation(f0, baseLut));
	const float coatAlbedo = diffuse.w > 0 ?
		saturate((0.04 * coatLut.x + coatLut.y) * GGXEnergyCompensation(0.04.xxx, coatLut).x) : 0;
	const float coatWeight = diffuse.w * coatAlbedo;
	const float transmission = 1.0 - coatWeight;
	const float3 baseWeight = baseAlbedo * transmission * transmission;
	const float baseY = dot(baseWeight, float3(0.2126, 0.7152, 0.0722));
	if (!VisibilityFinite(float4(f0, ior)) || !VisibilityFinite(float4(baseLut, coatLut)) ||
		!VisibilityFinite(float4(baseWeight, coatWeight)))
	{
		return;
	}
	const uint active = (baseY > 0 ? 1u : 0u) | (coatWeight > 0 ? 2u : 0u);
	weightsOutput[pixel] = float4(baseWeight, coatWeight);
	positions[pixel] = float4(receiver.Position, (float)(receiver.Instance));
	f0Output[pixel] = float4(f0, ior);
	energyOutput[pixel] = float4(diffuse.w, coatAlbedo, noV, coatNoV);
	float4 pending[4];
	[unroll]
	for (uint index = 0; index < 4; ++index)
	{
		pending[index] = SampleCount != 0 ? GetRWTexture2DFloat4(NextIndices[index])[pixel] : sums[index];
	}
	const bool samplePixel = all(pixel >= TileOrigin) && all(pixel - TileOrigin < TileExtent);
	const uint count = samplePixel ? SampleCount : 0u;
	for (uint sample = 0; sample < count; ++sample)
	{
		const uint pixelIndex = pixel.x + Size.x * pixel.y;
		if ((active & 1u) != 0)
		{
			VisibilityAccumulate(VisibilitySample(receiver, normalize(base.xyz), base.w,
				pixelIndex, SampleBegin + sample, 0), pending[0], pending[2]);
		}
		if ((active & 2u) != 0)
		{
			VisibilityAccumulate(VisibilitySample(receiver, reflect(-viewDirection, normalize(coat.xyz)),
				coat.w, pixelIndex, SampleBegin + sample, 1), pending[1], pending[3]);
		}
	}
	if (SampleCount != 0)
	{
		[unroll]
		for (uint index = 0; index < 4; ++index)
		{
			if (samplePixel)
			{
				GetRWTexture2DFloat4(NextIndices[index])[pixel] = pending[index];
			}
			if (EndSweep != 0)
			{
				sums[index] = pending[index];
			}
		}
	}
	const uint samples = SampleCount != 0 && EndSweep != 0 ? SampleBegin + SampleCount : SubmittedSamples;
	if (samples != 0)
	{
		baseOutput[pixel] = sums[0] / (float)(samples);
		coatOutput[pixel] = sums[1] / (float)(samples);
	}
	diagnostics[pixel] = float4((float)(receiver.Primitive), (float)(active), (float)(samples), 0);
	if (sums[0].w != 0 || sums[1].w != 0 || !VisibilityFinite(sums[0]) || !VisibilityFinite(sums[1]) ||
		(samples != 0 && (((active & 1u) != 0 && sums[0].y <= 0) ||
			((active & 2u) != 0 && sums[1].y <= 0))))
	{
		visibility[pixel] = float4(0, 0, 0, -2);
		diagnostics[pixel] = float4((float)(receiver.Primitive), (float)(active), (float)(samples), 1);
		return;
	}
	const float baseVisibility = sums[0].y > 0 ? sums[0].x / sums[0].y : 0;
	const float coatVisibility = sums[1].y > 0 ? sums[1].x / sums[1].y : 0;
	const float totalWeight = baseY + coatWeight;
	const float combined = totalWeight > 0 ?
		(baseY * baseVisibility + coatWeight * coatVisibility) / totalWeight : 0;
	visibility[pixel] = float4(baseVisibility, coatVisibility, combined,
		active == 0 ? 2 : samples != 0 ? 1 : 0);
}
