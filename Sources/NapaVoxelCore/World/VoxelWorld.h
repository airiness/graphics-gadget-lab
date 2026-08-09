#pragma once

#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelRestore.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>

namespace napa::voxel
{
	struct PrimitiveDesc;
	struct PrimitiveWorldGenerationResult;
	struct SphereEditRequest;
	struct VoxelMutationResult;

	class VoxelWorld final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		[[nodiscard]] static ValidationResult Create(
			const VoxelWorldConfig& config, std::unique_ptr<VoxelWorld>& world);

		VoxelWorld(ConstructionToken, const VoxelWorldConfig& config,
			const LogicalDomainMetrics& metrics, SampleAabb logicalSampleBounds);

		VoxelWorld(const VoxelWorld&) = delete;
		VoxelWorld& operator=(const VoxelWorld&) = delete;

		[[nodiscard]] const VoxelWorldConfig& GetConfig() const noexcept;
		[[nodiscard]] const LogicalDomainMetrics& GetLogicalDomainMetrics() const noexcept;
		[[nodiscard]] SampleAabb GetLogicalSampleBounds() const noexcept;
		[[nodiscard]] std::size_t GetResidentChunkCount() const noexcept;
		[[nodiscard]] std::uint64_t GetWorldVoxelRevision() const noexcept;
		[[nodiscard]] std::uint64_t GetSurfaceStateRevision() const noexcept;
		[[nodiscard]] bool IsOriginalStateSealed() const noexcept;
		void SealOriginalState() noexcept;

		[[nodiscard]] const VoxelChunk* FindChunk(ChunkCoord chunk) const noexcept;
		[[nodiscard]] ValidationResult EnsureChunkAllocated(ChunkCoord chunk, bool& allocated);

		[[nodiscard]] ValidationResult ReadOriginalSample(
			SampleCoord coordinate, VoxelSample& sample) const noexcept;
		[[nodiscard]] ValidationResult ReadCurrentSample(
			SampleCoord coordinate, VoxelSample& sample) const noexcept;

		[[nodiscard]] ValidationResult WriteOriginalAndCurrentSample(
			SampleCoord coordinate, VoxelSample input, bool& changed);
		[[nodiscard]] ValidationResult WriteCurrentSample(
			SampleCoord coordinate, VoxelSample input, bool& changed);

	private:
		using ChunkMap =
			std::map<ChunkCoord, std::unique_ptr<VoxelChunk>, ChunkCoordZYXLess>;

		friend ValidationResult ApplySphereEdit(VoxelWorld& world,
			const SphereEditRequest& request, VoxelMutationResult& result);
		friend ValidationResult RestoreAll(
			VoxelWorld& world, VoxelMutationResult& result);
		friend ValidationResult RestoreSampleOwnerChunk(
			VoxelWorld& world, ChunkCoord chunk, VoxelMutationResult& result);
		friend ValidationResult RestoreRegion(
			VoxelWorld& world, const SampleAabb& region, VoxelMutationResult& result);
		friend ValidationResult GeneratePrimitiveVoxelWorld(const VoxelWorldConfig& config,
			std::span<const PrimitiveDesc> primitives, std::unique_ptr<VoxelWorld>& world,
			PrimitiveWorldGenerationResult& result);

		[[nodiscard]] ValidationResult ResolveLogicalSample(
			SampleCoord coordinate, OwnedSampleAddress& address) const noexcept;
		[[nodiscard]] ValidationResult ReadSample(
			SampleCoord coordinate, VoxelSample& sample, bool original) const noexcept;
		[[nodiscard]] ValidationResult FindOrCreateChunk(
			ChunkCoord coordinate, VoxelChunk*& chunk, bool& allocated);
		[[nodiscard]] ValidationResult RestoreRegionInternal(
			const SampleAabb& region, VoxelMutationResult& result);
		[[nodiscard]] ValidationResult InitializePreparedSample(
			SampleCoord coordinate, VoxelSample prepared);
		void CommitGeneratedOriginalState() noexcept;

		VoxelWorldConfig m_Config{};
		LogicalDomainMetrics m_LogicalDomainMetrics{};
		SampleAabb m_LogicalSampleBounds{};
		ChunkMap m_Chunks;
		std::uint64_t m_WorldVoxelRevision = 0;
		std::uint64_t m_SurfaceStateRevision = 0;
		bool m_OriginalStateSealed = false;
	};
}
