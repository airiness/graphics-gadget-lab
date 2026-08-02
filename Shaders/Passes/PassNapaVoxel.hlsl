#include <Common/Common.hlsli>
#include <Common/ApplicationBinding.hlsli>

struct NapaVoxelPassParameters
{
	uint ViewIndex;
	uint Material;
	uint2 Padding0;
	float3 ChunkTranslation;
	float Padding1;
};

ConstantBuffer<NapaVoxelPassParameters> g_Pass : register(b2);

struct VSInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
};

struct VSOutput
{
	float4 Position : SV_Position;
	float3 NormalWS : NORMAL;
};

VSOutput VSMain(VSInput input)
{
	VSOutput output;
	const uint viewIndex = g_Scene.ViewBaseIndex + g_Pass.ViewIndex;
	const ViewData viewData = g_Views[viewIndex];
	const float3 positionWS = input.Position + g_Pass.ChunkTranslation;
	output.Position = mul(mul(float4(positionWS, 1.0), viewData.ViewMat), viewData.ProjMat);
	output.NormalWS = input.Normal;
	return output;
}

float3 ResolveMaterialColor(uint material)
{
	static const uint SoilMaterial = 1u;
	static const uint StoneMaterial = 2u;
	if (material == SoilMaterial)
	{
		return float3(0.46, 0.24, 0.08);
	}
	if (material == StoneMaterial)
	{
		return float3(0.38, 0.42, 0.48);
	}
	return float3(1.0, 0.0, 1.0);
}

float4 PSMain(VSOutput input) : SV_Target
{
	const float3 normalWS = SafeNormalize(input.NormalWS, float3(0.0, 1.0, 0.0));
	const float3 toLight = SafeNormalize(float3(0.35, 0.85, 0.4), float3(0.0, 1.0, 0.0));
	const float diffuse = saturate(dot(normalWS, toLight));
	const float lighting = 0.22 + diffuse * 0.78;
	return float4(ResolveMaterialColor(g_Pass.Material) * lighting, 1.0);
}
