#pragma once

#include "NapaVoxelCore/Edit/SphereEdit.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstdint>
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

	static_assert(std::is_standard_layout_v<VoxelSampleChange>);
	static_assert(std::is_trivially_copyable_v<VoxelSampleChange>);
}
