#pragma once

#include "NapaVoxelCore/Math/Vector.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"

#include <cstdint>
#include <span>

namespace gglab
{
	enum class NapaVoxelRaycastError : std::uint8_t
	{
		None = 0,
		InvalidRay,
		InvalidConfig,
		UninitializedVisibleMesh,
	};

	struct NapaVoxelRay
	{
		napa::voxel::Double3 m_Origin{};
		napa::voxel::Double3 m_Direction{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelRay&, const NapaVoxelRay&) noexcept = default;
	};

	struct NapaVoxelRaycastHit
	{
		napa::voxel::Double3 m_WorldPosition{};
		double m_Distance = 0.0;
		std::int64_t m_DistanceKey = 0;
		napa::voxel::ChunkCoord m_Chunk{};
		napa::voxel::VoxelMaterial m_Material = napa::voxel::VoxelMaterial::Empty;
		std::uint64_t m_SectionOrdinal = 0;
		std::uint64_t m_TriangleOrdinal = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelRaycastHit&, const NapaVoxelRaycastHit&) noexcept = default;
	};

	struct NapaVoxelRaycastResult
	{
		NapaVoxelRaycastError m_Error = NapaVoxelRaycastError::None;
		bool m_Hit = false;

		[[nodiscard]] constexpr bool Succeeded() const noexcept
		{
			return m_Error == NapaVoxelRaycastError::None;
		}
		[[nodiscard]] constexpr bool Failed() const noexcept
		{
			return !Succeeded();
		}
	};

	[[nodiscard]] bool IsValidNapaVoxelRay(const NapaVoxelRay& ray) noexcept;
	[[nodiscard]] NapaVoxelRaycastResult RaycastNapaVoxelMeshRecords(
		const napa::voxel::VoxelWorldConfig& config,
		std::span<const napa::voxel::ChunkMeshRecord> records,
		const NapaVoxelRay& ray, NapaVoxelRaycastHit& hit) noexcept;
	[[nodiscard]] NapaVoxelRaycastResult RaycastNapaVoxelVisibleMesh(
		const napa::voxel::VisibleMeshSet& visible,
		const NapaVoxelRay& ray, NapaVoxelRaycastHit& hit) noexcept;
}
