#pragma once

#include "NapaVoxelCore/Meshing/BoundaryContour.h"
#include "NapaVoxelCore/Meshing/MeshData.h"
#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/World/Coordinates.h"

#include <cstdint>
#include <vector>

namespace napa::voxel
{
	struct ChunkMeshRecord
	{
		ChunkCoord m_Chunk{};
		std::uint64_t m_SourceWorldVoxelRevision = 0;
		MeshData m_Mesh;
		// Matches material-section order, then triangle index order.
		std::vector<MeshTriangleWindingEvidence> m_WindingEvidence;
		MeshValidationResult m_Validation{};
		std::uint64_t m_SkippedDegenerateTriangleCount = 0;
		ChunkBoundaryContourSet m_BoundaryContours = MakeEmptyChunkBoundaryContourSet();
	};
}
