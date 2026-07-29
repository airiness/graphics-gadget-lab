#pragma once

#include "NapaVoxelCore/Hash/CanonicalHash.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

namespace napa::voxel
{
	inline void WriteCanonicalVoxelWorldConfig(
		CanonicalHashWriter& writer,
		const VoxelWorldConfig& config) noexcept
	{
		const CellAabb& bounds = config.m_LogicalCellBounds;
		writer.WriteI32(bounds.m_Min.m_X);
		writer.WriteI32(bounds.m_Min.m_Y);
		writer.WriteI32(bounds.m_Min.m_Z);
		writer.WriteI32(bounds.m_MaxExclusive.m_X);
		writer.WriteI32(bounds.m_MaxExclusive.m_Y);
		writer.WriteI32(bounds.m_MaxExclusive.m_Z);
		writer.WriteU32(config.m_ChunkCellCount);
		writer.WriteFloat32(config.m_VoxelSize);
		writer.WriteU8(IsoValue);
		writer.WriteFloat32(config.m_SurfaceBandVoxels);
	}
}
