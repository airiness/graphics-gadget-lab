#include <Common/Common.hlsli>
#include <Common/Binding.hlsli>	// Included BuffurLayout.hlsli in Binding.hlsli
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
};

VSOutput VSMain(VSInput IN)
{
	VSOutput OUT;
	
	// Get object data
	const uint objectIndex = g_Frame.ObjectBaseIndex + g_ObjectIndex;
	ObjectData objData = g_Objects[objectIndex];
	
	// World space position and normal
	float4 posWS = mul(float4(IN.Position, 1.0), objData.ModelMat);
	float3 normalWS = normalize(mul(IN.Normal, (float3x3) objData.NormalMat));

	// View space position
	float4 posVS = mul(posWS, g_Frame.ViewMat);
	
	// Clip space position
	float4 posCS = mul(posVS, g_Frame.ProjMat);
	
	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS.xyz;
	OUT.NormalWS = normalWS;
	OUT.UV = IN.UV;
	
	// output material index
	const uint materialIndex = g_Frame.MaterialBaseIndex + objData.MaterialIndex;
	OUT.MaterialIndex = materialIndex;
	
	return OUT;
}

// Sample normal map and compute perturbed normal in world space
float3 SampleNormalWS(MaterialData matData, float3 normalWS, float3 positionWS, float2 uv)
{
	// TODO: flip Y for normal map?
	
	// Sample normal texture
	float4 normalSampled = SampleTexture2D(matData.NormalTexIndex, g_SamplerLinear, uv);
	
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

float4 PSMain(VSOutput IN) : SV_Target
{
	MaterialData matData = g_Materials[IN.MaterialIndex];
	
	// BaseColor
	float4 baseColorSampled = SampleTexture2D(matData.BaseColorTexIndex, g_SamplerLinear, IN.UV);
	float3 baseColor = SRGBToLinear(baseColorSampled.rgb) * matData.BaseColorFactor.rgb; // TODO: sample sRGB without sRGB->Linear conversion
	float alpha = baseColorSampled.a * matData.BaseColorFactor.a; // TODO: alpha mode handling
	
	// Mataliic and Roughness (linear, B=metallic, G=roughness)
	float4 mrSampled = SampleTexture2D(matData.MetallicRoughnessTexIndex, g_SamplerLinear, IN.UV);
	float metallic = saturate(matData.MetallicFactor * mrSampled.b);
	float roughness = saturate(matData.RoughnessFactor * mrSampled.g);
	roughness = max(0.045, roughness); // to avoid 0 roughness)

	// Normal (linear)
	float3 normalWS = normalize(IN.NormalWS);	
	float3 N = SampleNormalWS(matData, normalWS, IN.PositionWS, IN.UV);
	
	// Shading
	float3 V = normalize(g_Frame.CameraPos.xyz - IN.PositionWS); // View direction
	float3 L = normalize(-g_Frame.MainLight.Direction.xyz); // DirWS is light to surface, so L = -DirWS
	
	float NoL = saturate(dot(N, L));
	float NoV = saturate(dot(N, V));
	float3 H = normalize(L + V); // Half vector
	float NoH = saturate(dot(N, H));
	float VoH = saturate(dot(V, H));
	
	float a = roughness * roughness; // convert artistic roughness to physical roughness
		
	float3 F0 = lerp(0.04.xxx, baseColor, metallic); // dielectric F0 is 0.04, metal F0 is baseColor	
	float D = D_GGX(NoH, a);
	float Vis = V_SmithGGXCorrelated(NoV, NoL, a);
	float3 F = F_Schlick(F0, 1.0.xxx, VoH); // use F90 = 1.0, TODO: use Fresnel reflectance at grazing angle
	
	float3 specular = D * Vis * F;
	
	float3 kd = (1.0.xxx - F) * (1.0 - metallic); // energy rest after specular and used for diffuse
	float3 diffuse = kd * Fd_Lambert(baseColor);
	
	float3 Lo = (diffuse + specular) *
		g_Frame.MainLight.Color.rgb *
		g_Frame.MainLight.Intensity * NoL;
	
	// Emissive texture(sRGB)
	float3 emissiveSampled = SampleTexture2D(matData.EmissiveTexIndex, g_SamplerLinear, IN.UV).rgb;
	emissiveSampled = SRGBToLinear(emissiveSampled); // TODO: sample sRGB without sRGB->Linear conversion	
	float3 emissive = emissiveSampled * matData.EmissiveColorFactor.rgb;
	
	// Add emissive
	Lo += emissive;
	
	// TODO: temperary ambient
	float up = saturate(normalWS.y * 0.5f + 0.5f);
	float3 sky = float3(0.4, 0.5, 0.8);
	float3 ground = float3(0.05, 0.04, 0.03);
	float3 hemi = lerp(ground, sky, up);
	
	// AO texture
	float aoSampled = SampleTexture2D(matData.OcclusionTexIndex, g_SamplerLinear, IN.UV).r;
	float ao = 1.0f + matData.OcclusionStrength * (aoSampled - 1.0f);
	ao = saturate(ao);
	
	float3 ambient = hemi * baseColor * 0.2;
	Lo += ambient * ao;
	
	// Tonemap
	float3 color = ACESFitted(Lo * g_Frame.Exposure);
	color = LinearToSRGB(color);
	
	return float4(color, alpha);
}