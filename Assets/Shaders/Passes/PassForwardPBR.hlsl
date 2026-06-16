#include <Common/Common.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/MaterialUtils.hlsli>
#include <Common/VertexTransform.hlsli>
#include <Lighting/ShadowSampling.hlsli>
#include <PBR/BRDF.hlsli>

uint GetShadowMapTextureIndex()
{
	return g_LocalParam2;
}

uint GetShadowMapSamplerIndex()
{
	return g_LocalParam3;
}

uint GetShadowMapSize()
{
	return g_LocalParam4;
}

bool IsShadowEnabled()
{
	return (g_LocalParam5 & 1u) != 0u;
}

bool IsShadowPCFEnabled()
{
	return (g_LocalParam5 & 2u) != 0u;
}

float GetShadowDepthBias()
{
	return asfloat(g_LocalParam6);
}

uint GetShadowViewIndex()
{
	return g_LocalParam7;
}

struct VSOutput
{
	float4 PositionCS : SV_POSITION;
	float3 PositionWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV0 : TEXCOORD2;
	float2 UV1 : TEXCOORD3;
	float4 TangentWS : TEXCOORD4;

	nointerpolation uint MaterialIndex : TEXCOORD5;
	nointerpolation uint ViewIndex : TEXCOORD6;
};

// Sample normal map and compute perturbed normal in world space
float3 SampleNormalWS(MaterialData matData, float3 normalWS, float4 tangentWS, float3 positionWS, float2 uv)
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
	float3x3 TBN = BuildTBNFromTangent(normalize(normalWS), tangentWS, positionWS, uv);

	// Transform normal from tangent space to world space
	float3 perturbedNormalWS = normalize(mul(normalSampled.xyz, TBN));
	return perturbedNormalWS;
}

float2 SampleIBLBrdfLUT(float NoV, float perceptualRoughness)
{
	float4 value = SampleTextureBindingLevel(
		MakeTextureSamplerBinding(g_Scene.IBLResource.BrdfLutBinding),
		float2(saturate(NoV), saturate(perceptualRoughness)),
		0);

	return value.rg;
}

float3 SampleIBLIrradiance(float3 normalWS)
{
	TextureSamplerBindingData binding = MakeTextureSamplerBinding(g_Scene.IBLResource.IrradianceBinding);
	return SampleTextureCube(binding, normalize(normalWS)).rgb * g_Scene.IBLResource.EnvironmentIntensity;
}

float3 SampleIBLPrefilteredSpecular(float3 reflectWS, float perceptualRoughness)
{
	TextureSamplerBindingData binding = MakeTextureSamplerBinding(g_Scene.IBLResource.PrefilteredSpecularBinding);
	const uint mipLevels = max(g_Scene.IBLResource.PrefilteredSpecularMipLevels, 1u);
	const float maxMipLevel = (float) (mipLevels - 1u);
	const float lod = saturate(perceptualRoughness) * maxMipLevel;
	return SampleTextureCubeLevel(binding, normalize(reflectWS), lod).rgb * g_Scene.IBLResource.EnvironmentIntensity;
}

float SampleDirectionalShadow(float3 positionWS, float NoL)
{
	if (!IsShadowEnabled() || NoL <= 0.0)
	{
		return 1.0;
	}

	const ShadowProjection shadowProjection = ProjectToShadowMap(positionWS, GetShadowViewIndex());
	if (!shadowProjection.IsValid)
	{
		return 1.0;
	}

	Texture2D<float> shadowMap = GetTexture2DFloat(GetShadowMapTextureIndex());
	SamplerComparisonState shadowSampler = GetSamplerComparisonState(GetShadowMapSamplerIndex());

	const float compareDepth = saturate(shadowProjection.ReceiverDepth - GetShadowDepthBias());

	if (!IsShadowPCFEnabled())
	{
		return SampleShadowHard(shadowMap, shadowSampler, shadowProjection.UV, compareDepth);
	}

	const float shadowMapSize = max((float) GetShadowMapSize(), 1.0);
	const float2 shadowTexelSize = 1.0.xx / shadowMapSize;
	return SampleShadowPCF3x3(shadowMap, shadowSampler, shadowProjection.UV, compareDepth, shadowTexelSize);
}

