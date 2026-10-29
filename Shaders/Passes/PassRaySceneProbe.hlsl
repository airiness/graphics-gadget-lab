RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<uint4> Geometries : register(t1);
StructuredBuffer<uint4> Instances : register(t2);

struct HitResult
{
	uint Hit;
	uint Instance;
	uint Primitive;
	uint Errors;
	float2 Barycentrics;
	float Distance;
	float Reserved;
};
RWStructuredBuffer<HitResult> Results : register(u0);

cbuffer Parameters : register(b0)
{
	uint Width;
	uint InstanceCount;
	uint GeometryCount;
	uint Reserved;
};

// A fixed world-space downward grid checks scene membership independently of the camera.
// Lighting and the RT sun-visibility comparison belong to the next integration slice.
[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= Width || id.y >= Width)
	{
		return;
	}
	float2 position = (((float2)(id.xy) + 0.5f) / (float)(Width)) * 32.0f - 16.0f;
	RayDesc ray;
	ray.Origin = float3(position.x, 20.0f, position.y);
	ray.Direction = float3(0.0f, -1.0f, 0.0f);
	ray.TMin = 0.001f;
	ray.TMax = 100.0f;
	RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
	query.TraceRayInline(Scene, RAY_FLAG_NONE, 1, ray);
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
		if (result.Instance >= InstanceCount)
		{
			result.Errors = 1;
		}
		else
		{
			uint4 instance = Instances[result.Instance];
			if (instance.z != 1 || instance.x >= GeometryCount)
			{
				result.Errors = 2;
			}
			else if (result.Primitive >= Geometries[instance.x].z)
			{
				result.Errors = 4;
			}
		}
	}
	Results[id.y * Width + id.x] = result;
}
