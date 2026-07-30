#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"

#include <cstdint>

namespace napa::voxel
{
	inline constexpr double CanonicalPositionQuantizationScale = 65536.0;

	struct VoxelWorldConfig
	{
		std::uint32_t m_ChunkCellCount = 16;
		float m_VoxelSize = 1.0f;
		float m_SurfaceBandVoxels = 2.0f;
		CellAabb m_LogicalCellBounds{};

		[[nodiscard]] friend constexpr bool operator==(
			const VoxelWorldConfig&, const VoxelWorldConfig&) noexcept = default;
	};

	struct LogicalDomainMetrics
	{
		std::uint64_t m_CellCountX = 0;
		std::uint64_t m_CellCountY = 0;
		std::uint64_t m_CellCountZ = 0;

		std::uint64_t m_SampleCountX = 0;
		std::uint64_t m_SampleCountY = 0;
		std::uint64_t m_SampleCountZ = 0;

		std::uint64_t m_TotalCellCount = 0;
		std::uint64_t m_TotalSampleCount = 0;

		ChunkAabb m_CellOwnerChunkBounds{};
		ChunkAabb m_SampleOwnerChunkBounds{};
		std::uint64_t m_CellOwnerChunkCount = 0;

		[[nodiscard]] friend constexpr bool operator==(
			const LogicalDomainMetrics&, const LogicalDomainMetrics&) noexcept = default;
	};

	[[nodiscard]] ValidationResult ComputeLogicalDomainMetrics(
		const VoxelWorldConfig& config, LogicalDomainMetrics& metrics) noexcept;
	[[nodiscard]] ValidationResult ValidateConfig(const VoxelWorldConfig& config) noexcept;
}
