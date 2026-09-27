#include <Common/Common.hlsli>
#include <Common/BindlessResources.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/DepthReconstruction.hlsli>
#include <Lighting/Atmosphere.hlsli>

StructuredBuffer<ViewData> g_Views : register(t3);

// The two atlases contain front-to-back radiance and RGB throughput at each
// frustum slice. Atlas rows are grouped by slice; no 3D view is required.
struct AerialPerspectiveParameters
{
	uint Source0;
	uint Source1;
	uint Source2;
	uint Source3;
	uint Output0;
	uint Output1;
	uint SamplerIndex;
	uint ViewIndex;
	uint GridWidth;
	uint GridHeight;
	uint SliceCount;
	float MaxDistanceKm;
#if defined(AERIAL_BUILD)
	float3 SunDirection;
#else
	uint Width;
	uint Height;
	uint DiagnosticMode;
#endif
	uint Padding;
};
ConstantBuffer<AerialPerspectiveParameters> g_Pass : register(b2);

static const float AerialNearScaleKm = 0.02;

float SliceDistanceKm(float slice)
{
	float normalized = saturate(slice / float(g_Pass.SliceCount - 1));
	return AerialNearScaleKm * (pow(1.0 + g_Pass.MaxDistanceKm / AerialNearScaleKm,
		normalized) - 1.0);
}

float SliceCoordinate(float distanceKm)
{
	return log(1.0 + min(distanceKm, g_Pass.MaxDistanceKm) / AerialNearScaleKm) /
		log(1.0 + g_Pass.MaxDistanceKm / AerialNearScaleKm) * float(g_Pass.SliceCount - 1);
}

float3 WorldDirection(float2 uv, ViewData viewData)
{
	float4 farPositionVS = mul(float4(UVToNDC(uv),
		GetDepthFarValue(viewData.DepthConvention), 1.0), viewData.InvProjMat);
	float3 directionVS = SafeNormalize(farPositionVS.xyz /
		max(abs(farPositionVS.w), 1.0e-6), float3(0.0, 0.0, 1.0));
	return SafeNormalize(mul(float4(directionVS, 0.0), viewData.InvViewMat).xyz,
		float3(0.0, 0.0, 1.0));
}

float2 AtlasUV(float2 screenUV, uint slice)
{
	float2 cell = saturate(screenUV) * float2(g_Pass.GridWidth - 1, g_Pass.GridHeight - 1);
	return float2((cell.x + 0.5) / g_Pass.GridWidth,
		(float(slice * g_Pass.GridHeight) + cell.y + 0.5) /
		float(g_Pass.GridHeight * g_Pass.SliceCount));
}

