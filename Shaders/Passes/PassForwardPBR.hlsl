#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <Common/ForwardCoverageVaryings.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/SurfaceEvaluation.hlsli>
#include <Common/EnvironmentSampling.hlsli>
#include <Lighting/AmbientOcclusion.hlsli>
#include <Lighting/ForwardPlus.hlsli>
#include <Lighting/ShadowSampling.hlsli>
#include <Lighting/DirectionalShadowData.hlsli>
#include <PBR/BRDF.hlsli>

struct ForwardPBRPassParameters
{
	uint ViewIndex;
	uint ShadowMapTextureIndex;
	uint ShadowMapSamplerIndex;
	uint ShadowMapSize;
	uint ShadowFlags;
	float ShadowBiasPadding;
	uint ShadowPadding;
	uint ForwardPlusTileCountX;
	uint ForwardPlusTileCountY;
	uint ForwardPlusGlobalLightCount;
	uint2 ForwardPlusGlobalLightIndices01;
	uint2 ForwardPlusGlobalLightIndices23;
	uint GTAOTextureIndex;
	uint GTAOFlags;
};

ConstantBuffer<ForwardPBRPassParameters> g_Pass : register(b2);
ConstantBuffer<DirectionalShadowData> g_Shadow : register(b3);

#if defined(GGLAB_FORWARD_PLUS)
StructuredBuffer<uint2> g_ForwardPlusTileHeaders : register(t5);
StructuredBuffer<uint> g_ForwardPlusTileIndices : register(t6);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION) && defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
struct ForwardPBRPixelOutput
{
	float4 ForwardPlusColor : SV_Target0;
	float4 LegacyColor : SV_Target1;
	float4 GTAOContribution : SV_Target2;
};

ForwardPBRPixelOutput MakeForwardPBRPixelOutput(
	float4 forwardPlusColor, float4 legacyColor, float4 gtaoContribution)
{
	ForwardPBRPixelOutput output;
	output.ForwardPlusColor = forwardPlusColor;
	output.LegacyColor = legacyColor;
	output.GTAOContribution = gtaoContribution;
	return output;
}
#elif defined(GGLAB_FORWARD_PLUS_VALIDATION)
struct ForwardPBRPixelOutput
{
	float4 ForwardPlusColor : SV_Target0;
	float4 LegacyColor : SV_Target1;
};

ForwardPBRPixelOutput MakeForwardPBRPixelOutput(
	float4 forwardPlusColor, float4 legacyColor, float4 gtaoContribution)
{
	ForwardPBRPixelOutput output;
	output.ForwardPlusColor = forwardPlusColor;
	output.LegacyColor = legacyColor;
	return output;
}
#elif defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
struct ForwardPBRPixelOutput
{
	float4 Color : SV_Target0;
	float4 GTAOContribution : SV_Target1;
};

ForwardPBRPixelOutput MakeForwardPBRPixelOutput(
	float4 color, float4 legacyColor, float4 gtaoContribution)
{
	ForwardPBRPixelOutput output;
	output.Color = color;
	output.GTAOContribution = gtaoContribution;
	return output;
}
#else
#define ForwardPBRPixelOutput float4

float4 MakeForwardPBRPixelOutput(float4 color, float4 legacyColor, float4 gtaoContribution)
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
static const uint GTAOEnabledFlag = 1u;

bool IsShadowEnabled()
{
	return (g_Pass.ShadowFlags & 1u) != 0u;
}

bool IsShadowPCFEnabled()
{
	return (g_Pass.ShadowFlags & 2u) != 0u;
}

