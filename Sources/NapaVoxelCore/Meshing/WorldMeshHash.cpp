#include "NapaVoxelCore/Meshing/WorldMeshHash.h"

#include "NapaVoxelCore/BuildContract.h"
#include "NapaVoxelCore/Hash/CanonicalHash.h"
#include "NapaVoxelCore/Hash/CanonicalVoxelSerialization.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] bool AccumulateCount(std::uint64_t value, std::uint64_t& total) noexcept
		{
			const std::optional<std::uint64_t> sum = CheckedAdd(total, value);
			if (!sum)
			{
				return false;
			}
			total = *sum;
			return true;
		}
	}

	ValidationResult ValidateAndHashWorldMeshRecords(std::span<const ChunkMeshRecord> records,
		const VoxelWorldConfig& config, WorldMeshValidationResult& result)
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}

		LogicalDomainMetrics metrics{};
		const ValidationResult metricsResult = ComputeLogicalDomainMetrics(config, metrics);
		if (metricsResult.Failed())
		{
			return metricsResult;
		}
		const std::optional<std::size_t> expectedChunkCount =
			CheckedNarrow<std::size_t>(metrics.m_CellOwnerChunkCount);
		if (!expectedChunkCount || records.size() != *expectedChunkCount)
		{
			return {
				ValidationError::InvalidWorldMeshRecordSet,
			};
		}

		CanonicalHashWriter writer;
		const BuildContract& contract = GetBuildContract();
		writer.WriteU32(contract.m_MeshHashSchemaVersion);
		writer.WriteU32(contract.m_ReferenceMesherVersion);
		WriteCanonicalVoxelWorldConfig(writer, config);
		writer.WriteCount(metrics.m_CellOwnerChunkCount);

		WorldMeshValidationResult validated{
			.m_ChunkCount = metrics.m_CellOwnerChunkCount,
		};
		// Exact cardinality plus strictly ordered, in-bounds coordinates
		// proves complete coverage without duplicating Domain traversal.
		const ChunkAabb& chunkBounds = metrics.m_CellOwnerChunkBounds;
		const ChunkCoordZYXLess chunkLess{};
		std::optional<ChunkCoord> previousChunk;
		for (const ChunkMeshRecord& record : records)
		{
			if (!chunkBounds.Contains(record.m_Chunk) ||
				(previousChunk && !chunkLess(*previousChunk, record.m_Chunk)))
			{
				return {
					ValidationError::InvalidWorldMeshRecordSet,
				};
			}
			previousChunk = record.m_Chunk;

			MeshValidationResult chunkValidation{};
			const ValidationResult validationResult = ValidateAndHashChunkMesh(
				record.m_Mesh, record.m_WindingEvidence, config, record.m_Chunk, chunkValidation);
			if (validationResult.Failed())
			{
				return validationResult;
			}
			if (chunkValidation != record.m_Validation)
			{
				return {
					ValidationError::MismatchedChunkMeshValidation,
				};
			}

			if (!AccumulateCount(chunkValidation.m_VertexCount, validated.m_VertexCount) ||
				!AccumulateCount(chunkValidation.m_SectionCount, validated.m_SectionCount) ||
				!AccumulateCount(chunkValidation.m_IndexCount, validated.m_IndexCount) ||
				!AccumulateCount(chunkValidation.m_TriangleCount, validated.m_TriangleCount) ||
				!AccumulateCount(record.m_SkippedDegenerateTriangleCount,
					validated.m_SkippedDegenerateTriangleCount))
			{
				return {
					ValidationError::ArithmeticOverflow,
				};
			}

			writer.WriteI32(record.m_Chunk.m_X);
			writer.WriteI32(record.m_Chunk.m_Y);
			writer.WriteI32(record.m_Chunk.m_Z);
			writer.WriteU64(chunkValidation.m_ValidationHash);
		}

		validated.m_ValidationHash = writer.GetValue();
		result = validated;
		return {};
	}
}
