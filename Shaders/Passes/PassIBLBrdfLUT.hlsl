#include <Common/Common.hlsli>
#include <Common/Sampling.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <PBR/BRDF.hlsli>

float2 IntegrateBRDF(float NoV, float perceptualRoughness)
{
	NoV = max(NoV, 1e-5);
	
	float a = PerceptualRoughnessToAlpha(perceptualRoughness);
	
	// N = (0, 0, 1)
	float3 V = float3(sqrt(max(0.0, 1.0 - NoV * NoV)), 0.0, NoV);
	
	float A = 0.0;
	float B = 0.0;

	const uint SAMPLE_COUNT = 1024;
	
	for (uint i = 0; i < SAMPLE_COUNT; ++i)
	{
		float2 Xi = Hammersley(i, SAMPLE_COUNT);

		float3 H = ImportanceSampleGGX(Xi, a);
		float3 L = normalize(2.0 * dot(V, H) * H - V);

		float NoL = saturate(L.z);
		float NoH = saturate(H.z);
		float VoH = saturate(dot(V, H));

		if (NoL > 0.0)
		{
			// V is the correlated Smith visibility term:
			// V = G / (4 * NoV * NoL)
			float Vis = V_SmithGGXCorrelated(NoV, NoL, a);

			// Convert visibility-form BRDF back into the split-sum integration weight.
			// Equivalent to: G * VoH / (NoH * NoV)
			float G_Vis = 4.0 * Vis * NoL * VoH / max(1e-5, NoH);

			float Fc = Pow5(1.0 - VoH);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	return float2(A, B) / SAMPLE_COUNT;
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	float NoV = saturate(IN.UV.x);
	float roughness = saturate(IN.UV.y);
	
	float2 brdf = IntegrateBRDF(NoV, roughness);
	
	return float4(brdf, 0.0, 1.0);
}