float LoadGTAO(uint2 pixel)
{
	if ((g_Pass.GTAOFlags & GTAOEnabledFlag) == 0u)
	{
		return 1.0;
	}
	Texture2D<float> gtaoTexture = GetTexture2DFloat(g_Pass.GTAOTextureIndex);
	return saturate(gtaoTexture.Load(int3(pixel, 0)));
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

float SampleDirectionalShadowCascade(float3 positionWS, ShadowReceiverPlane receiver,
	float receiverNoL, uint cascadeIndex)
{
	const uint viewIndex = g_Shadow.ViewBaseIndex + cascadeIndex;
	const ShadowProjection projection = ProjectToShadowMap(positionWS, viewIndex);
	if (!projection.IsValid)
	{
		return 1.0;
	}
	Texture2DArray<float> shadowMap = GetTexture2DArrayFloat(g_Pass.ShadowMapTextureIndex);
	SamplerComparisonState sampler = GetSamplerComparisonState(g_Pass.ShadowMapSamplerIndex);
	const float2 texelSize = 1.0.xx / max((float) g_Pass.ShadowMapSize, 1.0);
	const float2 gradient = ComputeShadowReceiverDepthGradient(receiver, viewIndex);
	const float residualBias = EvaluateDirectionalShadowReceiverBias(cascadeIndex, receiverNoL, g_Shadow);
	// Each hardware comparison blends four texels. Account for its one-texel support,
	// while PCF tap offsets below follow the actual receiver plane instead of adding bias.
	const float bilinearBias = ShadowBilinearReceiverBias(gradient.x, gradient.y, texelSize.x, texelSize.y);
	const float compareDepth = projection.ReceiverDepth - residualBias - bilinearBias;
	if (!IsShadowPCFEnabled())
	{
		return SampleShadowHard(shadowMap, sampler, projection.UV, cascadeIndex, saturate(compareDepth));
	}
	return SampleShadowPCF3x3(shadowMap, sampler, projection.UV, cascadeIndex,
		compareDepth, texelSize, gradient);
}

float SampleDirectionalShadow(float3 positionWS, ShadowReceiverPlane receiver, float receiverNoL)
{
	if (!IsShadowEnabled())
	{
		return 1.0;
	}
	const float mainViewZ = TransformPositionVS(float4(positionWS, 1.0),
		LoadViewData(g_Shadow.MainViewIndex)).z;
	const uint cascadeIndex = SelectDirectionalShadowCascade(mainViewZ, g_Shadow);
	if (cascadeIndex >= g_Shadow.CascadeCount)
	{
		return 1.0;
	}
	float visibility = SampleDirectionalShadowCascade(positionWS, receiver, receiverNoL, cascadeIndex);
	const float blendStart = g_Shadow.BlendStart[cascadeIndex];
	const float blendWidth = g_Shadow.SplitFar[cascadeIndex] - blendStart;
	if (cascadeIndex + 1u < g_Shadow.CascadeCount && blendWidth > 0.0 && mainViewZ > blendStart)
	{
		const float next = SampleDirectionalShadowCascade(positionWS, receiver, receiverNoL, cascadeIndex + 1u);
		visibility = lerp(visibility, next, smoothstep(blendStart, g_Shadow.SplitFar[cascadeIndex], mainViewZ));
	}
	const float fade = saturate((mainViewZ - g_Shadow.DistanceFadeStart) * g_Shadow.DistanceFadeInvRange);
	return lerp(visibility, 1.0, fade * fade * (3.0 - 2.0 * fade));
}

float3 ApplyShadowDiagnosticsOverlay(float3 color, float3 positionWS)
{
	if (!IsShadowEnabled() || (g_Pass.ShadowFlags & 12u) == 0u)
	{
		return color;
	}
	const float depth = TransformPositionVS(float4(positionWS, 1.0),
		LoadViewData(g_Shadow.MainViewIndex)).z;
	const uint cascade = SelectDirectionalShadowCascade(depth, g_Shadow);
	if (cascade >= g_Shadow.CascadeCount)
	{
		return color;
	}
	const float3 cascadeColors[4] = {
		float3(0.15, 0.65, 1.0), float3(0.35, 1.0, 0.25),
		float3(1.0, 0.45, 0.15), float3(0.8, 0.3, 1.0)
	};
	if ((g_Pass.ShadowFlags & 4u) != 0u)
	{
		color = lerp(color, cascadeColors[min(cascade, 3u)], 0.3);
	}
	if ((g_Pass.ShadowFlags & 8u) != 0u)
	{
		const float transitionStart = g_Shadow.BlendStart[cascade];
		if (cascade + 1u < g_Shadow.CascadeCount && depth >= transitionStart &&
			depth <= g_Shadow.SplitFar[cascade])
		{
			color = lerp(color, float3(1.0, 0.9, 0.1), 0.55);
		}
		if (g_Shadow.DistanceFadeInvRange > 0.0 && depth >= g_Shadow.DistanceFadeStart)
		{
			color = lerp(color, float3(1.0, 0.35, 0.65), 0.45);
		}
	}
	return color;
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

float3 EvaluateDirectLight(uint lightIndex, float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver, float3 V, float NoV,
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
		shadowVisibility = SampleDirectionalShadow(positionWS, shadowReceiver, dot(shadowReceiver.NormalWS, L));
	}

	return (diffuse + specular) * light.Color.rgb * light.Intensity * NoL * attenuation *
		shadowVisibility;
}

float3 EvaluateLegacyDirectLighting(float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver, float3 V, float NoV,
	float3 F0, float physicalRoughness, float3 baseColor, float metallic)
{
	float3 lighting = 0.0.xxx;
	for (uint lightOffset = 0; lightOffset < g_Scene.LightCount; ++lightOffset)
	{
		const uint lightIndex = g_Scene.LightBaseIndex + lightOffset;
		lighting += EvaluateDirectLight(lightIndex, positionWS, N, shadowReceiver, V, NoV, F0,
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

float3 EvaluateForwardPlusDirectLighting(float2 pixelPosition, float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver,
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
			lighting += EvaluateDirectLight(lightIndex, positionWS, N, shadowReceiver, V, NoV, F0,
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
		lighting += EvaluateDirectLight(lightIndex, positionWS, N, shadowReceiver, V, NoV, F0,
			physicalRoughness, baseColor, metallic);
	}
	return lighting;
}
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION) || defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
ForwardPBRPixelOutput PSMain(ForwardCoverageVSOutput IN, bool isFrontFace : SV_IsFrontFace)
#else
float4 PSMain(ForwardCoverageVSOutput IN, bool isFrontFace : SV_IsFrontFace) : SV_Target
#endif
{
	const ShadowReceiverPlane shadowReceiver = BuildShadowReceiverPlane(IN.PositionWS);
	MaterialData matData = g_Materials[IN.MaterialIndex];

	// Get view data
	ViewData viewData = g_Views[GetViewDataIndex(g_Pass.ViewIndex)];

	// Surface evaluation: the hand-authored
	// surface functions resolved from the runtime-driven MaterialData (factors
	// plus texture+sampler bindings) feed the existing Forward PBR lighting
	// below.
	const SurfaceData surface = EvaluateSurface(matData, IN.UV0, IN.UV1);
	const float3 baseColor = surface.BaseColor;
	// Alpha mode / cutoff / discard stays pass-owned: resolve the surface's
	// raw sampled alpha through the material's alpha policy here.
	const float alpha = ResolveMaterialAlpha(matData, surface.Opacity);

	// Metallic and Roughness (linear, resolved by the surface seam;
	// B=metallic, G=roughness)
	const float metallic = surface.Metallic;
	float perceptualRoughness = ClampPerceptualRoughnessForBRDF(surface.Roughness);

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
		return MakeForwardPBRPixelOutput(
			float4(baseColor, alpha), float4(baseColor, alpha), 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewMetallic)
	{
		return MakeForwardPBRPixelOutput(float4(metallic.xxx, alpha),
			float4(metallic.xxx, alpha), 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewRoughness)
	{
		return MakeForwardPBRPixelOutput(float4(perceptualRoughness.xxx, alpha),
			float4(perceptualRoughness.xxx, alpha), 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewNormal)
	{
		const float4 normalColor = float4(N * 0.5 + 0.5, alpha);
		return MakeForwardPBRPixelOutput(normalColor, normalColor, 0.0.xxxx);
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
		IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor, metallic);
#else
	const float3 directLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor, metallic);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	const float3 legacyDirectLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor, metallic);
#endif

	// Emissive (resolved by the surface seam from the emissive texture)
	const float3 emissive = surface.Emissive;

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
	const float gtao = LoadGTAO(uint2(IN.PositionCS.xy));
	const float3 materialOccludedDiffuseIBL =
		diffuseIBL * ResolveSpecularIBLVisibility(ao);
	const float3 gtaoContribution = materialOccludedDiffuseIBL * (1.0 - gtao);
	diffuseIBL *= ResolveDiffuseIBLVisibility(ao, gtao);
	specularIBL *= ResolveSpecularIBLVisibility(ao);

	float3 outputLighting = directLighting;
	outputLighting += emissive;
	outputLighting += diffuseIBL + specularIBL;
	const float4 outputColor = float4(SanitizeHDRColor(
		ApplyShadowDiagnosticsOverlay(outputLighting, IN.PositionWS)), alpha);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float3 legacyOutputLighting = legacyDirectLighting;
	legacyOutputLighting += emissive;
	legacyOutputLighting += diffuseIBL + specularIBL;
	const float4 legacyColor = float4(SanitizeHDRColor(
		ApplyShadowDiagnosticsOverlay(legacyOutputLighting, IN.PositionWS)), alpha);
	return MakeForwardPBRPixelOutput(outputColor, legacyColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0));
#else
	return MakeForwardPBRPixelOutput(outputColor, outputColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0));
#endif
}
