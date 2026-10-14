#pragma once

// Kept in lockstep with NonFiniteScanRecord in Runtime's NonFiniteWatchTypes.h.
// Ordered IEEE keys let integer atomics retain finite extrema, including signed zero.
struct NonFiniteScanRecord
{
	uint CheckedCount;
	uint NanCount;
	uint PositiveInfCount;
	uint NegativeInfCount;
	uint FiniteCount;
	uint MinimumOrdered;
	uint MaximumOrdered;
	uint FirstNonFiniteIndex;
};

NonFiniteScanRecord EmptyNonFiniteScanRecord()
{
	NonFiniteScanRecord value = (NonFiniteScanRecord)0;
	value.MinimumOrdered = 0xffffffffu;
	value.FirstNonFiniteIndex = 0xffffffffu;
	return value;
}

uint NonFiniteOrderedFloat(uint bits)
{
	return bits ^ ((bits & 0x80000000u) ? 0xffffffffu : 0x80000000u);
}

bool NonFiniteBitsAreFinite(uint bits)
{
	return (bits & 0x7f800000u) != 0x7f800000u;
}
