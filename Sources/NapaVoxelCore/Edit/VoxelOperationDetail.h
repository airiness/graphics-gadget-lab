#pragma once

#include "NapaVoxelCore/Testing/VoxelMutationTestAccess.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace napa::voxel::detail
{
	extern thread_local testing::VoxelOperationAllocationProbe* ActiveOperationAllocationProbe;
	extern thread_local bool SimulateExhaustedWorldRevision;
	extern thread_local bool IsVoxelOperationCommitPhase;

	[[nodiscard]] ValidationResult RecordPotentialAllocation() noexcept;

	template <typename Value>
	[[nodiscard]] ValidationResult EnsureAppendCapacity(std::vector<Value>& values)
	{
		if (values.size() < values.capacity())
		{
			return {};
		}
		if (values.size() == values.max_size())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		const ValidationResult allocationResult = RecordPotentialAllocation();
		if (allocationResult.Failed())
		{
			return allocationResult;
		}

		std::size_t nextCapacity = 1;
		if (values.capacity() != 0)
		{
			const auto doubled = CheckedMul(values.capacity(), std::size_t{ 2 });
			nextCapacity = doubled.has_value() ?
				std::min(*doubled, values.max_size()) : values.max_size();
		}
		values.reserve(nextCapacity);
		return {};
	}

	template <typename Value>
	[[nodiscard]] ValidationResult AppendTracked(
		std::vector<Value>& values, const Value& value)
	{
		const ValidationResult capacityResult = EnsureAppendCapacity(values);
		if (capacityResult.Failed())
		{
			return capacityResult;
		}
		values.push_back(value);
		return {};
	}

	[[nodiscard]] ValidationResult DeriveDirtyChunksFromValidatedConfig(
		const VoxelWorldConfig& config, std::span<const VoxelSampleChange> changes,
		std::vector<ChunkCoord>& dataDirtyChunks,
		std::vector<ChunkCoord>& meshDirtyChunks, bool validateChanges);
}
