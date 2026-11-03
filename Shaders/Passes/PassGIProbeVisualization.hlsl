#include <Common/Common.hlsli>
#include <Lighting/GIFieldSampling.hlsli>
#include <Lighting/GIProbeDepth.hlsli>

cbuffer Parameters : register(b2)
{
	GIFieldSamplingParameters Field;
	row_major float4x4 ViewProjection;
	float3 Camera;
	float Radius;
	uint SliceAxis;
	uint LayerMinimum;
	uint LayerMaximum;
	uint Budget;
	float MaximumDistance;
	uint ClassificationMask;
	uint StatsIndex;
	uint IDsIndex;
	uint Mode;
	uint ReversedDepth;
	uint2 Extent;
	uint4 ObservationStamp;
	uint DisplayMode;
	float PreviewExposureScale;
	uint DepthMode;
	uint SceneDepthIndex;
	row_major float4x4 UnjitteredViewProjection;
};

uint3 Coordinate(uint id)
{
	return uint3(id % Field.Counts.x, id / (Field.Counts.x * Field.Counts.z),
		(id / Field.Counts.x) % Field.Counts.z);
}

bool IntersectsFrustum(float3 center)
{
	const float4 x = float4(UnjitteredViewProjection[0][0], UnjitteredViewProjection[1][0], UnjitteredViewProjection[2][0], UnjitteredViewProjection[3][0]);
	const float4 y = float4(UnjitteredViewProjection[0][1], UnjitteredViewProjection[1][1], UnjitteredViewProjection[2][1], UnjitteredViewProjection[3][1]);
	const float4 z = float4(UnjitteredViewProjection[0][2], UnjitteredViewProjection[1][2], UnjitteredViewProjection[2][2], UnjitteredViewProjection[3][2]);
	const float4 w = float4(UnjitteredViewProjection[0][3], UnjitteredViewProjection[1][3], UnjitteredViewProjection[2][3], UnjitteredViewProjection[3][3]);
	const float4 planes[6] = { w + x, w - x, w + y, w - y, z, w - z };
	[unroll]
	for (uint plane = 0; plane < 6; ++plane)
	{
		if (dot(float4(center, 1), planes[plane]) < -Radius * length(planes[plane].xyz))
		{
			return false;
		}
	}
	return true;
}

// A serial scan is bounded by the field's probe limit, and makes first-N order
// independent of subgroup size or atomic arrival order. No CPU readback feeds drawing.
[numthreads(1, 1, 1)]
void CSMain()
{
	RWTexture2D<uint> stats = ResourceDescriptorHeap[NonUniformResourceIndex(StatsIndex)];
	RWTexture2D<uint> ids = ResourceDescriptorHeap[NonUniformResourceIndex(IDsIndex)];
	Texture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(Field.Classification)];
	const uint count = Field.Counts.x * Field.Counts.y * Field.Counts.z;
	uint afterSlice = 0;
	uint afterClass = 0;
	uint afterDistance = 0;
	uint afterFrustum = 0;
	uint rendered = 0;
	[loop]
	for (uint index = 0; index < Budget; ++index)
	{
		ids[uint2(index, 0)] = 0xffffffffu;
		ids[uint2(index, 1)] = 0;
	}
	[loop]
	for (uint id = 0; id < count; ++id)
	{
		const uint3 coordinate = Coordinate(id);
		if (SliceAxis != 0 && (coordinate[SliceAxis - 1] < LayerMinimum || coordinate[SliceAxis - 1] > LayerMaximum))
		{
			continue;
		}
		++afterSlice;
		const uint raw = classification.Load(int4(coordinate.xzy, 0));
		const uint category = (classification.Load(int4(0, 0, 0, 0)) & GIProbeFieldInvalidBit) != 0 ?
			3 : raw <= 2 ? raw : 3;
		if ((ClassificationMask & (1u << category)) == 0)
		{
			continue;
		}
		++afterClass;
		const float3 center = Field.Minimum + (float3)(coordinate) * Field.Spacing;
		if (MaximumDistance > 0 && length(center - Camera) > MaximumDistance)
		{
			continue;
		}
		++afterDistance;
		if (!IntersectsFrustum(center))
		{
			continue;
		}
		++afterFrustum;
		if (rendered < Budget)
		{
			ids[uint2(rendered, 0)] = id;
			ids[uint2(rendered, 1)] = category;
			++rendered;
		}
	}
	stats[uint2(0, 0)] = 0x47505631u;
	stats[uint2(1, 0)] = 1;
	stats[uint2(2, 0)] = count;
	stats[uint2(3, 0)] = afterSlice;
	stats[uint2(4, 0)] = afterClass;
	stats[uint2(5, 0)] = afterDistance;
	stats[uint2(6, 0)] = afterFrustum;
	stats[uint2(7, 0)] = rendered;
	stats[uint2(8, 0)] = afterFrustum - rendered;
	stats[uint2(9, 0)] = 0;
	stats[uint2(10, 0)] = Budget;
	stats[uint2(11, 0)] = 0;
	stats[uint2(12, 0)] = ObservationStamp.x;
	stats[uint2(13, 0)] = ObservationStamp.y;
	stats[uint2(14, 0)] = ObservationStamp.z;
	stats[uint2(15, 0)] = ObservationStamp.w;
}

