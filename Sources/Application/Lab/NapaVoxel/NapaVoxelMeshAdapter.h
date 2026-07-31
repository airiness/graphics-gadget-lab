#pragma once

#include "Core/Math/Vector.h"

#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace gglab
{
	enum class NapaVoxelMeshAdapterError : std::uint8_t
	{
		None = 0,
		CoreValidationFailed = 1,
		MismatchedCoreValidation = 2,
		InvalidSourceRevision = 3,
		DuplicateChunk = 4,
		CountOutOfRange = 5,
		NonFiniteWorldPosition = 6,
		UnrepresentableRenderTranslation = 7,
		MismatchedConvertedData = 8,
	};

	struct NapaVoxelMeshAdapterResult
	{
		NapaVoxelMeshAdapterError m_Error = NapaVoxelMeshAdapterError::None;
		napa::voxel::ValidationError m_CoreError = napa::voxel::ValidationError::None;

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return m_Error == NapaVoxelMeshAdapterError::None;
		}

		[[nodiscard]] constexpr bool Failed() const noexcept
		{
			return !Succeeded();
		}
	};

	struct NapaVoxelWorldPosition
	{
		double m_X = 0.0;
		double m_Y = 0.0;
		double m_Z = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelWorldPosition&, const NapaVoxelWorldPosition&) noexcept = default;
	};

	struct NapaVoxelLocalBounds
	{
		Vector3 m_Min{};
		Vector3 m_Max{};
	};

	struct NapaVoxelRenderVertex
	{
		Vector3 m_Position{};
		Vector3 m_Normal{};
	};

	struct NapaVoxelSectionDrawRange
	{
		napa::voxel::VoxelMaterial m_Material = napa::voxel::VoxelMaterial::Empty;
		std::uint32_t m_FirstIndex = 0;
		std::uint32_t m_IndexCount = 0;
	};

	struct NapaVoxelCpuChunkMesh
	{
		napa::voxel::ChunkCoord m_Chunk{};
		std::uint64_t m_SourceWorldVoxelRevision = 0;
		NapaVoxelWorldPosition m_ChunkOrigin{};
		std::vector<NapaVoxelRenderVertex> m_Vertices;
		std::vector<std::uint32_t> m_Indices;
		std::vector<NapaVoxelSectionDrawRange> m_SectionDrawRanges;
		NapaVoxelLocalBounds m_LocalBounds{};
		napa::voxel::MeshValidationResult m_CoreValidation{};

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_Vertices.empty();
		}
	};

	struct NapaVoxelCpuMeshSet
	{
		std::vector<NapaVoxelCpuChunkMesh> m_Chunks;
		std::uint64_t m_RenderableChunkCount = 0;
		std::uint64_t m_VertexCount = 0;
		std::uint64_t m_IndexCount = 0;
		std::uint64_t m_SectionCount = 0;
	};

	[[nodiscard]] NapaVoxelMeshAdapterResult ComputeNapaVoxelChunkOrigin(
		const napa::voxel::VoxelWorldConfig& config, napa::voxel::ChunkCoord chunk,
		NapaVoxelWorldPosition& chunkOrigin) noexcept;
	[[nodiscard]] NapaVoxelMeshAdapterResult ComputeNapaVoxelRenderTranslation(
		NapaVoxelWorldPosition chunkOrigin, NapaVoxelWorldPosition renderOrigin,
		Vector3& translation) noexcept;
	[[nodiscard]] NapaVoxelMeshAdapterResult ConvertNapaVoxelChunkMesh(
		const napa::voxel::ChunkMeshRecord& source,
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuChunkMesh& mesh);
	[[nodiscard]] NapaVoxelMeshAdapterResult ConvertNapaVoxelMeshRecords(
		std::span<const napa::voxel::ChunkMeshRecord> sources,
		const napa::voxel::VoxelWorldConfig& config, NapaVoxelCpuMeshSet& meshSet);

	static_assert(std::is_standard_layout_v<NapaVoxelWorldPosition>);
	static_assert(std::is_trivially_copyable_v<NapaVoxelWorldPosition>);
	static_assert(std::is_standard_layout_v<NapaVoxelRenderVertex>);
	static_assert(std::is_trivially_copyable_v<NapaVoxelRenderVertex>);
	static_assert(sizeof(NapaVoxelRenderVertex) == sizeof(float) * 6);
	static_assert(offsetof(NapaVoxelRenderVertex, m_Normal) == sizeof(float) * 3);
}
