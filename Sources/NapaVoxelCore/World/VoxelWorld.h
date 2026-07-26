#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

namespace napa::voxel
{
	class VoxelWorld final
	{
	public:
		[[nodiscard]] static ValidationResult Create(
			const VoxelWorldConfig& config,
			std::unique_ptr<VoxelWorld>& world);

		VoxelWorld(const VoxelWorld&) = delete;
		VoxelWorld& operator=(const VoxelWorld&) = delete;

		[[nodiscard]] const VoxelWorldConfig& GetConfig() const noexcept;
		[[nodiscard]] const LogicalDomainMetrics& GetLogicalDomainMetrics()
			const noexcept;
		[[nodiscard]] SampleAabb GetLogicalSampleBounds() const noexcept;
		[[nodiscard]] std::size_t GetResidentChunkCount() const noexcept;
		[[nodiscard]] std::uint64_t GetWorldVoxelRevision() const noexcept;

		[[nodiscard]] const VoxelChunk* FindChunk(
			ChunkCoord chunk) const noexcept;
		[[nodiscard]] ValidationResult EnsureChunkAllocated(
			ChunkCoord chunk,
			bool& allocated);

		[[nodiscard]] ValidationResult ReadOriginalSample(
			SampleCoord coordinate,
			VoxelSample& sample) const noexcept;
		[[nodiscard]] ValidationResult ReadCurrentSample(
			SampleCoord coordinate,
			VoxelSample& sample) const noexcept;

		[[nodiscard]] ValidationResult WriteOriginalAndCurrentSample(
			SampleCoord coordinate,
			VoxelSample input,
			bool& changed);
		[[nodiscard]] ValidationResult WriteCurrentSample(
			SampleCoord coordinate,
			VoxelSample input,
			bool& changed);

	private:
		VoxelWorld(
			const VoxelWorldConfig& config,
			const LogicalDomainMetrics& metrics,
			SampleAabb logicalSampleBounds);

		[[nodiscard]] ValidationResult ResolveLogicalSample(
			SampleCoord coordinate,
			OwnedSampleAddress& address) const noexcept;
		[[nodiscard]] ValidationResult ReadSample(
			SampleCoord coordinate,
			VoxelSample& sample,
			bool original) const noexcept;

		VoxelWorldConfig m_Config{};
		LogicalDomainMetrics m_LogicalDomainMetrics{};
		SampleAabb m_LogicalSampleBounds{};
		std::map<ChunkCoord, VoxelChunk, ChunkCoordZYXLess> m_Chunks;
		std::uint64_t m_WorldVoxelRevision = 0;
	};
}
