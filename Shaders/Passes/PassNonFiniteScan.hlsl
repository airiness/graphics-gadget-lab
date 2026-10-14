#include <Common/NonFiniteScan.hlsli>

struct NonFiniteScanParameters
{
	uint ScalarCount;
	uint DispatchGroupsX;
	uint Width;
	uint Height;
	uint Channels;
	uint TextureIndex;
	uint BufferStride;
	uint ComponentCount;
};
ConstantBuffer<NonFiniteScanParameters> g_Pass : register(b2);
RWStructuredBuffer<NonFiniteScanRecord> g_Record : register(u0);

#if defined(GGLAB_NONFINITE_SCAN_BUFFER)
ByteAddressBuffer g_Source : register(t0);
// One (byte offset, scalar size) pair per float component, in byte-offset order.
StructuredBuffer<uint2> g_Components : register(t1);
#endif

[numthreads(1, 1, 1)]
void CSClear()
{
	g_Record[0] = EmptyNonFiniteScanRecord();
}

// The caller clears the record and inserts a UAV barrier before CSMain; it also
// inserts a UAV-to-copy dependency before reading it back. No source is written.
[numthreads(64, 1, 1)]
void CSMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
	const uint groupIndex = group.x + group.y * g_Pass.DispatchGroupsX;
	// Reject the rectangular dispatch tail before multiplying, avoiding uint wrap
	// when a scan approaches UINT32_MAX scalar components.
	if (g_Pass.ScalarCount == 0 || groupIndex > (g_Pass.ScalarCount - 1u) / 64u)
	{
		return;
	}
	const uint index = groupIndex * 64u + lane;
	const bool checked = index < g_Pass.ScalarCount;
	uint bits = 0;
	if (checked)
	{
#if defined(GGLAB_NONFINITE_SCAN_BUFFER)
		const uint element = index / g_Pass.ComponentCount;
		const uint2 component = g_Components[index % g_Pass.ComponentCount];
		const uint address = element * g_Pass.BufferStride + component.x;
		bits = g_Source.Load(address & ~3u);
		if (component.y == 2u)
		{
			bits = asuint(f16tof32((bits >> ((address & 2u) * 8u)) & 0xffffu));
		}
#else
		uint pixel = index / g_Pass.Channels;
		const uint x = pixel % g_Pass.Width;
		pixel /= g_Pass.Width;
		const uint y = pixel % g_Pass.Height;
		const uint slice = pixel / g_Pass.Height;
		// The SRV selects one mip and the addressed slice range. Cubes use an array
		// view; the missing channels of R/RG formats are never counted.
		Texture2DArray<float4> source = ResourceDescriptorHeap[g_Pass.TextureIndex];
		bits = asuint(source.Load(int4(x, y, slice, 0))[index % g_Pass.Channels]);
#endif
	}
	const bool finite = NonFiniteBitsAreFinite(bits);
	const bool nan = !finite && (bits & 0x007fffffu) != 0;
	const bool negative = (bits & 0x80000000u) != 0;
	const uint checkedCount = WaveActiveCountBits(checked);
	const uint nanCount = WaveActiveCountBits(checked && nan);
	const uint positiveInf = WaveActiveCountBits(checked && !finite && !nan && !negative);
	const uint negativeInf = WaveActiveCountBits(checked && !finite && !nan && negative);
	const uint finiteCount = WaveActiveCountBits(checked && finite);
	const uint minimum = WaveActiveMin(checked && finite ? NonFiniteOrderedFloat(bits) : 0xffffffffu);
	const uint maximum = WaveActiveMax(checked && finite ? NonFiniteOrderedFloat(bits) : 0u);
	const uint first = WaveActiveMin(checked && !finite ? index : 0xffffffffu);
	if (WaveIsFirstLane())
	{
		InterlockedAdd(g_Record[0].CheckedCount, checkedCount);
		InterlockedAdd(g_Record[0].FiniteCount, finiteCount);
		if (finiteCount != 0)
		{
			InterlockedMin(g_Record[0].MinimumOrdered, minimum);
			InterlockedMax(g_Record[0].MaximumOrdered, maximum);
		}
		if (nanCount != 0)
		{
			InterlockedAdd(g_Record[0].NanCount, nanCount);
		}
		if (positiveInf != 0)
		{
			InterlockedAdd(g_Record[0].PositiveInfCount, positiveInf);
		}
		if (negativeInf != 0)
		{
			InterlockedAdd(g_Record[0].NegativeInfCount, negativeInf);
		}
		if (first != 0xffffffffu)
		{
			InterlockedMin(g_Record[0].FirstNonFiniteIndex, first);
		}
	}
}
