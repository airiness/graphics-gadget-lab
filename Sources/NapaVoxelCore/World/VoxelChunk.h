#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelSample.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace napa::voxel
{
	class VoxelWorld;
	struct SphereEditRequest;
	struct VoxelMutationResult;

	class VoxelChunk final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		[[nodiscard]] static ValidationResult Create(
			std::uint32_t chunkCellCount, std::unique_ptr<VoxelChunk>& chunk);

		explicit VoxelChunk(ConstructionToken, std::uint32_t chunkCellCount);

		VoxelChunk(const VoxelChunk&) = delete;
		VoxelChunk& operator=(const VoxelChunk&) = delete;
		VoxelChunk(VoxelChunk&&) = delete;
		VoxelChunk& operator=(VoxelChunk&&) = delete;

		[[nodiscard]] std::uint32_t GetChunkCellCount() const noexcept;
		[[nodiscard]] std::size_t GetSampleCount() const noexcept;
		[[nodiscard]] std::uint64_t GetVoxelRevision() const noexcept;

		[[nodiscard]] ValidationResult ReadOriginalSample(
			LocalCoord local, VoxelSample& sample) const noexcept;
		[[nodiscard]] ValidationResult ReadCurrentSample(
			LocalCoord local, VoxelSample& sample) const noexcept;

		[[nodiscard]] ValidationResult WriteOriginalAndCurrentSample(
			LocalCoord local, VoxelSample input, bool& changed) noexcept;
		[[nodiscard]] ValidationResult WriteCurrentSample(
			LocalCoord local, VoxelSample input, bool& changed) noexcept;

	private:
		friend ValidationResult ApplySphereEdit(VoxelWorld& world,
			const SphereEditRequest& request, VoxelMutationResult& result);
		friend class VoxelWorld;

		[[nodiscard]] ValidationResult ResolveFlatIndex(
			LocalCoord local, std::size_t& flatIndex) const noexcept;
		[[nodiscard]] ValidationResult AdvanceRevision() noexcept;
		[[nodiscard]] ValidationResult RestoreCurrentSamples(
			std::span<const LocalCoord> coordinates, bool& changed) noexcept;
		[[nodiscard]] ValidationResult InitializePreparedSample(
			LocalCoord local, VoxelSample prepared) noexcept;
		void CommitInitialState() noexcept;

		std::uint32_t m_ChunkCellCount = 0;
		std::vector<VoxelSample> m_OriginalSamples;
		std::vector<VoxelSample> m_CurrentSamples;
		std::uint64_t m_VoxelRevision = 0;
	};
}
