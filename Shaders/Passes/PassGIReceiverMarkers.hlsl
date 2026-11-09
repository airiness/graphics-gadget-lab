#include <Common/Common.hlsli>

cbuffer Parameters : register(b2)
{
	row_major float4x4 ViewProjection;
	uint RecordsIndex;
	uint2 Extent;
	uint Padding;
};

uint4 Record(uint index)
{
	Texture2D<uint> records = ResourceDescriptorHeap[NonUniformResourceIndex(RecordsIndex)];
	return uint4(records.Load(int3(index * 4, 0, 0)), records.Load(int3(index * 4 + 1, 0, 0)),
		records.Load(int3(index * 4 + 2, 0, 0)), records.Load(int3(index * 4 + 3, 0, 0)));
}

struct VertexOutput
{
	float4 Position : SV_Position;
	float4 Color : COLOR;
	float2 Local : TEXCOORD0;
	nointerpolation uint Marker : TEXCOORD1;
};

float4 CornerColor(uint corner)
{
	const uint first = 9 + corner * 8;
	const uint reason = Record(first + 1).w;
	const float normalized = asfloat(Record(first + 5).w);
	return reason == 1 ? float4(1.0, 0.05, 0.05, 1.0) :
		reason == 2 || reason == 3 ? float4(0.15, 0.2, 0.3, 1.0) :
		float4(lerp(float3(0.05, 0.25, 1.0), float3(1.0, 0.8, 0.05), sqrt(saturate(normalized))), 1.0);
}

VertexOutput VSMain(uint vertex : SV_VertexID)
{
	const float2 quad[6] = {
		float2(-1, -1), float2(-1, 1), float2(1, 1),
		float2(-1, -1), float2(1, 1), float2(1, -1) };
	VertexOutput output = (VertexOutput)(0);
	output.Position = float4(2, 2, 0, 1);
	const uint4 header = Record(0);
	if (header.x != 0x47524931u || header.y == 1 || header.y == 2 || header.y == 3 || header.y >= 6)
	{
		return output;
	}
	const uint primitive = vertex / 6;
	const float2 local = quad[vertex % 6];
	const bool isLine = primitive < 9;
	const uint selected = isLine ? primitive : primitive - 9;
	if (header.z == 0 && (isLine || selected != 0))
	{
		return output;
	}
	const float3 receiver = asfloat(Record(1)).xyz;
	const float3 biased = header.z != 0 ? asfloat(Record(2)).xyz : receiver;
	float3 a = receiver;
	float3 b = biased;
	float4 color = float4(0.05, 1.0, 1.0, 1.0);
	if ((isLine && selected > 0) || (!isLine && selected >= 2))
	{
		const uint corner = isLine ? selected - 1 : selected - 2;
		a = asfloat(Record(9 + corner * 8 + 1)).xyz;
		color = CornerColor(corner);
	}
	else if (!isLine && selected == 1)
	{
		a = biased;
		color = float4(1.0, 0.05, 1.0, 1.0);
	}
	const float4 clipA = mul(float4(a, 1), ViewProjection);
	const float4 clipB = mul(float4(b, 1), ViewProjection);
	if (clipA.w <= 1.0e-5 || (isLine && clipB.w <= 1.0e-5))
	{
		return output;
	}
	float2 clipXY = clipA.xy / clipA.w;
	if (isLine)
	{
		const float2 end = clipB.xy / clipB.w;
		const float2 pixelDelta = (end - clipXY) * (float2)(Extent);
		const float2 perpendicular = float2(-pixelDelta.y, pixelDelta.x) / max(length(pixelDelta), 1.0);
		clipXY = lerp(clipXY, end, local.x * 0.5 + 0.5) + perpendicular * local.y * 2.0 / (float2)(Extent);
	}
	else
	{
		clipXY += local * 9.0 / (float2)(Extent);
	}
	output.Position = float4(clipXY, 0, 1);
	output.Color = color;
	output.Local = local;
	output.Marker = isLine ? 0 : 1;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
	if (input.Marker != 0 && dot(input.Local, input.Local) > 1.0)
	{
		discard;
	}
	return float4(LinearToSRGB(input.Color.rgb), input.Color.a);
}
