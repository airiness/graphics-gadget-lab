#include <Common/CBLayout.hlsli>
#include <Common/Common.hlsli>
#include <PBR/BRDF.hlsli>

cbuffer GlobalCB : register(b0)
{
	GlobalCBData g_Global;
};

cbuffer ObjectCB : register(b1)
{
	ObjectCBData g_Object;
};

cbuffer MaterialCB : register(b2)
{
	MaterialCBData g_Material;
};

// texture and sampler
Texture2D g_BaseColorTex : register(t0);
SamplerState g_Sampler : register(s0);

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 UV : TEXCOORD;
};

struct VSOutput
{
	float4 PositionCS : SV_POSITION;
	float3 PositionWS : TEXCOORD0;
	float3 NormalWS : TEXCOORD1;
	float2 UV : TEXCOORD2;
	float3 ViewDirWS : TEXCOORD3;
};

VSOutput VSMain(VSInput IN)
{
	VSOutput OUT;
	
	float4 posWS = mul(float4(IN.Position, 1.0), g_Object.ModelMat);
	float3 normalWS = normalize(mul(IN.Normal, (float3x3) g_Object.NormalMat));
	
	float4 posVS = mul(posWS, g_Global.ViewMat);
	float4 posCS = mul(posVS, g_Global.ProjMat);
	
	OUT.PositionCS = posCS;
	OUT.PositionWS = posWS.xyz;
	OUT.NormalWS = normalWS;
	OUT.UV = IN.UV;
	OUT.ViewDirWS = g_Global.CameraPosWS.xyz - posWS.xyz;

	return OUT;
}

float4 PSMain(VSOutput IN) : SV_Target
{
	float3 N = normalize(IN.NormalWS);
	float3 V = normalize(IN.ViewDirWS); // View direction
	float3 L = normalize(-g_Global.MainLight.DirWS.xyz); // DirWS is light to surface, so L = -DirWS
	
	float NoL = saturate(dot(N, L));
	float NoV = saturate(dot(N, V));
	
	float3 baseColorTex = g_BaseColorTex.Sample(g_Sampler, IN.UV).rgb;
	float3 baseColor = SRGBToLinear(baseColorTex) * g_Material.BaseColorFactor.rgb; // need SRGB to Linear?
	float metallic = saturate(g_Material.MetallicFactor);
	float roughness = saturate(g_Material.RoughnessFactor);
	roughness = max(roughness, 0.045); // avoid 0 roughness
	float a = roughness * roughness; // convert artistic roughness to physical roughness
	
	float3 F0 = lerp(0.04.xxx, baseColor, metallic); // dielectric F0 is 0.04, metal F0 is baseColor
	
	float3 H = normalize(L + V); // Half vector
	float NoH = saturate(dot(N, H));
	float VoH = saturate(dot(V, H));
	
	float D = D_GGX(NoH, a);
	float Vis = V_SmithGGXCorrelated(NoV, NoL, a);
	float3 F = F_Schlick(F0, 1.0.xxx, VoH); // use F90 = 1.0, TODO: use Fresnel reflectance at grazing angle
	
	float3 specular = D * Vis * F;
	
	float3 kd = (1.0.xxx - F) * (1.0 - metallic); // energy rest after specular and used for diffuse
	float3 diffuse = kd * Fd_Lambert(baseColor);
	
	float3 Lo = (diffuse + specular) * g_Global.MainLight.Color.rgb * g_Global.MainLight.Intensity * NoL;
	
	Lo += g_Material.EmissiveColorFactor.rgb;
	
	float3 color = ACESFitted(Lo * g_Global.Exposure);
	color = LinearToSRGB(color);
	
	return float4(color, g_Material.BaseColorFactor.a);
}