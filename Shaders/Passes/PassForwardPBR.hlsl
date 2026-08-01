#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/EnvironmentSampling.hlsli>
#include <Lighting/ForwardPlus.hlsli>
#include <Lighting/ShadowSampling.hlsli>
#include <PBR/BRDF.hlsli>

struct ForwardPBRPassParameters
{
	uint ViewIndex;
	uint ShadowMapTextureIndex;
	uint ShadowMapSamplerIndex;
	uint ShadowMapSize;
	uint ShadowFlags;
	float ShadowReceiverDepthBias;
	uint ShadowViewIndex;
	uint ForwardPlusTileCountX;
	uint ForwardPlusTileCountY;
	uint ForwardPlusGlobalLightCount;
	uint2 ForwardPlusGlobalLightIndices01;
	uint2 ForwardPlusGlobalLightIndices23;
	uint2 Padding;
};

ConstantBuffer<ForwardPBRPassParameters> g_Pass : register(b2);

#if defined(GGLAB_FORWARD_PLUS)
StructuredBuffer<uint2> g_ForwardPlusTileHeaders : register(t5);
StructuredBuffer<uint> g_ForwardPlusTileIndices : register(t6);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
struct ForwardPBRPixelOutput
{
	float4 ForwardPlusColor : SV_Target0;
	float4 LegacyColor : SV_Target1;
};

ForwardPBRPixelOutput MakeForwardPBRPixelOutput(float4 forwardPlusColor, float4 legacyColor)
{
	ForwardPBRPixelOutput output;
	output.ForwardPlusColor = forwardPlusColor;
	output.LegacyColor = legacyColor;
	return output;
}
#else
#define ForwardPBRPixelOutput float4

float4 MakeForwardPBRPixelOutput(float4 color, float4 legacyColor)
{
	return color;
}
#endif

// Keep these values synchronized with MaterialDebugView in GraphicsTypes.h.
static const uint MaterialDebugViewLit = 0u;
static const uint MaterialDebugViewBaseColor = 1u;
static const uint MaterialDebugViewMetallic = 2u;
static const uint MaterialDebugViewRoughness = 3u;
static const uint MaterialDebugViewNormal = 4u;

bool IsShadowEnabled()
{
	return (g_Pass.ShadowFlags & 1u) != 0u;
}

bool IsShadowPCFEnabled()
{
	return (g_Pass.ShadowFlags & 2u) != 0u;
}

// Sample normal map and compute perturbed normal in world space
float3 SampleNormalWS(
	MaterialData matData, float3 normalWS, float4 tangentWS, float3 positionWS, float2 uv)
{
	// TODO: flip Y for normal map?

	// Sample normal texture
	float4 normalSampled = SampleTextureBinding(matData.NormalBinding.TextureSamplerBinding, uv);

	// Remap from [0,1] to [-1,1], xy only
	normalSampled.xy = normalSampled.xy * 2.0 - 1.0;

	// Apply normal scale xy
	normalSampled.xy *= matData.NormalScale; // apply normal scale

	// rebuild z, avoid normalization issues
	normalSampled.z = sqrt(saturate(1.0 - dot(normalSampled.xy, normalSampled.xy)));

	// Build TBN matrix
	float3x3 TBN = BuildTBNFromTangent(
		SafeNormalize(normalWS, float3(0.0, 1.0, 0.0)), tangentWS, positionWS, uv);

	// Transform normal from tangent space to world space
	float3 perturbedNormalWS = SafeNormalize(mul(normalSampled.xyz, TBN), TBN[2]);
	return perturbedNormalWS;
}

float FilterPerceptualRoughness(float perceptualRoughness, float3 normalWS)
{
	// Normal-map frequencies above the pixel footprint otherwise turn a small,
	// intense environment light into unstable sub-pixel specular highlights.
	float3 normalDx = ddx(normalWS);
	float3 normalDy = ddy(normalWS);
	float normalVariance = dot(normalDx, normalDx) + dot(normalDy, normalDy);
	float kernelAlpha = min(2.0 * normalVariance, 0.18);
	float alpha = PerceptualRoughnessToAlpha(perceptualRoughness);
	return sqrt(saturate(alpha + kernelAlpha));
}

float2 SampleIBLBrdfLUT(float NoV, float perceptualRoughness)
{
	float4 value =
		SampleTextureBindingLevel(MakeTextureSamplerBinding(g_Scene.IBLResource.BrdfLutBinding),
			float2(saturate(NoV), saturate(perceptualRoughness)), 0);

	return value.rg;
}

