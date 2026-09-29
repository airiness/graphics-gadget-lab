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
#include <PBR/SpecularAA.hlsli>

struct ForwardPBRPassParameters
{
	uint ViewIndex;
	uint ShadowMapTextureIndex;
	uint ShadowMapSamplerIndex;
	uint ShadowMapSize;
	uint ShadowFlags;
	uint AtmosphereTransmittanceIndex;
	uint AtmosphereSamplerIndex;
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

float4 EncodeForwardSceneColor(float4 color)
{
	const float preExposure = LoadViewData(g_Pass.ViewIndex).ScenePreExposure;
	return float4(EncodeSceneColor(color.rgb, preExposure), color.a);
}

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
	output.ForwardPlusColor = EncodeForwardSceneColor(forwardPlusColor);
	output.LegacyColor = EncodeForwardSceneColor(legacyColor);
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
	output.ForwardPlusColor = EncodeForwardSceneColor(forwardPlusColor);
	output.LegacyColor = EncodeForwardSceneColor(legacyColor);
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
	output.Color = EncodeForwardSceneColor(color);
	output.GTAOContribution = gtaoContribution;
	return output;
}
#else
#define ForwardPBRPixelOutput float4

float4 MakeForwardPBRPixelOutput(float4 color, float4 legacyColor, float4 gtaoContribution)
{
	return EncodeForwardSceneColor(color);
}
#endif

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
float3 SampleNormalWS(MaterialTextureBindingData binding, float normalScale,
	float3 normalWS, float4 tangentWS, float3 positionWS, float2 uv)
{
	// TODO: flip Y for normal map?

	// Sample normal texture
	float4 normalSampled = SampleTextureBinding(binding.TextureSamplerBinding, uv);

	// Remap from [0,1] to [-1,1], xy only
	normalSampled.xy = normalSampled.xy * 2.0 - 1.0;

	// Apply normal scale xy
	normalSampled.xy *= normalScale;

	// rebuild z, avoid normalization issues
	normalSampled.z = sqrt(saturate(1.0 - dot(normalSampled.xy, normalSampled.xy)));

	// Authored tangents describe UV0. A different UV set or a transformed normal
	// map needs a frame derived from the actual sampled coordinates, including
	// rotations and mirrored scales.
	const bool transformedFrame = binding.TexCoordIndex != 0u ||
		any(abs(binding.UVTransformU.xy - float2(1.0, 0.0)) > 1.0e-6) ||
		any(abs(binding.UVTransformV.xy - float2(0.0, 1.0)) > 1.0e-6);
	float3x3 TBN = transformedFrame
		? BuildTBN(normalWS, positionWS, uv)
		: BuildTBNFromTangent(normalWS, tangentWS, positionWS, uv);

	// Transform normal from tangent space to world space
	float3 perturbedNormalWS = SafeNormalize(mul(normalSampled.xyz, TBN), TBN[2]);
	return perturbedNormalWS;
}

BaseShadingState BuildBaseShadingState(SurfaceData surface, float3 normalWS,
	bool specularAAEnabled)
{
	BaseShadingState state;
	state.NormalWS = normalWS;
	state.AuthoredPerceptualRoughness = surface.Roughness;
	const SpecularAAResult specularAA = EvaluateSpecularAA(surface.Roughness, normalWS,
		specularAAEnabled);
	state.EffectivePerceptualRoughness = specularAA.EffectivePerceptualRoughness;
	state.BRDFAlpha = PerceptualRoughnessToAlpha(state.EffectivePerceptualRoughness);
	state.NormalVariance = specularAA.NormalVariance;
	state.SpecularAAKernelAlpha = specularAA.KernelAlpha;
	// glTF reserves zero for an infinite-IOR Fresnel response (F0 = 1).
	const float ior = surface.Ior == 0.0 ? 0.0 : max(surface.Ior, 1.0);
	const float dielectricReflectance = (ior - 1.0) / (ior + 1.0);
	state.F0 = lerp((dielectricReflectance * dielectricReflectance).xxx,
		surface.BaseColor, surface.Metallic);
	state.FeatureFlags = 0u;
	if (surface.ClearcoatFactor > 0.0)
	{
		state.FeatureFlags |= 1u;
	}
	if (surface.AnisotropyStrength > 0.0)
	{
		state.FeatureFlags |= 2u;
	}
	if (any(surface.SheenColor > 0.0.xxx))
	{
		state.FeatureFlags |= 4u;
	}
	return state;
}

