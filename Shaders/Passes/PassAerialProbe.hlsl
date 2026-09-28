#include <Common/BindlessResources.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/DepthReconstruction.hlsli>

StructuredBuffer<ViewData> g_Views : register(t3);

struct AerialProbeParameters
{
	uint SurfaceIndex;
	uint CompositeIndex;
	uint DepthIndex;
	uint RadianceIndex;
	uint ThroughputIndex;
	uint SamplerIndex;
	uint ViewIndex;
	uint GridWidth;
	uint GridHeight;
	uint SliceCount;
	uint Width;
	uint Height;
	float MaxDistanceKm;
	float WorldScaleKm;
	uint FrameSerialLow;
	uint FrameSerialHigh;
};
ConstantBuffer<AerialProbeParameters> g_Pass : register(b0);

struct AerialProbeSample
{
	float3 Surface;
	uint FrameSerialLow;
	float3 Composite;
	uint FrameSerialHigh;
	float4 Transmittance;
	float4 InScattering;
	float4 Metadata;
};
RWStructuredBuffer<AerialProbeSample> g_Output : register(u0);

float SliceCoordinate(float distanceKm)
{
	const float nearScaleKm = 0.02;
	return log(1.0 + min(distanceKm, g_Pass.MaxDistanceKm) / nearScaleKm) /
		log(1.0 + g_Pass.MaxDistanceKm / nearScaleKm) * float(g_Pass.SliceCount - 1);
}

float2 AtlasUV(float2 screenUV, uint slice)
{
	float2 grid = float2(g_Pass.GridWidth, g_Pass.GridHeight);
	float2 cell = clamp(saturate(screenUV) * grid, 0.5, grid - 0.5);
	return float2(cell.x / g_Pass.GridWidth,
		(float(slice * g_Pass.GridHeight) + cell.y) /
		float(g_Pass.GridHeight * g_Pass.SliceCount));
}

[numthreads(2, 1, 1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID)
{
	if (dispatchId.x >= 2) return;
	const uint2 pixel = dispatchId.x == 0
		? uint2(g_Pass.Width / 2, g_Pass.Height / 2)
		: uint2(g_Pass.Width / 2, g_Pass.Height / 8);
	const ViewData viewData = g_Views[g_Pass.ViewIndex];
	const float2 uv = PixelCenterToUV(pixel, uint2(g_Pass.Width, g_Pass.Height));
	const float rawDepth = GetTexture2DFloat(g_Pass.DepthIndex).Load(int3(pixel, 0));
	const bool background = IsDepthBackground(rawDepth, viewData.DepthConvention);
	const float preExposure = viewData.ScenePreExposure;
	AerialProbeSample sample;
	sample.Surface = GetTexture2DFloat4(g_Pass.SurfaceIndex).Load(int3(pixel, 0)).rgb /
		preExposure;
	sample.FrameSerialLow = g_Pass.FrameSerialLow;
	sample.Composite = GetTexture2DFloat4(g_Pass.CompositeIndex).Load(int3(pixel, 0)).rgb /
		preExposure;
	sample.FrameSerialHigh = g_Pass.FrameSerialHigh;
	sample.Transmittance = float4(1, 1, 1, 1);
	sample.InScattering = 0;
	float distanceKm = 0;
	if (!background)
	{
		const float3 viewPosition = ReconstructViewPosition(uv, rawDepth,
			viewData.InvProjMat);
		distanceKm = length(viewPosition) * g_Pass.WorldScaleKm;
		const float slicePosition = SliceCoordinate(distanceKm);
		const uint lowerSlice = uint(floor(slicePosition));
		const uint upperSlice = min(lowerSlice + 1, g_Pass.SliceCount - 1);
		SamplerState linearSampler = GetSamplerState(g_Pass.SamplerIndex);
		Texture2D<float4> radianceAtlas = GetTexture2DFloat4(g_Pass.RadianceIndex);
		Texture2D<float4> throughputAtlas = GetTexture2DFloat4(g_Pass.ThroughputIndex);
		const float3 lowerRadiance = radianceAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, lowerSlice), 0).rgb;
		const float3 upperRadiance = radianceAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, upperSlice), 0).rgb;
		const float3 lowerThroughput = throughputAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, lowerSlice), 0).rgb;
		const float3 upperThroughput = throughputAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, upperSlice), 0).rgb;
		sample.Transmittance = float4(lerp(lowerThroughput, upperThroughput,
			frac(slicePosition)), 1);
		sample.InScattering = float4(lerp(lowerRadiance, upperRadiance,
			frac(slicePosition)), 0);
	}
	sample.Metadata = float4(rawDepth, distanceKm, preExposure, background ? 1 : 0);
	g_Output[dispatchId.x] = sample;
}