float3 SampleIBLIrradiance(float3 normalWS)
{
	TextureSamplerBindingData binding =
		MakeTextureSamplerBinding(g_Scene.IBLResource.IrradianceBinding);
	float3 direction = WorldToEnvironmentDirection(SafeNormalize(normalWS, float3(0.0, 1.0, 0.0)),
		g_Scene.IBLResource.EnvironmentRotationRadians);
	return SampleTextureCube(binding, direction).rgb * g_Scene.IBLResource.EnvironmentIntensity;
}

float3 SampleIBLPrefilteredSpecular(float3 reflectWS, float perceptualRoughness)
{
	TextureSamplerBindingData binding =
		MakeTextureSamplerBinding(g_Scene.IBLResource.PrefilteredSpecularBinding);
	const uint mipLevels = max(g_Scene.IBLResource.PrefilteredSpecularMipLevels, 1u);
	const float maxMipLevel = (float) (mipLevels - 1u);
	const float lod = saturate(perceptualRoughness) * maxMipLevel;
	float3 direction = WorldToEnvironmentDirection(SafeNormalize(reflectWS, float3(0.0, 1.0, 0.0)),
		g_Scene.IBLResource.EnvironmentRotationRadians);
	return SampleTextureCubeLevel(binding, direction, lod).rgb *
		   g_Scene.IBLResource.EnvironmentIntensity;
}

float SampleDirectionalShadow(float3 positionWS, float NoL)
{
	if (!IsShadowEnabled() || NoL <= 0.0)
	{
		return 1.0;
	}

	const ShadowProjection shadowProjection =
		ProjectToShadowMap(positionWS, g_Pass.ShadowViewIndex);
	if (!shadowProjection.IsValid)
	{
		return 1.0;
	}

	Texture2D<float> shadowMap = GetTexture2DFloat(g_Pass.ShadowMapTextureIndex);
	SamplerComparisonState shadowSampler = GetSamplerComparisonState(g_Pass.ShadowMapSamplerIndex);

	const float compareDepth =
		saturate(shadowProjection.ReceiverDepth - g_Pass.ShadowReceiverDepthBias);

	if (!IsShadowPCFEnabled())
	{
		return SampleShadowHard(shadowMap, shadowSampler, shadowProjection.UV, compareDepth);
	}

	const float shadowMapSize = max((float) g_Pass.ShadowMapSize, 1.0);
	const float2 shadowTexelSize = 1.0.xx / shadowMapSize;
	return SampleShadowPCF3x3(
		shadowMap, shadowSampler, shadowProjection.UV, compareDepth, shadowTexelSize);
}

bool ResolveLightVector(LightData light, float3 positionWS, out float3 L, out float attenuation)
{
	static const uint LightTypeDirectional = 0u;
	static const uint LightTypeSpot = 1u;
	static const uint LightTypePoint = 2u;

	attenuation = 1.0;
	if (light.Intensity <= 0.0)
	{
		L = 0.0.xxx;
		return false;
	}

	if (light.LightType == LightTypeDirectional)
	{
		L = normalize(-light.Direction.xyz);
		return true;
	}

	const float3 toLight = light.Position.xyz - positionWS;
	const float distanceToLight = length(toLight);
	const float range = max(light.Range, 0.0001);
	if (distanceToLight <= 0.0001 || distanceToLight >= range)
	{
		L = 0.0.xxx;
		return false;
	}

	L = toLight / distanceToLight;
	const float normalizedDistance = saturate(distanceToLight / range);
	attenuation = saturate(1.0 - normalizedDistance * normalizedDistance);
	attenuation *= attenuation;

	if (light.LightType == LightTypeSpot)
	{
		const float3 spotDirection = normalize(light.Direction.xyz);
		const float cosTheta = dot(normalize(-L), spotDirection);
		const float outerCos = cos(radians(max(light.SpotAngle, 0.001) * 0.5));
		const float innerCos = cos(radians(max(light.SpotAngle * 0.8, 0.001) * 0.5));
		const float spotAttenuation =
			saturate((cosTheta - outerCos) / max(innerCos - outerCos, 0.001));
		attenuation *= spotAttenuation * spotAttenuation;
	}
	else if (light.LightType != LightTypePoint)
	{
		return false;
	}

	return attenuation > 0.0;
}

