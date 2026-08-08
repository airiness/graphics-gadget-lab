#pragma once

#include "NapaVoxelCore/Edit/SphereEdit.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace napa::voxel
{
	class VoxelWorld;

	struct VoxelSampleChange
	{
		SampleCoord m_Coordinate{};
		VoxelSample m_Before{};
		VoxelSample m_After{};

		[[nodiscard]] friend bool operator==(
			const VoxelSampleChange&, const VoxelSampleChange&) noexcept = default;
	};

	struct VoxelMutationResult
	{
		std::uint64_t m_BaseWorldVoxelRevision = 0;
		std::uint64_t m_TargetWorldVoxelRevision = 0;
		std::vector<VoxelSampleChange> m_SampleChanges;
		std::vector<ChunkCoord> m_DataDirtyChunks;
		std::vector<ChunkCoord> m_MeshDirtyChunks;

		[[nodiscard]] bool Changed() const noexcept
		{
			return !m_SampleChanges.empty();
		}

		[[nodiscard]] friend bool operator==(
			const VoxelMutationResult&, const VoxelMutationResult&) noexcept = default;
	};

	[[nodiscard]] ValidationResult EvaluateSphereEditSampleTransition(
		const SphereEditContext& context, SampleCoord sample,
		VoxelSample before, VoxelSample& after) noexcept;
	[[nodiscard]] ValidationResult ApplySphereEdit(VoxelWorld& world,
		const SphereEditRequest& request, VoxelMutationResult& result);
	[[nodiscard]] ValidationResult DeriveVoxelMutationDirtyChunks(
		const VoxelWorldConfig& config, std::span<const VoxelSampleChange> changes,
		std::vector<ChunkCoord>& dataDirtyChunks,
		std::vector<ChunkCoord>& meshDirtyChunks);

	static_assert(std::is_standard_layout_v<VoxelSampleChange>);
	static_assert(std::is_trivially_copyable_v<VoxelSampleChange>);
}
