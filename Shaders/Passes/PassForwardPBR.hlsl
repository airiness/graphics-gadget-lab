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
#include <PBR/DirectLighting.hlsli>
#include <PBR/IndirectLighting.hlsli>
#include <PBR/MaterialDiagnostics.hlsli>

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

#if defined(GGLAB_FORWARD_PLUS_VALIDATION) || defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT) || defined(GGLAB_MATERIAL_DIAGNOSTICS)
struct ForwardPBRPixelOutput
{
	float4 Color : SV_Target0;
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float4 AllLightsReferenceColor : SV_Target1;
#endif
#if defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float4 GTAOContribution : SV_Target2;
#else
	float4 GTAOContribution : SV_Target1;
#endif
#endif
#if defined(GGLAB_MATERIAL_DIAGNOSTICS)
#if defined(GGLAB_FORWARD_PLUS_VALIDATION) && defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
	float4 MaterialDiagnosticColor : SV_Target3;
	float4 MaterialDiagnosticCoverage : SV_Target4;
	float4 MaterialDiagnosticLighting : SV_Target5;
#elif defined(GGLAB_FORWARD_PLUS_VALIDATION) || defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
	float4 MaterialDiagnosticColor : SV_Target2;
	float4 MaterialDiagnosticCoverage : SV_Target3;
	float4 MaterialDiagnosticLighting : SV_Target4;
#else
	float4 MaterialDiagnosticColor : SV_Target1;
	float4 MaterialDiagnosticCoverage : SV_Target2;
	float4 MaterialDiagnosticLighting : SV_Target3;
#endif
#endif
};

ForwardPBRPixelOutput MakeForwardPBRPixelOutput(float4 color, float4 allLightsReferenceColor,
	float4 gtaoContribution, float3 diagnosticColor = 0.0.xxx, bool diagnostic = false)
{
	ForwardPBRPixelOutput output;
	output.Color = EncodeForwardSceneColor(color);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	output.AllLightsReferenceColor = EncodeForwardSceneColor(allLightsReferenceColor);
#endif
#if defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT)
	output.GTAOContribution = gtaoContribution;
#endif
#if defined(GGLAB_MATERIAL_DIAGNOSTICS)
	// Preserve full scene radiance for metering while tracking the part that
	// must be replaced by display-linear diagnostics during final composition.
	const MaterialDiagnosticOutput diagnostics = MakeMaterialDiagnosticOutput(output.Color, diagnosticColor, diagnostic);
	output.MaterialDiagnosticColor = diagnostics.Color;
	output.MaterialDiagnosticCoverage = diagnostics.Coverage;
	output.MaterialDiagnosticLighting = diagnostics.Lighting;
#endif
	return output;
}
#else
#define ForwardPBRPixelOutput float4

float4 MakeForwardPBRPixelOutput(float4 color, float4 allLightsReferenceColor, float4 gtaoContribution,
	float3 diagnosticColor = 0.0.xxx, bool diagnostic = false)
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

MaterialIBLResponse SampleMaterialEnvironment(PreparedMaterialShading material)
{
	MaterialIBLSamples environment;
	environment.Irradiance = SampleIBLIrradiance(material.Base.NormalWS);
	environment.BaseSpecular = SampleIBLPrefilteredSpecular(
		GetMaterialIBLReflection(material), material.Base.EffectivePerceptualRoughness);
	environment.ClearcoatSpecular = 0.0.xxx;
	MaterialIBLResponse response = EvaluateBaseMaterialIBL(material, environment);
	if (material.Clearcoat.Factor > 0.0)
	{
		const float3 reflection = reflect(-material.ViewDirectionWS, material.Clearcoat.NormalWS);
		environment.ClearcoatSpecular = SampleIBLPrefilteredSpecular(
			reflection, material.Clearcoat.PerceptualRoughness);
		ApplyClearcoatIBL(response, material.Clearcoat, environment.ClearcoatSpecular);
	}
	return response;
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

float3 EvaluateDirectLight(uint lightIndex, float3 positionWS,
	ShadowReceiverPlane shadowReceiver, PreparedMaterialShading material)
{
	float3 result = 0.0.xxx;
	const LightData light = g_Lights[lightIndex];
	float3 L = 0.0.xxx;
	float attenuation = 1.0;
	if (!ResolveLightVector(light, positionWS, L, attenuation))
	{
		return result;
	}

	const float NoL = saturate(dot(material.Base.NormalWS, L));
	const float coatNoL = material.Clearcoat.Factor > 0.0
		? saturate(dot(material.Clearcoat.NormalWS, L)) : 0.0;
	if (NoL <= 0.0 && coatNoL <= 0.0)
	{
		return result;
	}

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
	const float3 directResponse = EvaluateDirectMaterialResponse(L, NoL, coatNoL, material,
		lightIndex == g_Scene.WorldSunLightIndex, g_Scene.WorldSunAngularRadius);
	result = directResponse * illuminance * attenuation * shadowVisibility;
	return result;
}

float3 EvaluateAllDirectLights(float3 positionWS,
	ShadowReceiverPlane shadowReceiver, PreparedMaterialShading material)
{
	float3 lighting = 0.0.xxx;
	for (uint lightOffset = 0; lightOffset < g_Scene.LightCount; ++lightOffset)
	{
		const uint lightIndex = g_Scene.LightBaseIndex + lightOffset;
		const float3 light = EvaluateDirectLight(lightIndex, positionWS, shadowReceiver, material);
		lighting += light;
	}
	return lighting;
}

#if defined(GGLAB_FORWARD_PLUS)
uint GetForwardPlusGlobalLightIndex(uint listIndex)
{
	return listIndex < 2u ? g_Pass.ForwardPlusGlobalLightIndices01[listIndex]
		: g_Pass.ForwardPlusGlobalLightIndices23[listIndex - 2u];
}

float3 EvaluateForwardPlusDirectLighting(float2 pixelPosition, float3 positionWS,
	ShadowReceiverPlane shadowReceiver, PreparedMaterialShading material)
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
			const float3 light = EvaluateDirectLight(lightIndex, positionWS, shadowReceiver, material);
			lighting += light;
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
		const float3 light = EvaluateDirectLight(lightIndex, positionWS, shadowReceiver, material);
		lighting += light;
	}
	return lighting;
}
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION) || defined(GGLAB_GTAO_CONTRIBUTION_OUTPUT) || defined(GGLAB_MATERIAL_DIAGNOSTICS)
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
	// Alpha mode / cutoff / discard stays pass-owned: resolve the surface's
	// raw sampled alpha through the material's alpha policy here.
	const float alpha = ResolveMaterialAlpha(matData, surface.Opacity);

	MaterialShadingInput shadingInput;
	shadingInput.PositionWS = IN.PositionWS;
	shadingInput.NormalWS = IN.NormalWS;
	shadingInput.TangentWS = IN.TangentWS;
	shadingInput.UV0 = IN.UV0;
	shadingInput.UV1 = IN.UV1;
	shadingInput.ViewPositionWS = viewData.CameraPos.xyz;
	shadingInput.IsFrontFace = isFrontFace;
	const MaterialShadingFrame frame = PrepareMaterialShadingFrame(matData, surface, shadingInput);