float3 EvaluateDirectLight(uint lightIndex, float3 positionWS, float3 N, float3 V, float NoV,
	float3 F0, float physicalRoughness, float3 baseColor, float metallic)
{
	const LightData light = g_Lights[lightIndex];
	float3 L = 0.0.xxx;
	float attenuation = 1.0;
	if (!ResolveLightVector(light, positionWS, L, attenuation))
	{
		return 0.0.xxx;
	}

	const float NoL = saturate(dot(N, L));
	if (NoL <= 0.0)
	{
		return 0.0.xxx;
	}

	const float3 H = SafeNormalize(L + V, N);
	const float NoH = saturate(dot(N, H));
	const float VoH = saturate(dot(V, H));
	const float D = D_GGX(NoH, physicalRoughness);
	const float visibility = V_SmithGGXCorrelated(NoV, NoL, physicalRoughness);
	const float3 F = F_Schlick(F0, 1.0.xxx, VoH);
	const float3 specular = D * visibility * F;
	const float3 kd = (1.0.xxx - F) * (1.0 - metallic);
	const float3 diffuse = kd * Fd_Lambert(baseColor);

	float shadowVisibility = 1.0;
	if (light.LightType == 0u && lightIndex == g_Scene.DirectionalShadowLightIndex)
	{
		shadowVisibility = SampleDirectionalShadow(positionWS, NoL);
	}

	return (diffuse + specular) * light.Color.rgb * light.Intensity * NoL * attenuation *
		shadowVisibility;
}

float3 EvaluateLegacyDirectLighting(float3 positionWS, float3 N, float3 V, float NoV,
	float3 F0, float physicalRoughness, float3 baseColor, float metallic)
{
	float3 lighting = 0.0.xxx;
	for (uint lightOffset = 0; lightOffset < g_Scene.LightCount; ++lightOffset)
	{
		const uint lightIndex = g_Scene.LightBaseIndex + lightOffset;
		lighting += EvaluateDirectLight(lightIndex, positionWS, N, V, NoV, F0,
			physicalRoughness, baseColor, metallic);
	}
	return lighting;
}

#if defined(GGLAB_FORWARD_PLUS)
uint GetForwardPlusGlobalLightIndex(uint listIndex)
{
	return listIndex < 2u ? g_Pass.ForwardPlusGlobalLightIndices01[listIndex]
		: g_Pass.ForwardPlusGlobalLightIndices23[listIndex - 2u];
}

float3 EvaluateForwardPlusDirectLighting(float2 pixelPosition, float3 positionWS, float3 N,
	float3 V, float NoV, float3 F0, float physicalRoughness, float3 baseColor, float metallic)
{
	float3 lighting = 0.0.xxx;
	const uint globalLightCount = min(
		g_Pass.ForwardPlusGlobalLightCount, FORWARD_PLUS_GLOBAL_LIGHT_CAPACITY);
	for (uint listOffset = 0; listOffset < globalLightCount; ++listOffset)
	{
		const uint lightIndex = GetForwardPlusGlobalLightIndex(listOffset);
		if (lightIndex >= g_Scene.LightBaseIndex &&
			lightIndex < g_Scene.LightBaseIndex + g_Scene.LightCount)
		{
			lighting += EvaluateDirectLight(lightIndex, positionWS, N, V, NoV, F0,
				physicalRoughness, baseColor, metallic);
		}
	}

	const uint2 tileCount = uint2(g_Pass.ForwardPlusTileCountX, g_Pass.ForwardPlusTileCountY);
	if (any(tileCount == 0u))
	{
		return lighting;
	}
	const uint tileIndex = GetForwardPlusTileIndex(uint2(pixelPosition), tileCount);
	const uint2 header = g_ForwardPlusTileHeaders[tileIndex];
	const uint localLightCount = GetForwardPlusTileLightCount(header.y);
	const uint lightEnd = g_Scene.LightBaseIndex + g_Scene.LightCount;
	for (uint listOffset = 0; listOffset < localLightCount; ++listOffset)
	{
		const uint lightIndex = g_ForwardPlusTileIndices[header.x + listOffset];
		if (lightIndex < g_Scene.LightBaseIndex || lightIndex >= lightEnd ||
			g_Lights[lightIndex].LightType == 0u)
		{
			continue;
		}
		lighting += EvaluateDirectLight(lightIndex, positionWS, N, V, NoV, F0,
			physicalRoughness, baseColor, metallic);
	}
	return lighting;
}
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
ForwardPBRPixelOutput PSMain(ForwardCoverageVSOutput IN, bool isFrontFace : SV_IsFrontFace)
#else
float4 PSMain(ForwardCoverageVSOutput IN, bool isFrontFace : SV_IsFrontFace) : SV_Target
#endif
{
	MaterialData matData = g_Materials[IN.MaterialIndex];

	// Get view data
	ViewData viewData = g_Views[IN.ViewIndex];

	// BaseColor
	const float4 baseColorSampled = SampleMaterialBaseColor(matData, IN.UV0, IN.UV1);
	const float3 baseColor = baseColorSampled.rgb;

	float alpha = ResolveMaterialAlpha(matData, baseColorSampled.a);

	// Mataliic and Roughness (linear, B=metallic, G=roughness)
	float2 metallicRoughnessUV = SelectUV(matData.MetallicRoughnessBinding, IN.UV0, IN.UV1);
	float4 mrSampled = SampleTextureBinding(
		matData.MetallicRoughnessBinding.TextureSamplerBinding, metallicRoughnessUV);
	float metallic = saturate(matData.MetallicFactor * mrSampled.b);
	float perceptualRoughness = saturate(matData.RoughnessFactor * mrSampled.g);
	perceptualRoughness = ClampPerceptualRoughnessForBRDF(perceptualRoughness);

	// Normal (linear)
	float3 normalWS = SafeNormalize(IN.NormalWS, float3(0.0, 1.0, 0.0));
	float4 tangentWS = IN.TangentWS;
	if ((matData.Flags & 1u) != 0u && !isFrontFace)
	{
		normalWS = -normalWS;
	}
	float2 normalUV = SelectUV(matData.NormalBinding, IN.UV0, IN.UV1);
	float3 N = SampleNormalWS(matData, normalWS, tangentWS, IN.PositionWS, normalUV);

	if (matData.DebugView == MaterialDebugViewBaseColor)
	{
		return MakeForwardPBRPixelOutput(float4(baseColor, alpha), float4(baseColor, alpha));
	}
	if (matData.DebugView == MaterialDebugViewMetallic)
	{
		return MakeForwardPBRPixelOutput(float4(metallic.xxx, alpha),
			float4(metallic.xxx, alpha));
	}
	if (matData.DebugView == MaterialDebugViewRoughness)
	{
		return MakeForwardPBRPixelOutput(float4(perceptualRoughness.xxx, alpha),
			float4(perceptualRoughness.xxx, alpha));
	}
	if (matData.DebugView == MaterialDebugViewNormal)
	{
		const float4 normalColor = float4(N * 0.5 + 0.5, alpha);
		return MakeForwardPBRPixelOutput(normalColor, normalColor);
	}
	perceptualRoughness = FilterPerceptualRoughness(perceptualRoughness, N);

	// Shading
	float3 V = SafeNormalize(viewData.CameraPos.xyz - IN.PositionWS, N); // View direction
	float NoV = saturate(dot(N, V));

	// convert artistic roughness to physical roughness
	float a = PerceptualRoughnessToAlpha(perceptualRoughness);

	float3 F0 = lerp(0.04.xxx, baseColor, metallic); // dielectric F0 is 0.04, metal F0 is baseColor
#if defined(GGLAB_FORWARD_PLUS)
	const float3 directLighting = EvaluateForwardPlusDirectLighting(IN.PositionCS.xy,
		IN.PositionWS, N, V, NoV, F0, a, baseColor, metallic);
#else
	const float3 directLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, V, NoV, F0, a, baseColor, metallic);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	const float3 legacyDirectLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, V, NoV, F0, a, baseColor, metallic);
