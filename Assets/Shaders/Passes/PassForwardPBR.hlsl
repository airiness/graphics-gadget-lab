#include <Common/Common.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>
#include <PBR/BRDF.hlsli>

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD0;
};

struct VSOutput
{
	float4 PositionCS : SV_POSITION;
	float3 PositionWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV : TEXCOORD2;
	
	nointerpolation uint MaterialIndex : TEXCOORD3;
	nointerpolation uint ViewIndex : TEXCOORD4;
};

// Sample normal map and compute perturbed normal in world space
float3 SampleNormalWS(MaterialData matData, float3 normalWS, float3 positionWS, float2 uv)
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
	float3x3 TBN = BuildTBN(normalize(normalWS), positionWS, uv); // TODO: Support vertex tangent in VertexAttribute	
	
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

VSOutput VSMain(VSInput IN)
{
	VSOutput OUT;
	
	// Get object data
	const uint objectIndex = g_Scene.ObjectBaseIndex + g_ObjectIndex;
	ObjectData objData = g_Objects[objectIndex];
	
	// Get view data
	const uint viewIndex = g_Scene.ViewBaseIndex + g_ViewIndex;
	ViewData viewData = g_Views[viewIndex];
	
	// World space position and normal
	float4 posWS = mul(float4(IN.Position, 1.0), objData.ModelMat);
	float3 normalWS = normalize(mul(IN.Normal, (float3x3) objData.NormalMat));

	// View space position
	float4 posVS = mul(posWS, viewData.ViewMat);
	
	// Clip space position
	float4 posCS = mul(posVS, viewData.ProjMat);
	
	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS.xyz;
	OUT.NormalWS = normalWS;
	OUT.UV = IN.UV;
	
	// output material index
	const uint materialIndex = g_Scene.MaterialBaseIndex + objData.MaterialIndex;
	OUT.MaterialIndex = materialIndex;
	OUT.ViewIndex = viewIndex;
	
	return OUT;
}

float4 PSMain(VSOutput IN) : SV_Target
{
	MaterialData matData = g_Materials[IN.MaterialIndex];
	
	// Get view data
	ViewData viewData = g_Views[IN.ViewIndex];
	
	// BaseColor
	float4 baseColorSampled = SampleTextureBinding(matData.BaseColorBinding.TextureSamplerBinding, IN.UV);
	float3 baseColor = baseColorSampled.rgb * matData.BaseColorFactor.rgb;
	
	float alpha = baseColorSampled.a * matData.BaseColorFactor.a;
	// Alpha mode handling
	if (matData.AlphaMode == 0) // OPAQUE
	{
		alpha = 1.0;
	}
	else if (matData.AlphaMode == 1) // MASK
	{
		clip(alpha - matData.AlphaCutoff);
		alpha = 1.0;
	}
	
	// Mataliic and Roughness (linear, B=metallic, G=roughness)
	float4 mrSampled = SampleTextureBinding(matData.MetallicRoughnessBinding.TextureSamplerBinding, IN.UV);
	float metallic = saturate(matData.MetallicFactor * mrSampled.b);
	float perceptualRoughness = saturate(matData.RoughnessFactor * mrSampled.g);
	perceptualRoughness = ClampPerceptualRoughnessForBRDF(perceptualRoughness);

	// Normal (linear)
	float3 normalWS = normalize(IN.NormalWS);
	float3 N = SampleNormalWS(matData, normalWS, IN.PositionWS, IN.UV);
	
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
	
	float3 Lo = (diffuse + specular) *
		g_Scene.MainLight.Color.rgb *
		g_Scene.MainLight.Intensity * NoL;
	
	// Emissive texture(sRGB)
	float3 emissiveSampled = SampleTextureBinding(matData.EmissiveBinding.TextureSamplerBinding, IN.UV).rgb;
	float3 emissive = emissiveSampled * matData.EmissiveColorFactor.rgb;
	
	// Add emissive
	Lo += emissive;
	
	// LUT for IBL specular
	float2 brdfLUT = SampleIBLBrdfLUT(NoV, perceptualRoughness);
	float3 specularIBLFactor = F0 * brdfLUT.x + brdfLUT.y;
	
	// TODO: sample prefiltered environment map with roughness as mip level
	float3 fakePrefilteredEnv = 1.0.xxx;
	float3 specularIBL = fakePrefilteredEnv * specularIBLFactor;
	
	// TODO: temperary ambient
	float up = saturate(normalWS.y * 0.5f + 0.5f);
	float3 sky = float3(0.4, 0.5, 0.8);
	float3 ground = float3(0.05, 0.04, 0.03);
	float3 hemi = lerp(ground, sky, up);
	
	// AO texture
	float aoSampled = SampleTextureBinding(matData.OcclusionBinding.TextureSamplerBinding, IN.UV).r;
	float ao = 1.0f + matData.OcclusionStrength * (aoSampled - 1.0f);
	ao = saturate(ao);
	
	float3 ambient = hemi * baseColor * 0.2;
	Lo += ambient * ao;
	
	// TODO: Specular IBL
	Lo += specularIBL * 0.1;
	
	// Tonemap
	float3 color = ACESFitted(Lo * viewData.Exposure);
	color = LinearToSRGB(color);
	
	return float4(color, alpha);
}
