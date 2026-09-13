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

	enum class VoxelMutationChangeKind : std::uint8_t
	{
		None = 0,
		DamageOnly = 1,
		SurfaceChanged = 2,
	};

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
		[[nodiscard]] VoxelMutationChangeKind GetChangeKind() const noexcept
		{
			if (!Changed())
			{
				return VoxelMutationChangeKind::None;
			}
			for (const VoxelSampleChange& change : m_SampleChanges)
			{
				if (change.m_Before.m_Density != change.m_After.m_Density ||
					change.m_Before.m_Material != change.m_After.m_Material)
				{
					return VoxelMutationChangeKind::SurfaceChanged;
				}
			}
			return VoxelMutationChangeKind::DamageOnly;
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
	static_assert(std::is_same_v<
		std::underlying_type_t<VoxelMutationChangeKind>, std::uint8_t>);
}