struct VertexOutput
{
	float4 Position : SV_Position;
	float3 Normal : TEXCOORD0;
	float2 UV : TEXCOORD1;
	nointerpolation uint Category : TEXCOORD2;
	nointerpolation uint ProbeID : TEXCOORD3;
	float3 WorldPosition : TEXCOORD4;
};

VertexOutput VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
	VertexOutput output = (VertexOutput)(0);
	output.Position = float4(2, 2, 0, 1);
	const float2 corners[6] = {
		float2(0, 0), float2(0, 1), float2(1, 1),
		float2(0, 0), float2(1, 1), float2(1, 0) };
	if (Mode == 1)
	{
		// Twelve screen-width edges around the probe-center volume bounds.
		const uint edge = vertex / 6;
		const uint axis = edge / 4;
		const uint side = edge % 4;
		float3 a = 0;
		a[(axis + 1) % 3] = (float)(side & 1);
		a[(axis + 2) % 3] = (float)(side >> 1);
		float3 b = a;
		b[axis] = 1;
		const float3 size = (float3)(Field.Counts - 1) * Field.Spacing;
		float4 ca = mul(float4(Field.Minimum + a * size, 1), ViewProjection);
		float4 cb = mul(float4(Field.Minimum + b * size, 1), ViewProjection);
		// Clip the center line before dividing by w or expanding its screen width.
		// A behind-eye endpoint must not discard the edge's visible portion.
		const float da = ReversedDepth != 0 ? ca.w - ca.z : ca.z;
		const float db = ReversedDepth != 0 ? cb.w - cb.z : cb.z;
		if (da < 0 && db < 0)
		{
			return output;
		}
		if (da < 0 || db < 0)
		{
			float4 clipped = lerp(ca, cb, da / (da - db));
			clipped.z = ReversedDepth != 0 ? clipped.w : 0;
			if (da < 0)
			{
				ca = clipped;
			}
			else
			{
				cb = clipped;
			}
		}
		if (ca.w <= 0 || cb.w <= 0)
		{
			return output;
		}
		const float2 local = corners[vertex % 6];
		const float2 delta = (cb.xy / cb.w - ca.xy / ca.w) * (float2)(Extent);
		const float2 perpendicular = float2(-delta.y, delta.x) / max(length(delta), 0.001);
		const float4 clip = lerp(ca, cb, local.x);
		output.Position = clip;
		output.Position.xy += perpendicular * (local.y * 2 - 1) / (float2)(Extent) * clip.w;
		output.Category = 4;
		return output;
	}
	Texture2D<uint> stats = ResourceDescriptorHeap[NonUniformResourceIndex(StatsIndex)];
	Texture2D<uint> ids = ResourceDescriptorHeap[NonUniformResourceIndex(IDsIndex)];
	if (instance >= stats.Load(int3(7, 0, 0)))
	{
		return output;
	}
	const uint2 record = uint2(ids.Load(int3(instance, 0, 0)), ids.Load(int3(instance, 1, 0)));
	const uint cell = vertex / 6;
	const float2 uv = (float2(cell % 16, cell / 16) + corners[vertex % 6]) / float2(16, 8);
	const float longitude = uv.x * 6.28318530718;
	const float latitude = uv.y * 3.14159265359;
	const float3 normal = float3(sin(latitude) * cos(longitude), cos(latitude), sin(latitude) * sin(longitude));
	const float3 center = Field.Minimum + (float3)(Coordinate(record.x)) * Field.Spacing;
	output.WorldPosition = center + normal * Radius;
	output.Position = mul(float4(output.WorldPosition, 1), ViewProjection);
	output.ProbeID = record.x;
	output.Normal = normal;
	output.UV = uv;
	output.Category = record.y;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
	// Exclude clear-depth endpoints; strict D32 testing resolves nearest, then
	// ascending stable ID for exactly equal comparable depths.
	if (!GIProbeDepthAdmitted(input.Position.z, ReversedDepth != 0))
	{
		discard;
	}
	if (DepthMode == 1)
	{
		Texture2D<float> scene = ResourceDescriptorHeap[NonUniformResourceIndex(SceneDepthIndex)];
		if (!GIProbeSceneDepthVisible(input.Position.z, scene.Load(int3((uint2)(input.Position.xy), 0)), ReversedDepth != 0))
		{
			discard;
		}
	}
	if (input.Category == 4)
	{
		return float4(0.55, 0.8, 1.0, 1);
	}
	const float2 grid = abs(sin(input.UV * float2(8, 4) * 3.14159265359));
	// Cap the angular line width: derivative expansion must not fill a small
	// Inactive sphere and erase the shape distinction from an Active solid.
	const float2 wireWidth = clamp(fwidth(input.UV) * float2(8, 4) * 2, 0.015, 0.16);
	if (input.Category == 2 && all(grid > wireWidth))
	{
		discard;
	}
	if (input.Category == 0 && grid.y > 0.3)
	{
		discard;
	}
	if (DisplayMode == 1 && input.Category == 1)
	{
		const float3 normal = normalize(input.Normal);
		Texture2DArray<float4> atlas = ResourceDescriptorHeap[NonUniformResourceIndex(Field.Irradiance)];
		const float3 response = GIFieldIrradiance(atlas, Coordinate(input.ProbeID), normal, Field.IrradianceInteriorWidth);
		if (!all(isfinite(NonFiniteObserveValue(NF_SITE_GI_PROBE_VISUALIZATION_PSMAIN_ISFINITE, response))) || any(response < 0))
		{
			return float4(1, 0, 1, 1);
		}
		// Atlas M = E/pi is already the unit-albedo Lambertian response. It has
		// no camera exposure or pre-exposure. Keep a neutral silhouette for valid black.
		const float facing = abs(dot(normal, normalize(Camera - input.WorldPosition)));
		if (facing <= max(fwidth(facing), 0.025))
		{
			return float4(0.35, 0.35, 0.35, 1);
		}
		return float4(LinearToSRGB(ACESFitted(response * PreviewExposureScale,
			NF_SITE_GI_PROBE_VISUALIZATION_PSMAIN_ACESFITTED)), 1);
	}
	const float light = 0.45 + 0.55 * saturate(dot(normalize(input.Normal), normalize(float3(-1, 2, -3))));
	const float3 color = input.Category == 1 ? float3(0.08, 0.8, 0.5) * light :
		input.Category == 2 ? float3(0.15, 0.22, 0.3) :
		input.Category == 0 ? float3(1.0, 0.65, 0.05) : float3(1, 0, 1);
	return float4(color, 1);
}
