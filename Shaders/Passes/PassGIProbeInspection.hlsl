#include <Common/Common.hlsli>
#include <Lighting/GIFieldSampling.hlsli>

cbuffer Parameters : register(b2)
{
	GIFieldSamplingParameters Field;
	uint3 Coordinate;
	uint Output;
};

void StoreRecord(RWTexture2D<uint> output, uint record, uint4 value)
{
	output[uint2(record * 4, 0)] = value.x;
	output[uint2(record * 4 + 1, 0)] = value.y;
	output[uint2(record * 4 + 2, 0)] = value.z;
	output[uint2(record * 4 + 3, 0)] = value.w;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint width = Field.IrradianceInteriorWidth + 2;
	const uint irradianceTexels = width * width;
	RWTexture2D<uint> output = ResourceDescriptorHeap[NonUniformResourceIndex(Output)];
	if (id.x == 0)
	{
		Texture2DArray<uint> classification = ResourceDescriptorHeap[NonUniformResourceIndex(Field.Classification)];
		const uint state = classification.Load(int4(Coordinate.xzy, 0));
		const uint invalid = classification.Load(int4(0, 0, 0, 0)) & GIProbeFieldInvalidBit;
		StoreRecord(output, 0, uint4(0x47504931u, state & ~GIProbeFieldInvalidBit, invalid,
			Field.IrradianceInteriorWidth));
	}
	if (id.x < irradianceTexels)
	{
		Texture2DArray<float4> atlas = ResourceDescriptorHeap[NonUniformResourceIndex(Field.Irradiance)];
		const uint2 texel = Coordinate.xz * width + uint2(id.x % width, id.x / width);
		StoreRecord(output, 1 + id.x, asuint(float4(
			atlas.Load(int4(texel, Coordinate.y, 0)).rgb * 3.14159265358979323846, 0)));
	}
	else if (id.x < irradianceTexels + 16 * 16)
	{
		Texture2DArray<float2> atlas = ResourceDescriptorHeap[NonUniformResourceIndex(Field.Distance)];
		const uint index = id.x - irradianceTexels;
		const uint2 texel = Coordinate.xz * 16 + uint2(index % 16, index / 16);
		StoreRecord(output, 1 + id.x, asuint(float4(atlas.Load(int4(texel, Coordinate.y, 0)), 0, 0)));
	}
}
