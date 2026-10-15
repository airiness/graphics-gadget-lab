#include <Common/NonFiniteCounters.hlsli>

RWByteAddressBuffer g_Record : register(u0);
cbuffer ClearParameters : register(b2)
{
	uint FrameLow;
	uint FrameHigh;
};

[numthreads(64, 1, 1)]
void CSClear(uint3 id : SV_DispatchThreadID)
{
	const uint records = NF_SITE_COUNT * NF_PROGRAM_COUNT;
	if (id.x < records)
	{
		const uint address = 32u + id.x * 32u;
		g_Record.Store4(address, 0u.xxxx);
		g_Record.Store4(address + 16u, 0u.xxxx);
	}
	if (id.x == 0)
	{
		g_Record.Store4(0, uint4(0x4e464354u, 1u, NF_SITE_COUNT, NF_PROGRAM_COUNT));
		g_Record.Store4(16, uint4(FrameLow, FrameHigh, NF_INVENTORY_LOW, NF_INVENTORY_HIGH));
	}
}
