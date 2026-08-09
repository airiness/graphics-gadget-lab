#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelMeshAdapter.h"

#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include "Core/Math/MathFunctions.h"

#include "NapaVoxelCore/Meshing/MeshValidation.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] constexpr NapaVoxelMeshAdapterResult MakeAdapterError(
			NapaVoxelMeshAdapterError error) noexcept
		{
			return { .m_Error = error };
		}

		[[nodiscard]] constexpr NapaVoxelMeshAdapterResult MakeCoreError(
			napa::voxel::ValidationResult result) noexcept
		{
			return {
				.m_Error = NapaVoxelMeshAdapterError::CoreValidationFailed,
				.m_CoreError = result.m_Error,
			};
		}

		[[nodiscard]] bool IsFinite(NapaVoxelWorldPosition position) noexcept
		{
			return std::isfinite(position.m_X) && std::isfinite(position.m_Y) &&
				std::isfinite(position.m_Z);
		}

		[[nodiscard]] bool HasSufficientFloatPrecision(
			double chunkOrigin, double chunkEnd, double requiredSpacing) noexcept
		{
			const double maximumMagnitude = std::max(std::abs(chunkOrigin), std::abs(chunkEnd));
			const float roundedMagnitude = static_cast<float>(maximumMagnitude);
			const float nextMagnitude =
				std::nextafter(roundedMagnitude, std::numeric_limits<float>::infinity());
			if (!std::isfinite(nextMagnitude))
			{
				return false;
			}

			const double floatSpacing =
				static_cast<double>(nextMagnitude) - static_cast<double>(roundedMagnitude);
			return floatSpacing <= requiredSpacing;
		}

		[[nodiscard]] NapaVoxelMeshAdapterResult ComputeValidatedChunkOrigin(
			const napa::voxel::VoxelWorldConfig& config, napa::voxel::ChunkCoord chunk,
			NapaVoxelWorldPosition& chunkOrigin) noexcept
		{
			const double chunkSize = static_cast<double>(config.m_ChunkCellCount) *
				static_cast<double>(config.m_VoxelSize);
			const NapaVoxelWorldPosition prepared{
				.m_X = static_cast<double>(chunk.m_X) * chunkSize,
				.m_Y = static_cast<double>(chunk.m_Y) * chunkSize,
				.m_Z = static_cast<double>(chunk.m_Z) * chunkSize,
			};
			if (!std::isfinite(chunkSize) || !IsFinite(prepared))
			{
				return MakeAdapterError(NapaVoxelMeshAdapterError::NonFiniteWorldPosition);
			}

			chunkOrigin = prepared;
			return {};
		}

		[[nodiscard]] NapaVoxelMeshAdapterResult ConvertValidatedChunkMesh(
			const napa::voxel::ChunkMeshRecord& source,
			const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuChunkMesh& mesh)
		{
			using namespace napa::voxel;

			if (source.m_SourceWorldVoxelRevision == 0)
			{
				return MakeAdapterError(NapaVoxelMeshAdapterError::InvalidSourceRevision);
			}

			MeshValidationResult validation{};
			const ValidationResult validationResult = ValidateAndHashChunkMesh(source.m_Mesh,
				source.m_WindingEvidence, config, source.m_Chunk, validation);
			if (validationResult.Failed())
			{
				return MakeCoreError(validationResult);
			}
			if (validation != source.m_Validation)
			{
				return MakeAdapterError(NapaVoxelMeshAdapterError::MismatchedCoreValidation);
			}

			const std::optional<std::uint32_t> vertexCount =
				CheckedNarrow<std::uint32_t>(validation.m_VertexCount);
			const std::optional<std::uint32_t> indexCount =
				CheckedNarrow<std::uint32_t>(validation.m_IndexCount);
			const std::optional<std::uint32_t> sectionCount =
				CheckedNarrow<std::uint32_t>(validation.m_SectionCount);
			const std::optional<std::size_t> indexCapacity =
				CheckedNarrow<std::size_t>(validation.m_IndexCount);
			if (!vertexCount || !indexCount || !sectionCount || !indexCapacity)
			{
				return MakeAdapterError(NapaVoxelMeshAdapterError::CountOutOfRange);
			}

			NapaVoxelWorldPosition chunkOrigin{};
			const NapaVoxelMeshAdapterResult originResult =
				ComputeValidatedChunkOrigin(config, source.m_Chunk, chunkOrigin);
			if (originResult.Failed())
			{
				return originResult;
			}

			NapaVoxelCpuChunkMesh converted{
				.m_Chunk = source.m_Chunk,
				.m_SourceWorldVoxelRevision = source.m_SourceWorldVoxelRevision,
				.m_ChunkOrigin = chunkOrigin,
				.m_LocalBounds = {
					.m_Min = {
						source.m_Mesh.m_Bounds.m_Min.m_X,
						source.m_Mesh.m_Bounds.m_Min.m_Y,
						source.m_Mesh.m_Bounds.m_Min.m_Z,
					},
					.m_Max = {
						source.m_Mesh.m_Bounds.m_Max.m_X,
						source.m_Mesh.m_Bounds.m_Max.m_Y,
						source.m_Mesh.m_Bounds.m_Max.m_Z,
					},
				},
				.m_CoreValidation = validation,
			};
			converted.m_Vertices.reserve(*vertexCount);
			for (const MeshVertex& sourceVertex : source.m_Mesh.m_Vertices)
			{
				const NapaVoxelRenderVertex vertex{
					.m_Position = {
						sourceVertex.m_Position.m_X,
						sourceVertex.m_Position.m_Y,
						sourceVertex.m_Position.m_Z,
					},
					.m_Normal = {
						sourceVertex.m_Normal.m_X,
						sourceVertex.m_Normal.m_Y,
						sourceVertex.m_Normal.m_Z,
					},
				};
				if (!math::IsFinite(vertex.m_Position) || !math::IsFinite(vertex.m_Normal))
				{
					return MakeAdapterError(NapaVoxelMeshAdapterError::MismatchedConvertedData);
				}
				converted.m_Vertices.push_back(vertex);
			}

			converted.m_Indices.reserve(*indexCapacity);
			converted.m_SectionDrawRanges.reserve(*sectionCount);
			for (const MeshSection& sourceSection : source.m_Mesh.m_Sections)
			{
				const std::optional<std::uint32_t> firstIndex =
					CheckedNarrow<std::uint32_t>(converted.m_Indices.size());
				const std::optional<std::uint32_t> sectionIndexCount =
					CheckedNarrow<std::uint32_t>(sourceSection.m_Indices.size());
				if (!firstIndex || !sectionIndexCount)
				{
					return MakeAdapterError(NapaVoxelMeshAdapterError::CountOutOfRange);
				}

				converted.m_SectionDrawRanges.push_back({
					.m_Material = sourceSection.m_Material,
					.m_FirstIndex = *firstIndex,
					.m_IndexCount = *sectionIndexCount,
					});
				converted.m_Indices.insert(converted.m_Indices.end(),
					sourceSection.m_Indices.begin(), sourceSection.m_Indices.end());
			}

			if (converted.m_Vertices.size() != *vertexCount ||
				converted.m_Indices.size() != *indexCount ||
				converted.m_SectionDrawRanges.size() != *sectionCount)
			{
				return MakeAdapterError(NapaVoxelMeshAdapterError::MismatchedConvertedData);
			}

			mesh = std::move(converted);
			return {};
		}

		[[nodiscard]] bool AccumulateCount(std::uint64_t value, std::uint64_t& total) noexcept
		{
			const std::optional<std::uint64_t> sum =
				napa::voxel::CheckedAdd(total, value);
			if (!sum)
			{
				return false;
			}
			total = *sum;
			return true;
		}

		NapaVoxelMeshAdapterResult ConvertMeshRecordPointers(
			std::vector<const napa::voxel::ChunkMeshRecord*> orderedSources,
			const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuMeshSet& meshSet)
		{
			using namespace napa::voxel;
			std::sort(orderedSources.begin(), orderedSources.end(),
				[](const ChunkMeshRecord* lhs, const ChunkMeshRecord* rhs) noexcept
				{
					return ChunkCoordZYXLess{}(lhs->m_Chunk, rhs->m_Chunk);
				});
			for (std::size_t index = 1; index < orderedSources.size(); ++index)
			{
				if (orderedSources[index - 1]->m_Chunk == orderedSources[index]->m_Chunk)
				{
					return MakeAdapterError(NapaVoxelMeshAdapterError::DuplicateChunk);
				}
			}

			NapaVoxelCpuMeshSet converted;
			converted.m_Chunks.reserve(orderedSources.size());
			for (const ChunkMeshRecord* source : orderedSources)
			{
				NapaVoxelCpuChunkMesh chunkMesh;
				const NapaVoxelMeshAdapterResult convertResult =
					ConvertValidatedChunkMesh(*source, config, chunkMesh);
				if (convertResult.Failed())
				{
					return convertResult;
				}

				if (!chunkMesh.IsEmpty() &&
					!AccumulateCount(std::uint64_t{ 1 }, converted.m_RenderableChunkCount))
				{
					return MakeAdapterError(NapaVoxelMeshAdapterError::CountOutOfRange);
				}
				if (!AccumulateCount(chunkMesh.m_CoreValidation.m_VertexCount,
					converted.m_VertexCount) ||
					!AccumulateCount(chunkMesh.m_CoreValidation.m_IndexCount,
						converted.m_IndexCount) ||
					!AccumulateCount(chunkMesh.m_CoreValidation.m_SectionCount,
						converted.m_SectionCount))
				{
					return MakeAdapterError(NapaVoxelMeshAdapterError::CountOutOfRange);
				}

				converted.m_Chunks.push_back(std::move(chunkMesh));
			}

			meshSet = std::move(converted);
			return {};
		}
	}

	NapaVoxelMeshAdapterResult ComputeNapaVoxelChunkOrigin(
		const napa::voxel::VoxelWorldConfig& config, napa::voxel::ChunkCoord chunk,
		NapaVoxelWorldPosition& chunkOrigin) noexcept
	{
		const napa::voxel::ValidationResult configResult = napa::voxel::ValidateConfig(config);
		if (configResult.Failed())
		{
			return MakeCoreError(configResult);
		}
		return ComputeValidatedChunkOrigin(config, chunk, chunkOrigin);
	}

	NapaVoxelMeshAdapterResult ComputeNapaVoxelRenderTranslation(
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelWorldPosition chunkOrigin,
		NapaVoxelWorldPosition renderOrigin, Vector3& translation) noexcept
	{
		const napa::voxel::ValidationResult configResult = napa::voxel::ValidateConfig(config);
		if (configResult.Failed())
		{
			return MakeCoreError(configResult);
		}
		if (!IsFinite(chunkOrigin) || !IsFinite(renderOrigin))
		{
			return MakeAdapterError(NapaVoxelMeshAdapterError::NonFiniteWorldPosition);
		}

		const NapaVoxelWorldPosition difference{
			.m_X = chunkOrigin.m_X - renderOrigin.m_X,
			.m_Y = chunkOrigin.m_Y - renderOrigin.m_Y,
			.m_Z = chunkOrigin.m_Z - renderOrigin.m_Z,
		};
		constexpr double MaximumFloat = static_cast<double>(std::numeric_limits<float>::max());
		if (!IsFinite(difference) || std::abs(difference.m_X) > MaximumFloat ||
			std::abs(difference.m_Y) > MaximumFloat || std::abs(difference.m_Z) > MaximumFloat)
		{
			return MakeAdapterError(
				NapaVoxelMeshAdapterError::UnrepresentableRenderTranslation);
		}

		const double chunkSize = static_cast<double>(config.m_ChunkCellCount) *
			static_cast<double>(config.m_VoxelSize);
		const NapaVoxelWorldPosition chunkEnd{
			.m_X = difference.m_X + chunkSize,
			.m_Y = difference.m_Y + chunkSize,
			.m_Z = difference.m_Z + chunkSize,
		};
		const double requiredSpacing = static_cast<double>(config.m_VoxelSize) /
			napa::voxel::CanonicalPositionQuantizationScale;
		if (!IsFinite(chunkEnd) ||
			!HasSufficientFloatPrecision(difference.m_X, chunkEnd.m_X, requiredSpacing) ||
			!HasSufficientFloatPrecision(difference.m_Y, chunkEnd.m_Y, requiredSpacing) ||
			!HasSufficientFloatPrecision(difference.m_Z, chunkEnd.m_Z, requiredSpacing))
		{
			return MakeAdapterError(
				NapaVoxelMeshAdapterError::InsufficientRenderTranslationPrecision);
		}

		const Vector3 prepared{
			static_cast<float>(difference.m_X),
			static_cast<float>(difference.m_Y),
			static_cast<float>(difference.m_Z),
		};
		if (!math::IsFinite(prepared))
		{
			return MakeAdapterError(
				NapaVoxelMeshAdapterError::UnrepresentableRenderTranslation);
		}

		translation = prepared;
		return {};
	}

	NapaVoxelMeshAdapterResult ConvertNapaVoxelChunkMesh(
		const napa::voxel::ChunkMeshRecord& source,
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuChunkMesh& mesh)
	{
		const napa::voxel::ValidationResult configResult = napa::voxel::ValidateConfig(config);
		if (configResult.Failed())
		{
			return MakeCoreError(configResult);
		}
		return ConvertValidatedChunkMesh(source, config, mesh);
	}

	NapaVoxelMeshAdapterResult ConvertNapaVoxelMeshRecords(
		std::span<const napa::voxel::ChunkMeshRecord> sources,
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuMeshSet& meshSet)
	{
		using namespace napa::voxel;

		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return MakeCoreError(configResult);
		}

		std::vector<const ChunkMeshRecord*> orderedSources;
		orderedSources.reserve(sources.size());
		for (const ChunkMeshRecord& source : sources)
		{
			orderedSources.push_back(&source);
		}
		return ConvertMeshRecordPointers(std::move(orderedSources), config, meshSet);
	}

	NapaVoxelMeshAdapterResult ConvertNapaVoxelMeshReplacements(
		const napa::voxel::CpuMeshReplacementView& sources,
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuMeshSet& meshSet)
	{
		const napa::voxel::ValidationResult configResult = napa::voxel::ValidateConfig(config);
		if (configResult.Failed())
		{
			return MakeCoreError(configResult);
		}

		std::vector<const napa::voxel::ChunkMeshRecord*> orderedSources;
		orderedSources.reserve(sources.size());
		for (std::size_t index = 0; index < sources.size(); ++index)
		{
			orderedSources.push_back(&sources[index]);
		}
		return ConvertMeshRecordPointers(std::move(orderedSources), config, meshSet);
	}
}
