#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"

#include <cstdint>
#include <memory>

namespace napa::voxel
{
	class VisibleMeshSet;
	class VoxelWorld;
	struct VoxelMutationResult;

	class PendingDataOnlyPublication final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		explicit PendingDataOnlyPublication(
			ConstructionToken, std::uint64_t targetWorldRevision) noexcept;
		PendingDataOnlyPublication(const PendingDataOnlyPublication&) = delete;
		PendingDataOnlyPublication& operator=(const PendingDataOnlyPublication&) = delete;

		[[nodiscard]] std::uint64_t GetTargetWorldRevision() const noexcept;

	private:
		friend ValidationResult PrepareDataOnlyPublication(const VoxelWorld& authoritativeWorld,
			const VoxelMutationResult& mutation, const VisibleMeshSet& visible,
			std::unique_ptr<PendingDataOnlyPublication>& pending);
		friend void CommitDataOnlyPublication(
			std::unique_ptr<PendingDataOnlyPublication>& pending,
			VisibleMeshSet& visible) noexcept;

		std::uint64_t m_TargetWorldRevision = 0;
	};

	[[nodiscard]] ValidationResult PrepareDataOnlyPublication(
		const VoxelWorld& authoritativeWorld, const VoxelMutationResult& mutation,
		const VisibleMeshSet& visible,
		std::unique_ptr<PendingDataOnlyPublication>& pending);
	void CommitDataOnlyPublication(
		std::unique_ptr<PendingDataOnlyPublication>& pending,
		VisibleMeshSet& visible) noexcept;
}