#endif

	// Emissive texture(sRGB)
	float2 emissiveUV = SelectUV(matData.EmissiveBinding, IN.UV0, IN.UV1);
	float3 emissiveSampled =
		SampleTextureBinding(matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb;
	float3 emissive = emissiveSampled * matData.EmissiveColorFactor.rgb;

	// IBL
	float3 iblF = F_Schlick(F0, max((1.0 - perceptualRoughness).xxx, F0), NoV);

	float3 diffuseIBLFactor = (1.0.xxx - iblF) * (1.0 - metallic);
	float3 diffuseIBL = SampleIBLIrradiance(N) * diffuseIBLFactor * Fd_Lambert(baseColor);

	float2 brdfLUT = SampleIBLBrdfLUT(NoV, perceptualRoughness);
	float3 specularIBLFactor = F0 * brdfLUT.x + brdfLUT.y;

	float3 reflectWS = reflect(-V, N);
	float3 prefilteredEnv = SampleIBLPrefilteredSpecular(reflectWS, perceptualRoughness);
	float3 specularIBL = prefilteredEnv * specularIBLFactor;

	// AO texture
	float2 occlusionUV = SelectUV(matData.OcclusionBinding, IN.UV0, IN.UV1);
	float aoSampled =
		SampleTextureBinding(matData.OcclusionBinding.TextureSamplerBinding, occlusionUV).r;
	float ao = 1.0f + matData.OcclusionStrength * (aoSampled - 1.0f);
	ao = saturate(ao);

	float3 outputLighting = directLighting;
	outputLighting += emissive;
	outputLighting += (diffuseIBL + specularIBL) * ao;
	const float4 outputColor = float4(SanitizeHDRColor(outputLighting), alpha);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float3 legacyOutputLighting = legacyDirectLighting;
	legacyOutputLighting += emissive;
	legacyOutputLighting += (diffuseIBL + specularIBL) * ao;
	const float4 legacyColor = float4(SanitizeHDRColor(legacyOutputLighting), alpha);
	return MakeForwardPBRPixelOutput(outputColor, legacyColor);
#else
	return MakeForwardPBRPixelOutput(outputColor, outputColor);
#endif
}
