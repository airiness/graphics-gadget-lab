RaytracingAccelerationStructure Scene : register(t0);

struct HitResult
{
	uint Hit;
	uint Instance;
	uint Primitive;
	uint Reserved;
	float2 Barycentrics;
	float Distance;
	float Padding;
};

RWStructuredBuffer<HitResult> Results : register(u0);

cbuffer Parameters : register(b0)
{
	uint TextureIndex;
	uint Width;
	uint Height;
	uint Reserved;
};

HitResult Trace(float3 origin, float3 direction, uint mask)
{
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = direction;
	ray.TMin = 0.001f;
	ray.TMax = 100.0f;
	RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
	query.TraceRayInline(Scene, RAY_FLAG_NONE, mask, ray);
	while (query.Proceed())
	{
	}
	HitResult result = (HitResult)(0);
	if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
	{
		result.Hit = 1;
		result.Instance = query.CommittedInstanceID();
		result.Primitive = query.CommittedPrimitiveIndex();
		result.Barycentrics = query.CommittedTriangleBarycentrics();
		result.Distance = query.CommittedRayT();
	}
	return result;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Width || id.y >= Height)
	{
		return;
	}
	if (id.y == 0 && id.x < 8)
	{
		static const float2 Origins[8] = {
			float2(-0.75f, 0.0f), float2(0.75f, 0.0f), float2(0.0f, 0.9f),
			float2(-0.75f, 0.9f), float2(3.0f, 3.0f), float2(-0.75f, 0.0f),
			float2(0.75f, 0.0f), float2(-0.75f, 0.0f)
		};
		uint mask = id.x == 5 ? 2 : (id.x == 6 ? 1 : 255);
		Results[id.x] = Trace(float3(Origins[id.x], -2.0f),
			float3(0.0f, 0.0f, id.x == 7 ? -1.0f : 1.0f), mask);
	}
	float2 uv = ((float2)(id.xy) + 0.5f) / float2(Width, Height);
	float2 position = (uv * 2.0f - 1.0f) * float2(1.5f, -1.2f) + float2(0.0f, 0.4f);
	HitResult result = Trace(float3(position, -2.0f), float3(0.0f, 0.0f, 1.0f), 255);
	RWTexture2D<float4> output = ResourceDescriptorHeap[TextureIndex];
	float3 color = result.Hit != 0 ?
		float3(1.0f - result.Barycentrics.x - result.Barycentrics.y,
			result.Barycentrics.x, result.Barycentrics.y) : float3(0.015f, 0.02f, 0.035f);
	output[id.xy] = float4(color, 1.0f);
}

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 UV : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
	VertexOutput output;
	output.UV = float2((id << 1) & 2, id & 2);
	output.Position = float4(output.UV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	Texture2D<float4> image = ResourceDescriptorHeap[TextureIndex];
	uint2 pixel = min((uint2)(input.UV * float2(Width, Height)), uint2(Width - 1, Height - 1));
	return image.Load(int3(pixel, 0));
}
