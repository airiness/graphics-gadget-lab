#include <Common/Common.hlsli>
#include <Common/BufferLayout.hlsli>
#include <Common/Binding.hlsli>
#include <PBR/BRDF.hlsli>

// texture
Texture2D g_BaseColorTex : register(t0);

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
	
	nointerpolation float4 BaseColorFactor : TEXCOORD3;
	nointerpolation float2 MetallicRoughnessFactor : TEXCOORD4;
	nointerpolation float3 EmissiveColorFactor : TEXCOORD5;
	nointerpolation uint MaterialIndex : TEXCOORD6;
};

VSOutput VSMain(VSInput IN)
{
	VSOutput OUT;
	
	const uint objectIndex = g_Frame.ObjectBaseIndex + g_ObjectIndex;
	ObjectData objData = g_Objects[objectIndex];
	
	float4 posWS = mul(float4(IN.Position, 1.0), objData.ModelMat);
	float3 normalWS = normalize(mul(IN.Normal, (float3x3) objData.NormalMat));
	
	float4 posVS = mul(posWS, g_Frame.ViewMat);
	float4 posCS = mul(posVS, g_Frame.ProjMat);
	
	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS.xyz;
	OUT.NormalWS = normalWS;
	OUT.UV = IN.UV;
	
	uint materialIndex = objData.MaterialIndex;
	//const uint materialIndex = g_Frame.MaterialBaseIndex + g_MaterialIndex;
	
	MaterialData matData = g_Materials[materialIndex];
	
	OUT.BaseColorFactor = matData.BaseColorFactor;
	OUT.MetallicRoughnessFactor = float2(matData.MetallicFactor, max(0.045, matData.RoughnessFactor));
	OUT.EmissiveColorFactor = matData.EmissiveColorFactor.rgb;
	OUT.MaterialIndex = objData.MaterialIndex;

	return OUT;
}

float4 PSMain(VSOutput IN) : SV_Target
{	
	float3 N = normalize(IN.NormalWS);
	float3 V = normalize(g_Frame.CameraPosWS.xyz - IN.PositionWS); // View direction
	float3 L = normalize(-g_Frame.MainLight.DirWS.xyz); // DirWS is light to surface, so L = -DirWS
	
	float NoL = saturate(dot(N, L));
	float NoV = saturate(dot(N, V));
	float3 H = normalize(L + V); // Half vector
	float NoH = saturate(dot(N, H));
	float VoH = saturate(dot(V, H));
	
	float3 baseColorTex = g_BaseColorTex.Sample(g_SamplerLinear, IN.UV).rgb;
	float3 baseColor = SRGBToLinear(baseColorTex) * IN.BaseColorFactor.rgb; // need SRGB to Linear?
	
	float roughness = saturate(IN.MetallicRoughnessFactor.y);
	float metallic = saturate(IN.MetallicRoughnessFactor.x);

	float a = roughness * roughness; // convert artistic roughness to physical roughness
	
	float3 F0 = lerp(0.04.xxx, baseColor, metallic); // dielectric F0 is 0.04, metal F0 is baseColor	
	float D = D_GGX(NoH, a);
	float Vis = V_SmithGGXCorrelated(NoV, NoL, a);
	float3 F = F_Schlick(F0, 1.0.xxx, VoH); // use F90 = 1.0, TODO: use Fresnel reflectance at grazing angle
	
	float3 specular = D * Vis * F;
	
	float3 kd = (1.0.xxx - F) * (1.0 - metallic); // energy rest after specular and used for diffuse
	float3 diffuse = kd * Fd_Lambert(baseColor);
	
	float3 Lo = (diffuse + specular) * g_Frame.MainLight.Color.rgb * g_Frame.MainLight.Intensity * NoL;
	
	Lo += IN.EmissiveColorFactor;
	
	float3 color = ACESFitted(Lo * g_Frame.Exposure);
	color = LinearToSRGB(color);
	
	return float4(color, IN.BaseColorFactor.a);
}