AnisotropyShadingState BuildAnisotropyShadingState(SurfaceData surface, float3 normalWS,
	float3 shadingNormalWS, float4 tangentWS, float3 positionWS, float2 normalUV,
	float authoredBaseAlpha, float kernelAlpha, bool evaluateFrame)
{
	AnisotropyShadingState state;
	state.Strength = surface.AnisotropyStrength;
	state.TangentWS = 0.0.xxx;
	state.BitangentWS = 0.0.xxx;
	state.AlphaB = saturate(authoredBaseAlpha + kernelAlpha);
	state.AlphaT = state.AlphaB;
	if (!evaluateFrame) return state;
	const float2 filteredAlpha = FilterAnisotropicAlpha(authoredBaseAlpha,
		state.Strength, kernelAlpha);
	state.AlphaT = filteredAlpha.x;
	state.AlphaB = filteredAlpha.y;

	// A missing mesh tangent uses the same normal-map coordinates as normal
	// perturbation, including the selected UV set and texture transform.
	const float3x3 frame = BuildTBNFromTangent(normalWS, tangentWS, positionWS, normalUV);
	const float2 direction = surface.AnisotropyDirectionTS;
	const float3 tangent = frame[0] * direction.x + frame[1] * direction.y;
	const float3 projected = tangent - shadingNormalWS * dot(shadingNormalWS, tangent);
	const float3 fallbackTangent = SafeNormalize(cross(frame[1], shadingNormalWS), frame[0]);
	state.TangentWS = SafeNormalize(projected, fallbackTangent);
	const float handedness = dot(cross(frame[0], frame[1]), normalWS) < 0.0 ? -1.0 : 1.0;
	state.BitangentWS = SafeNormalize(cross(shadingNormalWS, state.TangentWS), frame[1]) * handedness;
	return state;
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

float3 ProjectToTangentHemisphere(float3 direction, float3 normalWS)
{
	const float NoD = dot(direction, normalWS);
	return SafeNormalize(direction + normalWS * max(0.01 - NoD, 0.0), normalWS);
}

float3 SampleAnisotropicIBL(float3 reflectWS, float3 normalWS,
	AnisotropyShadingState anisotropy)
{
	// The environment is prefiltered isotropically. Three normalized taps at the
	// minor-axis mip approximate the elongated major-axis reflection footprint.
	const float3 axis = anisotropy.TangentWS - reflectWS * dot(anisotropy.TangentWS, reflectWS);
	const float3 majorDirection = SafeNormalize(axis, anisotropy.TangentWS);
	const float spread = 0.5 * max(anisotropy.AlphaT - anisotropy.AlphaB, 0.0);
	const float minorRoughness = sqrt(anisotropy.AlphaB);
	const float3 center = SampleIBLPrefilteredSpecular(
		ProjectToTangentHemisphere(reflectWS, normalWS), minorRoughness);
	const float3 positive = SampleIBLPrefilteredSpecular(
		ProjectToTangentHemisphere(
			SafeNormalize(reflectWS + majorDirection * spread, reflectWS), normalWS), minorRoughness);
	const float3 negative = SampleIBLPrefilteredSpecular(
		ProjectToTangentHemisphere(
			SafeNormalize(reflectWS - majorDirection * spread, reflectWS), normalWS), minorRoughness);
	return 0.5 * center + 0.25 * (positive + negative);
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
	SamplerComparisonState samplerState = GetSamplerComparisonState(g_Pass.ShadowMapSamplerIndex);
	const float2 texelSize = 1.0.xx / max((float) g_Pass.ShadowMapSize, 1.0);
	const float2 gradient = ComputeShadowReceiverDepthGradient(receiver, viewIndex);
	const float residualBias = EvaluateDirectionalShadowReceiverBias(cascadeIndex, receiverNoL, g_Shadow);
	// Each hardware comparison blends four texels. Account for its one-texel support,
	// while PCF tap offsets below follow the actual receiver plane instead of adding bias.
	const float bilinearBias = ShadowBilinearReceiverBias(gradient.x, gradient.y, texelSize.x, texelSize.y);
	const float compareDepth = projection.ReceiverDepth - residualBias - bilinearBias;
	if (!IsShadowPCFEnabled())
	{
		return SampleShadowHard(shadowMap, samplerState, projection.UV, cascadeIndex, saturate(compareDepth));
	}
	return SampleShadowPCF3x3(shadowMap, samplerState, projection.UV, cascadeIndex,
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
	const float3 cascadeColors[4] =
	{
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

float3 WorldSunTransmittance(float3 positionWS, float3 sunDirection)
{
	if (g_Pass.AtmosphereTransmittanceIndex == 0xffffffffu)
	{
		return 1.0.xxx;
	}
	float3 positionKm = positionWS * g_Scene.AtmosphereWorld.w - g_Scene.AtmosphereWorld.xyz;
	float radiusKm = length(positionKm);
	float bottomKm = g_Scene.AtmosphereRadii.x;
	float topKm = g_Scene.AtmosphereRadii.y;
	float mu = dot(positionKm, sunDirection) / max(radiusKm, 1.0e-6);
	float b = radiusKm * mu;
	float c = (radiusKm - bottomKm) * (radiusKm + bottomKm);
	float discriminant = b * b - c;
	if (b < 0.0 && discriminant >= 0.0 && -b - sqrt(discriminant) > 0.0001)
	{
		return 0.0.xxx;
	}
	float2 unitUV = saturate(float2(mu * 0.5 + 0.5,
		(radiusKm - bottomKm) / max(topKm - bottomKm, 1.0e-6)));
	float2 uv = (unitUV * float2(255.0, 63.0) + 0.5) / float2(256.0, 64.0);
	return GetTexture2DFloat4(g_Pass.AtmosphereTransmittanceIndex).SampleLevel(
		GetSamplerState(g_Pass.AtmosphereSamplerIndex), uv, 0).rgb;
}

float3 WorldSunDiskSpecular(float3 centerDirection, float3 N, float3 V, float3 F0,
	float physicalRoughness)
{
	const float sineRadius = sin(g_Scene.WorldSunAngularRadius);
	const float sineRadiusSquared = sineRadius * sineRadius;
	const float3 referenceAxis = abs(centerDirection.y) < 0.99 ?
		float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(referenceAxis, centerDirection));
	const float3 bitangent = cross(centerDirection, tangent);
	const float NoV = saturate(dot(N, V));
	float3 integrated = 0.0.xxx;
	[unroll]
	for (uint sampleIndex = 0; sampleIndex < 32u; ++sampleIndex)
	{
		const float radial = sqrt((sampleIndex + 0.5) / 32.0 * sineRadiusSquared);
		const float phase = sampleIndex * 2.39996322973;
		const float3 L = centerDirection * sqrt(1.0 - radial * radial) +
			(tangent * cos(phase) + bitangent * sin(phase)) * radial;
		const float NoL = saturate(dot(N, L));
		const float3 H = SafeNormalize(L + V, N);
		const float NoH = saturate(dot(N, H));
		const float VoH = saturate(dot(V, H));
		integrated += D_GGX(NoH, physicalRoughness) *
			V_SmithGGXCorrelated(NoV, NoL, physicalRoughness) *
			F_Schlick(F0, 1.0.xxx, VoH) * NoL;
	}
	// Solid-angle samples reconstruct the authored perpendicular disk illuminance.
	return integrated * (2.0 / (1.0 + cos(g_Scene.WorldSunAngularRadius))) / 32.0;
}

float3 WorldSunDiskAnisotropicSpecular(float3 centerDirection, float3 N, float3 V,
	float3 F0, AnisotropyShadingState anisotropy)
{
	const float sineRadius = sin(g_Scene.WorldSunAngularRadius);
	const float sineRadiusSquared = sineRadius * sineRadius;
	const float3 referenceAxis = abs(centerDirection.y) < 0.99 ?
		float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
	const float3 tangent = normalize(cross(referenceAxis, centerDirection));
	const float3 bitangent = cross(centerDirection, tangent);
	float3 integrated = 0.0.xxx;
	[unroll]
	for (uint sampleIndex = 0; sampleIndex < 32u; ++sampleIndex)
	{
		const float radial = sqrt((sampleIndex + 0.5) / 32.0 * sineRadiusSquared);
		const float phase = sampleIndex * 2.39996322973;
		const float3 L = centerDirection * sqrt(1.0 - radial * radial) +
			(tangent * cos(phase) + bitangent * sin(phase)) * radial;
		const float NoL = saturate(dot(N, L));
		const float3 H = SafeNormalize(L + V, N);
		const float VoH = saturate(dot(V, H));
		integrated += D_GGXAnisotropic(H, N, anisotropy.TangentWS,
			anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB) *
			V_SmithGGXCorrelatedAnisotropic(V, L, N, anisotropy.TangentWS,
				anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB) *
			F_Schlick(F0, 1.0.xxx, VoH) * NoL;
	}
	// The same 32 solid-angle samples and perpendicular-illuminance normalization
	// as the isotropic world-sun lobe keep the two paths comparable.
	return integrated * (2.0 / (1.0 + cos(g_Scene.WorldSunAngularRadius))) / 32.0;
}

float3 EvaluateClearcoatDirect(float3 L, float3 V, ClearcoatShadingState coat,
	bool worldSun)
{
	const float NoL = saturate(dot(coat.NormalWS, L));
	if (NoL <= 0.0) return 0.0.xxx;
	const float3 F0 = 0.04.xxx;
	if (worldSun && coat.BRDFAlpha < 0.04)
	{
		// Integrate the finite solar disk with the coat's own normal and GGX width.
		return WorldSunDiskSpecular(L, coat.NormalWS, V, F0, coat.BRDFAlpha) *
			coat.EnergyCompensation;
	}
	const float3 H = SafeNormalize(L + V, coat.NormalWS);
	const float NoH = saturate(dot(coat.NormalWS, H));
	const float VoH = saturate(dot(V, H));
	return D_GGX(NoH, coat.BRDFAlpha) *
		V_SmithGGXCorrelated(coat.NoV, NoL, coat.BRDFAlpha) *
		F_Schlick(F0, 1.0.xxx, VoH) * coat.EnergyCompensation * NoL;
}

float3 EvaluateSheenDirect(float3 L, float3 V, float3 N, float NoV,
	SheenShadingState sheen, bool worldSun)
{
	const float NoL = saturate(dot(N, L));
	if (NoL <= 0.0 || !any(sheen.Color > 0.0.xxx)) return 0.0.xxx;
	if (worldSun && sheen.Alpha < 0.04)
	{
		const float sineRadius = sin(g_Scene.WorldSunAngularRadius);
		const float sineRadiusSquared = sineRadius * sineRadius;
		const float3 referenceAxis = abs(L.y) < 0.99 ?
			float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
		const float3 tangent = normalize(cross(referenceAxis, L));
		const float3 bitangent = cross(L, tangent);
		float integrated = 0.0;
		[unroll]
		for (uint sampleIndex = 0; sampleIndex < 32u; ++sampleIndex)
		{
			const float radial = sqrt((sampleIndex + 0.5) / 32.0 * sineRadiusSquared);
			const float phase = sampleIndex * 2.39996322973;
			const float3 sampleDirection = L * sqrt(1.0 - radial * radial) +
				(tangent * cos(phase) + bitangent * sin(phase)) * radial;
			const float sampleNoL = saturate(dot(N, sampleDirection));
			const float3 H = SafeNormalize(sampleDirection + V, N);
			integrated += D_Charlie(saturate(dot(N, H)), sheen.Alpha) *
				V_Charlie(NoV, sampleNoL, sheen.Alpha) * sampleNoL /
				CharlieSheenNormalization(NoV, sampleNoL, sheen.PerceptualRoughness);
		}
		return sheen.Color * integrated *
			(2.0 / (1.0 + cos(g_Scene.WorldSunAngularRadius))) / 32.0;
	}
	const float3 H = SafeNormalize(L + V, N);
	return sheen.Color * D_Charlie(saturate(dot(N, H)), sheen.Alpha) *
		V_Charlie(NoV, NoL, sheen.Alpha) * NoL /
		CharlieSheenNormalization(NoV, NoL, sheen.PerceptualRoughness);
}

struct DirectLightingResult
{
	float3 Total;
	float3 Sheen;
};

DirectLightingResult EvaluateDirectLight(uint lightIndex, float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver, float3 V, float NoV,
	float3 F0, float physicalRoughness, float3 baseColor, float3 diffuseWeight,
	float3 energyCompensation, AnisotropyShadingState anisotropy, ClearcoatShadingState coat,
	SheenShadingState sheen)
{
	DirectLightingResult result = { 0.0.xxx, 0.0.xxx };
	const LightData light = g_Lights[lightIndex];
	float3 L = 0.0.xxx;
	float attenuation = 1.0;
	if (!ResolveLightVector(light, positionWS, L, attenuation))
	{
		return result;
	}

	const float NoL = saturate(dot(N, L));
	if (NoL <= 0.0 && (coat.Factor <= 0.0 || dot(coat.NormalWS, L) <= 0.0))
	{
		return result;
	}

	const float3 H = SafeNormalize(L + V, N);
	const float NoH = saturate(dot(N, H));
	const float VoH = saturate(dot(V, H));
	float D;
	float visibility;
	if (anisotropy.Strength > 0.0)
	{
		D = D_GGXAnisotropic(H, N, anisotropy.TangentWS,
			anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB);
		visibility = V_SmithGGXCorrelatedAnisotropic(V, L, N,
			anisotropy.TangentWS, anisotropy.BitangentWS, anisotropy.AlphaT, anisotropy.AlphaB);
	}
	else
	{
		D = D_GGX(NoH, physicalRoughness);
		visibility = V_SmithGGXCorrelated(NoV, NoL, physicalRoughness);
	}
	const float3 F = F_Schlick(F0, 1.0.xxx, VoH);
	const float3 specular = D * visibility * F * energyCompensation;
	const float3 diffuse = diffuseWeight * Fd_Lambert(baseColor);

	float shadowVisibility = 1.0;
	if (light.LightType == 0u && lightIndex == g_Scene.DirectionalShadowLightIndex)
	{
		shadowVisibility = SampleDirectionalShadow(positionWS, shadowReceiver, dot(shadowReceiver.NormalWS, L));
	}

	// The designated world sun supplies Y-normalized RGB and perpendicular lux.
	// The disk specular integrates the same illuminance; do not apply another pi factor.
	float3 illuminance = light.Color.rgb * light.Intensity;
	if (lightIndex == g_Scene.WorldSunLightIndex)
	{
		illuminance *= WorldSunTransmittance(positionWS, L);
	}
	float3 directResponse = (diffuse + specular) * NoL;
	// The isotropic center approximation stayed within 1% above perceptual
	// roughness 0.2. The anisotropic lobe uses its narrower width for the disk gate.
	if (lightIndex == g_Scene.WorldSunLightIndex &&
		(anisotropy.Strength > 0.0 ? anisotropy.AlphaB : physicalRoughness) < 0.04)
	{
		const float3 diskSpecular = anisotropy.Strength > 0.0
			? WorldSunDiskAnisotropicSpecular(L, N, V, F0, anisotropy)
			: WorldSunDiskSpecular(L, N, V, F0, physicalRoughness);
		directResponse = diffuse * NoL + diskSpecular * energyCompensation;
	}
	if (any(sheen.Color > 0.0.xxx))
	{
		const float incidentEnergy = SheenDirectionalAlbedo(
			NoL, sheen.DirectionalAlbedoFitLow, sheen.DirectionalAlbedoFitHigh);
		const float baseTransmission = 1.0 - max(sheen.Color.r,
			max(sheen.Color.g, sheen.Color.b)) * max(sheen.ViewDirectionalAlbedo, incidentEnergy);
		result.Sheen = EvaluateSheenDirect(L, V, N, NoV, sheen,
			lightIndex == g_Scene.WorldSunLightIndex);
		directResponse = directResponse * baseTransmission + result.Sheen;
	}
	if (coat.Factor > 0.0)
	{
		const float coatNoL = saturate(dot(coat.NormalWS, L));
		// Two interface crossings attenuate the base; the reflected coat lobe is
		// evaluated once. The LUT carries the rough coat's outgoing reflectance.
		const float incidentReflectance = F_Schlick(0.04.xxx, 1.0.xxx, coatNoL).x;
		const float transmission = (1.0 - coat.Factor * coat.DirectionalAlbedo) *
			(1.0 - coat.Factor * incidentReflectance);
		directResponse = directResponse * transmission + coat.Factor *
			EvaluateClearcoatDirect(L, V, coat, lightIndex == g_Scene.WorldSunLightIndex);
		result.Sheen *= transmission;
	}
	result.Total = directResponse * illuminance * attenuation * shadowVisibility;
	result.Sheen *= illuminance * attenuation * shadowVisibility;
	return result;
}

DirectLightingResult EvaluateLegacyDirectLighting(float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver, float3 V, float NoV,
	float3 F0, float physicalRoughness, float3 baseColor, float3 diffuseWeight,
	float3 energyCompensation, AnisotropyShadingState anisotropy, ClearcoatShadingState coat,
	SheenShadingState sheen)
{
	DirectLightingResult lighting = { 0.0.xxx, 0.0.xxx };
	for (uint lightOffset = 0; lightOffset < g_Scene.LightCount; ++lightOffset)
	{
		const uint lightIndex = g_Scene.LightBaseIndex + lightOffset;
		const DirectLightingResult light = EvaluateDirectLight(lightIndex, positionWS, N,
			shadowReceiver, V, NoV, F0, physicalRoughness, baseColor, diffuseWeight,
			energyCompensation, anisotropy, coat, sheen);
		lighting.Total += light.Total;
		lighting.Sheen += light.Sheen;
	}
	return lighting;
}

#if defined(GGLAB_FORWARD_PLUS)
uint GetForwardPlusGlobalLightIndex(uint listIndex)
{
	return listIndex < 2u ? g_Pass.ForwardPlusGlobalLightIndices01[listIndex]
		: g_Pass.ForwardPlusGlobalLightIndices23[listIndex - 2u];
}

DirectLightingResult EvaluateForwardPlusDirectLighting(float2 pixelPosition, float3 positionWS, float3 N, ShadowReceiverPlane shadowReceiver,
	float3 V, float NoV, float3 F0, float physicalRoughness, float3 baseColor,
	float3 diffuseWeight, float3 energyCompensation, AnisotropyShadingState anisotropy,
	ClearcoatShadingState coat, SheenShadingState sheen)
{
	DirectLightingResult lighting = { 0.0.xxx, 0.0.xxx };
	const uint globalLightCount = min(
		g_Pass.ForwardPlusGlobalLightCount, FORWARD_PLUS_GLOBAL_LIGHT_CAPACITY);
	for (uint listOffset = 0; listOffset < globalLightCount; ++listOffset)
	{
		const uint lightIndex = GetForwardPlusGlobalLightIndex(listOffset);
		if (lightIndex >= g_Scene.LightBaseIndex &&
			lightIndex < g_Scene.LightBaseIndex + g_Scene.LightCount)
		{
			const DirectLightingResult light = EvaluateDirectLight(lightIndex, positionWS, N,
				shadowReceiver, V, NoV, F0, physicalRoughness, baseColor, diffuseWeight,
				energyCompensation, anisotropy, coat, sheen);
			lighting.Total += light.Total;
			lighting.Sheen += light.Sheen;
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
		const DirectLightingResult light = EvaluateDirectLight(lightIndex, positionWS, N,
			shadowReceiver, V, NoV, F0, physicalRoughness, baseColor, diffuseWeight,
			energyCompensation, anisotropy, coat, sheen);
		lighting.Total += light.Total;
		lighting.Sheen += light.Sheen;
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
	const float authoredRoughness = surface.Roughness;

	// Normal (linear)
	float3 normalWS = SafeNormalize(IN.NormalWS, float3(0.0, 1.0, 0.0));
	float4 tangentWS = IN.TangentWS;
	if ((matData.Flags & 1u) != 0u && !isFrontFace)
	{
		normalWS = -normalWS;
	}
	float2 normalUV = SelectUV(matData.NormalBinding, IN.UV0, IN.UV1);
	float3 N = SampleNormalWS(matData.NormalBinding, matData.NormalScale,
		normalWS, tangentWS, IN.PositionWS, normalUV);
	float3 clearcoatNormalWS = normalWS;
	if ((matData.ClearcoatFactor > 0.0 ||
		(matData.DebugView == MaterialDebugViewClearcoatNormal ||
			matData.DebugView == MaterialDebugViewEffectiveClearcoatRoughness)) &&
		matData.ClearcoatNormalBinding.TextureEnabled != 0u)
	{
		const float2 clearcoatUV = SelectUV(matData.ClearcoatNormalBinding, IN.UV0, IN.UV1);
		clearcoatNormalWS = SampleNormalWS(matData.ClearcoatNormalBinding,
			matData.ClearcoatNormalScale, normalWS, tangentWS, IN.PositionWS, clearcoatUV);
	}
	const bool specularAAEnabled = matData.DebugView != MaterialDebugViewUnfilteredLit;
	const BaseShadingState shading = BuildBaseShadingState(surface, N, specularAAEnabled);
	// Derivatives are evaluated before feature and debug-view branches.
	const SpecularAAResult coatSpecularAA = EvaluateSpecularAA(
		surface.ClearcoatRoughness, clearcoatNormalWS, specularAAEnabled);
	const AnisotropyShadingState anisotropy = BuildAnisotropyShadingState(surface,
		normalWS, N, tangentWS, IN.PositionWS, normalUV,
		PerceptualRoughnessToAlpha(ClampPerceptualRoughnessForBRDF(authoredRoughness)),
		shading.SpecularAAKernelAlpha,
		matData.AnisotropyStrength > 0.0 ||
		matData.DebugView == MaterialDebugViewAnisotropyDirectionWorld);
	float perceptualRoughness = ClampPerceptualRoughnessForBRDF(authoredRoughness);

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
	if (matData.DebugView == MaterialDebugViewAuthoredRoughness)
	{
		const float4 color = float4(shading.AuthoredPerceptualRoughness.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewEffectiveRoughness)
	{
		const float4 color = float4(shading.EffectivePerceptualRoughness.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewNormalVariance)
	{
		const float4 color = float4(shading.NormalVariance,
			surface.ClearcoatFactor > 0.0 ? coatSpecularAA.NormalVariance : 0.0,
			0.0, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewSpecularAAContribution)
	{
		const float4 color = float4(shading.SpecularAAKernelAlpha,
			surface.ClearcoatFactor > 0.0 ? coatSpecularAA.KernelAlpha : 0.0,
			shading.EffectivePerceptualRoughness -
				ClampPerceptualRoughnessForBRDF(authoredRoughness), alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewF0)
	{
		const float4 color = float4(shading.F0, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewIor)
	{
		// Preserve an unbounded IOR range in a bounded grayscale diagnostic.
		const float mappedIor = surface.Ior == 0.0 ? 1.0 :
			1.0 - 1.0 / max(surface.Ior, 1.0);
		const float4 color = float4(mappedIor.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewClearcoatFactor)
	{
		const float4 color = float4(surface.ClearcoatFactor.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewClearcoatRoughness)
	{
		const float4 color = float4(surface.ClearcoatRoughness.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewEffectiveClearcoatRoughness)
	{
		const float4 color = float4(coatSpecularAA.EffectivePerceptualRoughness.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewClearcoatNormal)
	{
		const float4 color = float4(clearcoatNormalWS * 0.5 + 0.5, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewAnisotropyStrength)
	{
		const float4 color = float4(surface.AnisotropyStrength.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewAnisotropicAlpha)
	{
		const float4 color = float4(anisotropy.AlphaT, anisotropy.AlphaB, 0.0, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewAnisotropyDirectionTangent)
	{
		const float2 direction = surface.AnisotropyDirectionTS * 0.5 + 0.5;
		const float4 color = float4(direction, 0.5, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewAnisotropyDirectionWorld)
	{
		const float4 color = float4(anisotropy.TangentWS * 0.5 + 0.5, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewSheenColor)
	{
		const float4 color = float4(surface.SheenColor, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewSheenRoughness)
	{
		const float4 color = float4(surface.SheenRoughness.xxx, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	if (matData.DebugView == MaterialDebugViewFeatureFlags)
	{
		const float3 enabled = float3((shading.FeatureFlags & 1u) != 0u,
			(shading.FeatureFlags & 2u) != 0u, (shading.FeatureFlags & 4u) != 0u);
		const float4 color = float4(enabled, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}
	perceptualRoughness = shading.EffectivePerceptualRoughness;

	// Shading
	float3 V = SafeNormalize(viewData.CameraPos.xyz - IN.PositionWS, N); // View direction
	float NoV = saturate(dot(N, V));

	// convert artistic roughness to physical roughness
	float a = shading.BRDFAlpha;

	float3 F0 = shading.F0;
	// The isotropic split-sum LUT estimates directional energy for the
	// anisotropic lobe using its unmodified base roughness.
	const float2 brdfLUT = SampleIBLBrdfLUT(NoV, perceptualRoughness);
	const float3 energyCompensation = GGXEnergyCompensation(F0, brdfLUT);
	const float3 specularDirectionalAlbedo =
		saturate((F0 * brdfLUT.x + brdfLUT.y) * energyCompensation);
	// The view-integrated GGX albedo also weights direct diffuse. This is a
	// directional approximation until incident-angle coupling is modeled.
	const float3 diffuseWeight = (1.0.xxx - specularDirectionalAlbedo) * (1.0 - metallic);
	ClearcoatShadingState coat;
	coat.Factor = surface.ClearcoatFactor;
	coat.NormalWS = clearcoatNormalWS;
	coat.DirectionalAlbedo = 0.0;
	coat.EnergyCompensation = 1.0.xxx;
	coat.PerceptualRoughness = coatSpecularAA.EffectivePerceptualRoughness;
	coat.BRDFAlpha = PerceptualRoughnessToAlpha(coat.PerceptualRoughness);
	coat.NoV = saturate(dot(clearcoatNormalWS, V));
	if (coat.Factor > 0.0)
	{
		const float2 coatLUT = SampleIBLBrdfLUT(coat.NoV, coat.PerceptualRoughness);
		coat.EnergyCompensation = GGXEnergyCompensation(0.04.xxx, coatLUT);
		coat.DirectionalAlbedo = saturate(
			(0.04 * coatLUT.x + coatLUT.y) * coat.EnergyCompensation.x);
	}
	SheenShadingState sheen;
	sheen.Color = surface.SheenColor;
	sheen.PerceptualRoughness = 0.0;
	sheen.Alpha = 0.0;
	sheen.ViewDirectionalAlbedo = 0.0;
	sheen.DirectionalAlbedoFitLow = 0.0.xxxx;
	sheen.DirectionalAlbedoFitHigh = 0.0.xxx;
	if (any(sheen.Color > 0.0.xxx))
	{
		// Charlie sheen retains independent authored roughness: the GGX alpha
		// footprint is not calibrated for this grazing microfiber lobe.
		sheen.PerceptualRoughness = ClampPerceptualRoughnessForBRDF(
			surface.SheenRoughness);
		sheen.Alpha = PerceptualRoughnessToAlpha(sheen.PerceptualRoughness);
		PrepareSheenDirectionalAlbedo(sheen.PerceptualRoughness,
			sheen.DirectionalAlbedoFitLow, sheen.DirectionalAlbedoFitHigh);
		sheen.ViewDirectionalAlbedo = SheenDirectionalAlbedo(NoV,
			sheen.DirectionalAlbedoFitLow, sheen.DirectionalAlbedoFitHigh);
	}
#if defined(GGLAB_FORWARD_PLUS)
	const DirectLightingResult directLighting = EvaluateForwardPlusDirectLighting(IN.PositionCS.xy,
		IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor, diffuseWeight,
		energyCompensation, anisotropy, coat, sheen);
#else
	const DirectLightingResult directLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor,
			diffuseWeight, energyCompensation, anisotropy, coat, sheen);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	const DirectLightingResult legacyDirectLighting =
		EvaluateLegacyDirectLighting(IN.PositionWS, N, shadowReceiver, V, NoV, F0, a, baseColor,
			diffuseWeight, energyCompensation, anisotropy, coat, sheen);
#endif

	// Emissive (resolved by the surface seam from the emissive texture)
	// The clearcoat layer sits above emission and attenuates its outgoing light.
	const float coatFresnel = F_Schlick(0.04.xxx, 1.0.xxx, coat.NoV).x;
	const float3 emissive = surface.Emissive * (1.0 - coat.Factor * coatFresnel);

	// IBL
	float3 diffuseIBL = SampleIBLIrradiance(N) * diffuseWeight * Fd_Lambert(baseColor);

	float3 reflectWS = reflect(-V, N);
	float3 prefilteredEnv = anisotropy.Strength > 0.0
		? SampleAnisotropicIBL(reflectWS, N, anisotropy)
		: SampleIBLPrefilteredSpecular(reflectWS, perceptualRoughness);
	float3 specularIBL = prefilteredEnv * specularDirectionalAlbedo;
	float3 sheenIBL = 0.0.xxx;
	if (any(sheen.Color > 0.0.xxx))
	{
		const float maxSheenColor = max(sheen.Color.r, max(sheen.Color.g, sheen.Color.b));
		const float baseTransmission = 1.0 - maxSheenColor * sheen.ViewDirectionalAlbedo;
		diffuseIBL *= baseTransmission;
		specularIBL *= baseTransmission;
		// The GGX-prefiltered environment is sampled in the backscatter direction.
		// This is a roughness-aware approximation of Charlie IBL, not a Charlie prefilter.
		sheenIBL = SampleIBLPrefilteredSpecular(V, sheen.PerceptualRoughness) *
			sheen.Color * sheen.ViewDirectionalAlbedo;
	}
	if (coat.Factor > 0.0)
	{
		const float baseTransmission = 1.0 - coat.Factor * coat.DirectionalAlbedo;
		diffuseIBL *= baseTransmission * baseTransmission;
		specularIBL *= baseTransmission * baseTransmission;
		sheenIBL *= baseTransmission * baseTransmission;
		const float3 coatReflection = reflect(-V, clearcoatNormalWS);
		const float3 coatEnvironment = SampleIBLPrefilteredSpecular(
			coatReflection, coat.PerceptualRoughness);
		specularIBL += coatEnvironment * (coat.Factor * coat.DirectionalAlbedo);
	}

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
	sheenIBL *= ResolveSpecularIBLVisibility(ao);
	if (matData.DebugView == MaterialDebugViewSheenContribution)
	{
		const float4 color = float4(directLighting.Sheen + sheenIBL, alpha);
		return MakeForwardPBRPixelOutput(color, color, 0.0.xxxx);
	}

	float3 outputLighting = directLighting.Total;
	outputLighting += emissive;
	outputLighting += diffuseIBL + specularIBL + sheenIBL;
	const float4 outputColor = float4(
		ApplyShadowDiagnosticsOverlay(outputLighting, IN.PositionWS), alpha);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float3 legacyOutputLighting = legacyDirectLighting.Total;
	legacyOutputLighting += emissive;
	legacyOutputLighting += diffuseIBL + specularIBL + sheenIBL;
	const float4 legacyColor = float4(
		ApplyShadowDiagnosticsOverlay(legacyOutputLighting, IN.PositionWS), alpha);
	return MakeForwardPBRPixelOutput(outputColor, legacyColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0));
#else
	return MakeForwardPBRPixelOutput(outputColor, outputColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0));
#endif
}