VSOutput VSMain(VertexInputP3N3T2T2Tan4 IN)
{
	VSOutput OUT;

	const ObjectData objData = LoadCurrentObjectData();
	const ViewData viewData = LoadCurrentViewData();

	const float4 posWS = TransformPositionWS(IN.Position, objData);
	const float4 posVS = TransformPositionVS(posWS, viewData);
	const float4 posCS = TransformPositionCS(posVS, viewData);
	const float3 normalWS = TransformNormalWS(IN.Normal, objData);
	const float4 tangentWS = TransformTangentWS(IN.Tangent, objData);

	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS.xyz;
	OUT.NormalWS = normalWS;
	OUT.UV0 = IN.UV0;
	OUT.UV1 = IN.UV1;
	OUT.TangentWS = tangentWS;

	// output material index
	const uint materialIndex = g_Scene.MaterialBaseIndex + objData.MaterialIndex;
	OUT.MaterialIndex = materialIndex;
	OUT.ViewIndex = GetCurrentViewDataIndex();

	return OUT;
}

float4 PSMain(VSOutput IN, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
	MaterialData matData = g_Materials[IN.MaterialIndex];

	// Get view data
	ViewData viewData = g_Views[IN.ViewIndex];

	// BaseColor
	const float2 baseColorUV = SelectUV(matData.BaseColorBinding, IN.UV0, IN.UV1);
	float4 baseColorSampled = SampleTextureBinding(matData.BaseColorBinding.TextureSamplerBinding, baseColorUV);
	float3 baseColor = baseColorSampled.rgb * matData.BaseColorFactor.rgb;

	float alpha = ResolveMaterialAlpha(
		matData,
		baseColorSampled.a * matData.BaseColorFactor.a);

	// Mataliic and Roughness (linear, B=metallic, G=roughness)
	float2 metallicRoughnessUV = SelectUV(matData.MetallicRoughnessBinding, IN.UV0, IN.UV1);
	float4 mrSampled = SampleTextureBinding(matData.MetallicRoughnessBinding.TextureSamplerBinding, metallicRoughnessUV);
	float metallic = saturate(matData.MetallicFactor * mrSampled.b);
	float perceptualRoughness = saturate(matData.RoughnessFactor * mrSampled.g);
	perceptualRoughness = ClampPerceptualRoughnessForBRDF(perceptualRoughness);

	// Normal (linear)
	float3 normalWS = normalize(IN.NormalWS);
	float4 tangentWS = IN.TangentWS;
	if ((matData.Flags & 1u) != 0u && !isFrontFace)
	{
		normalWS = -normalWS;
	}
	float2 normalUV = SelectUV(matData.NormalBinding, IN.UV0, IN.UV1);
	float3 N = SampleNormalWS(matData, normalWS, tangentWS, IN.PositionWS, normalUV);

	// Shading
	float3 V = normalize(viewData.CameraPos.xyz - IN.PositionWS); // View direction
	float3 L = normalize(-g_Scene.MainLight.Direction.xyz); // DirWS is light to surface, so L = -DirWS

	float NoL = saturate(dot(N, L));
	float NoV = saturate(dot(N, V));
	float3 H = normalize(L + V); // Half vector
	float NoH = saturate(dot(N, H));
	float VoH = saturate(dot(V, H));

	// convert artistic roughness to physical roughness
	float a = PerceptualRoughnessToAlpha(perceptualRoughness);

	float3 F0 = lerp(0.04.xxx, baseColor, metallic); // dielectric F0 is 0.04, metal F0 is baseColor
	float D = D_GGX(NoH, a);
	float Vis = V_SmithGGXCorrelated(NoV, NoL, a);
	float3 F = F_Schlick(F0, 1.0.xxx, VoH); // use F90 = 1.0, TODO: use Fresnel reflectance at grazing angle

	float3 specular = D * Vis * F;

	float3 kd = (1.0.xxx - F) * (1.0 - metallic); // energy rest after specular and used for diffuse
	float3 diffuse = kd * Fd_Lambert(baseColor);

	float shadowVisibility = SampleDirectionalShadow(IN.PositionWS, NoL);

	float3 Lo = (diffuse + specular) *
		g_Scene.MainLight.Color.rgb *
		g_Scene.MainLight.Intensity * NoL * shadowVisibility;

	// Emissive texture(sRGB)
	float2 emissiveUV = SelectUV(matData.EmissiveBinding, IN.UV0, IN.UV1);
	float3 emissiveSampled = SampleTextureBinding(matData.EmissiveBinding.TextureSamplerBinding, emissiveUV).rgb;
	float3 emissive = emissiveSampled * matData.EmissiveColorFactor.rgb;

	// Add emissive
	Lo += emissive;

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
	float aoSampled = SampleTextureBinding(matData.OcclusionBinding.TextureSamplerBinding, occlusionUV).r;
	float ao = 1.0f + matData.OcclusionStrength * (aoSampled - 1.0f);
	ao = saturate(ao);

	Lo += (diffuseIBL + specularIBL) * ao;

	return float4(Lo, alpha);
}
