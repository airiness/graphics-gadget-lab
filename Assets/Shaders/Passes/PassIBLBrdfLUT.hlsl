#include <Common/Common.hlsli>
#include <PBR/BRDF.hlsli>

struct VSOutput
{
	float4 Position : SV_POSITION;
	float2 UV : TEXCOORD0;
};

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

VSOutput VSMain(uint vid : SV_VertexID)
{
	// Vertex data position and UV coordinates for a full-screen triangle
	float2 pos[3] =
	{
		float2(-1.0, -1.0),
		float2(-1.0, 3.0),
		float2(3.0, -1.0)
	};
	

	// D3D-style fullscreen triangle UV:
    // top-left    : (0, 0)
    // bottom-left : (0, 1)
    // bottom-right: (1, 1)
	// 
	// NDC +y ↑
	// Render target(texture) +y ↓
	// NDC (-1,  1) -> UV (0, 0) left top
	// NDC ( 1,  1) -> UV (1, 0) right top
	// NDC (-1, -1) -> UV (0, 1) left bottom
	// NDC ( 1, -1) -> UV (1, 1) right bottom
	//
	// U = (x + 1) / 2
	// V = (1 - y) / 2
	float2 uv[3] =
	{
		float2(0.0, 1.0),
		float2(0.0, -1.0),
		float2(2.0, 1.0)
	};
		
	VSOutput OUT;
	OUT.Position = float4(pos[vid], 0.0, 1.0);
	OUT.UV = uv[vid];
	
	return OUT;
	
	// Can write as this:
	//// D3D NDC -> texture UV
	//OUT.UV = float2((pos[vid].x + 1.0) * 0.5, (1.0 - pos[vid].y) * 0.5);
}

float4 PSMain(VSOutput IN) : SV_Target0
{
	float NoV = saturate(IN.UV.x);
	float roughness = saturate(IN.UV.y);
	
	float2 brdf = IntegrateBRDF(NoV, roughness);
	
	return float4(brdf, 0.0, 1.0);
}