#if defined(GGLAB_MATERIAL_DIAGNOSTICS)
	// Scene extraction requests diagnostic MRTs for every parameter debug view.
	float3 diagnosticColor;
	const bool diagnostic = TryEvaluateMaterialDiagnostic(matData.DebugView, surface,
		frame.Base, frame.ClearcoatSpecularAA, frame.ClearcoatNormalWS, frame.Anisotropy, diagnosticColor);
#else
	const float3 diagnosticColor = 0.0.xxx;
	const bool diagnostic = false;
#endif

	// The pass owns texture access. Preparation consumes the actual LUT samples
	// once, and every light and environment response uses the same derived state.
	const float2 baseBrdfLUT = SampleIBLBrdfLUT(frame.NoV, frame.Base.EffectivePerceptualRoughness);
	ClearcoatShadingState coat = BuildClearcoatShadingState(surface, frame);
	if (coat.Factor > 0.0)
	{
		const float2 clearcoatBrdfLUT = SampleIBLBrdfLUT(coat.NoV, coat.PerceptualRoughness);
		ApplyClearcoatDirectionalEnergy(coat, clearcoatBrdfLUT);
	}
	const PreparedMaterialShading material = PrepareMaterialShading(surface, frame,
		baseBrdfLUT, coat);
#if defined(GGLAB_FORWARD_PLUS)
	const float3 directLighting = EvaluateForwardPlusDirectLighting(IN.PositionCS.xy,
		IN.PositionWS, shadowReceiver, material);
#else
	const float3 directLighting = EvaluateAllDirectLights(IN.PositionWS, shadowReceiver, material);
#endif

#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	const float3 allLightsDirectLighting = EvaluateAllDirectLights(IN.PositionWS, shadowReceiver, material);
#endif

	const float3 emissive = EvaluateMaterialEmission(surface.Emissive, material.Clearcoat);
	MaterialIBLResponse ibl = SampleMaterialEnvironment(material);

	// AO texture
	float2 occlusionUV = SelectUV(matData.OcclusionBinding, IN.UV0, IN.UV1);
	float aoSampled =
		SampleTextureBinding(matData.OcclusionBinding.TextureSamplerBinding, occlusionUV).r;
	float ao = 1.0f + matData.OcclusionStrength * (aoSampled - 1.0f);
	ao = saturate(ao);
	const float gtao = LoadGTAO(uint2(IN.PositionCS.xy));
	const float3 materialOccludedDiffuseIBL =
		ibl.Diffuse * ResolveSpecularIBLVisibility(ao);
	const float3 gtaoContribution = materialOccludedDiffuseIBL * (1.0 - gtao);
	ibl.Diffuse *= ResolveDiffuseIBLVisibility(ao, gtao);
	ibl.Specular *= ResolveSpecularIBLVisibility(ao);

	float3 outputLighting = directLighting;
	outputLighting += emissive;
	outputLighting += ibl.Diffuse + ibl.Specular;
	const float4 outputColor = float4(
		ApplyShadowDiagnosticsOverlay(outputLighting, IN.PositionWS), alpha);
#if defined(GGLAB_FORWARD_PLUS_VALIDATION)
	float3 allLightsOutputLighting = allLightsDirectLighting;
	allLightsOutputLighting += emissive;
	allLightsOutputLighting += ibl.Diffuse + ibl.Specular;
	const float4 allLightsReferenceColor = float4(
		ApplyShadowDiagnosticsOverlay(allLightsOutputLighting, IN.PositionWS), alpha);
	return MakeForwardPBRPixelOutput(outputColor, allLightsReferenceColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0), diagnosticColor, diagnostic);
#else
	return MakeForwardPBRPixelOutput(outputColor, outputColor,
		float4(SanitizeHDRColor(gtaoContribution), 1.0), diagnosticColor, diagnostic);
#endif
}