#if defined(AERIAL_BUILD)
[numthreads(8, 8, 1)]
void CSBuild(uint3 dispatchId : SV_DispatchThreadID)
{
	if (dispatchId.x >= g_Pass.GridWidth || dispatchId.y >= g_Pass.GridHeight) return;
	const ViewData viewData = g_Views[g_Pass.ViewIndex];
	const float2 uv = PixelCenterToUV(dispatchId.xy,
		uint2(g_Pass.GridWidth, g_Pass.GridHeight));
	const float3 direction = WorldDirection(uv, viewData);
	const float3 camera = viewData.CameraPos.xyz * g_Atmosphere.World.w - g_Atmosphere.World.xyz;
	Texture2D<float4> transmittanceLut = GetTexture2DFloat4(g_Pass.Source0);
	Texture2D<float4> multipleLut = GetTexture2DFloat4(g_Pass.Source1);
	RWTexture2D<float4> radianceAtlas = GetRWTexture2DFloat4(g_Pass.Output0);
	RWTexture2D<float4> throughputAtlas = GetRWTexture2DFloat4(g_Pass.Output1);
	SamplerState linearSampler = GetSamplerState(g_Pass.SamplerIndex);
	const float3 sunDirection = SafeNormalize(g_Pass.SunDirection, float3(0, 1, 0));
	const float cosine = dot(direction, sunDirection);
	const float rayPhase = 3.0 * (1.0 + cosine * cosine) / (16.0 * AtmospherePi);
	const float anisotropy = g_Atmosphere.Mie.w;
	const float miePhase = (1.0 - anisotropy * anisotropy) /
		(4.0 * AtmospherePi * pow(max(0.001,
			1.0 + anisotropy * anisotropy - 2.0 * anisotropy * cosine), 1.5));
	float3 throughput = 1.0;
	float3 radiance = 0.0;
	float previousDistanceKm = 0.0;
	const float groundDistanceKm = AtmosphereGroundDistance(camera, direction);
	const float topDistanceKm = AtmosphereTopDistance(camera, direction);
	const float atmosphereEndKm = groundDistanceKm > 0.0
		? min(groundDistanceKm, topDistanceKm) : topDistanceKm;
	for (uint slice = 0; slice < g_Pass.SliceCount; ++slice)
	{
		const float distanceKm = min(SliceDistanceKm(float(slice)), atmosphereEndKm);
		const float intervalKm = distanceKm - previousDistanceKm;
		const uint steps = max(1u, min(48u, uint(ceil(intervalKm / 2.0))));
		const float stepKm = intervalKm / float(steps);
		for (uint step = 0; step < steps; ++step)
		{
			const float3 position = camera + direction *
				(previousDistanceKm + (float(step) + 0.5) * stepKm);
			const float radius = length(position);
			if (radius <= g_Atmosphere.Radii.x || radius >= g_Atmosphere.Radii.y) continue;
			float3 rayleigh, extinction;
			float mie;
			AtmosphereMedium(position, rayleigh, mie, extinction);
			const float3 multi = multipleLut.SampleLevel(linearSampler,
				AtmosphereLutUV(radius, dot(position, sunDirection) / radius,
					float2(32, 32)), 0).rgb;
			const float3 source = (rayleigh * rayPhase + mie * miePhase) *
				AtmosphereSunTransmittance(transmittanceLut, linearSampler,
					position, sunDirection) + (rayleigh + mie) * multi;
			radiance += throughput * source * AtmosphereSegmentIntegral(extinction,
				stepKm) * g_Atmosphere.Sun.rgb;
			throughput *= exp(-extinction * stepKm);
		}
		const uint2 atlasPixel = uint2(dispatchId.x,
			dispatchId.y + slice * g_Pass.GridHeight);
		radianceAtlas[atlasPixel] = AtmosphereValidated(radiance);
		throughputAtlas[atlasPixel] = all(isfinite(throughput))
			? float4(saturate(throughput), 1.0) : float4(1.0, 0.0, 1.0, 0.0);
		previousDistanceKm = distanceKm;
	}
}
#else
[numthreads(8, 8, 1)]
void CSComposite(uint3 dispatchId : SV_DispatchThreadID)
{
	if (dispatchId.x >= g_Pass.Width || dispatchId.y >= g_Pass.Height) return;
	const ViewData viewData = g_Views[g_Pass.ViewIndex];
	const float2 uv = PixelCenterToUV(dispatchId.xy, uint2(g_Pass.Width, g_Pass.Height));
	Texture2D<float4> sceneColor = GetTexture2DFloat4(g_Pass.Source0);
	Texture2D<float> depth = GetTexture2DFloat(g_Pass.Source1);
	Texture2D<float4> radianceAtlas = GetTexture2DFloat4(g_Pass.Source2);
	Texture2D<float4> throughputAtlas = GetTexture2DFloat4(g_Pass.Source3);
	RWTexture2D<float4> output = GetRWTexture2DFloat4(g_Pass.Output0);
	const float4 storedColor = sceneColor.Load(int3(dispatchId.xy, 0));
	const float rawDepth = depth.Load(int3(dispatchId.xy, 0));
	float3 throughput = 1.0;
	float3 radiance = 0.0;
	if (!IsDepthBackground(rawDepth, viewData.DepthConvention))
	{
		const float3 viewPosition = ReconstructViewPosition(uv, rawDepth,
			viewData.InvProjMat);
		const float distanceKm = length(viewPosition) * g_Atmosphere.World.w;
		const float slicePosition = SliceCoordinate(distanceKm);
		const uint lowerSlice = uint(floor(slicePosition));
		const uint upperSlice = min(lowerSlice + 1, g_Pass.SliceCount - 1);
		SamplerState linearSampler = GetSamplerState(g_Pass.SamplerIndex);
		const float4 lowerRadiance = radianceAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, lowerSlice), 0);
		const float4 upperRadiance = radianceAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, upperSlice), 0);
		const float4 lowerThroughput = throughputAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, lowerSlice), 0);
		const float4 upperThroughput = throughputAtlas.SampleLevel(linearSampler,
			AtlasUV(uv, upperSlice), 0);
		if (min(min(lowerRadiance.a, upperRadiance.a),
			min(lowerThroughput.a, upperThroughput.a)) < 0.999)
		{
			output[dispatchId.xy] = float4(1.0, 0.0, 1.0, 1.0);
			if (g_Pass.DiagnosticMode != 0)
			{
				GetRWTexture2DFloat4(g_Pass.Output1)[dispatchId.xy] = float4(1.0, 0.0, 1.0, 1.0);
			}
			return;
		}
		radiance = lerp(lowerRadiance.rgb, upperRadiance.rgb, frac(slicePosition));
		throughput = lerp(lowerThroughput.rgb, upperThroughput.rgb, frac(slicePosition));
	}
	output[dispatchId.xy] = float4(storedColor.rgb * throughput +
		EncodeSceneColor(radiance, viewData.ScenePreExposure), storedColor.a);
	if (g_Pass.DiagnosticMode != 0)
	{
		RWTexture2D<float4> diagnostic = GetRWTexture2DFloat4(g_Pass.Output1);
		diagnostic[dispatchId.xy] = float4(g_Pass.DiagnosticMode == 1
			? throughput : EncodeSceneColor(radiance, viewData.ScenePreExposure), 1.0);
	}
}
#endif
