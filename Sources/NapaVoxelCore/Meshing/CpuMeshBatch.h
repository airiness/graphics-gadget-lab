#pragma once

#include "NapaVoxelCore/Meshing/BoundaryContour.h"
#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/Meshing/WorldMeshHash.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorld.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace napa::voxel
{
	struct CpuMeshBatch;
	class PendingCpuMeshBatch;
	class VisibleMeshSet;

	[[nodiscard]] ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
		std::uint64_t targetWorldVoxelRevision, std::span<const ChunkCoord> requestedChunks,
		CpuMeshBatch& batch);
	[[nodiscard]] ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
		const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
	[[nodiscard]] ValidationResult PublishCpuMeshBatch(
		std::unique_ptr<PendingCpuMeshBatch>& pending, VisibleMeshSet& visible) noexcept;
	[[nodiscard]] ValidationResult ComputeVisibleWorldMeshHash(
		const VisibleMeshSet& visible, WorldMeshValidationResult& result);

	struct CpuMeshBatch
	{
		VoxelWorldConfig m_Config{};
		std::uint64_t m_TargetWorldVoxelRevision = 0;
		std::vector<ChunkCoord> m_RequestedChunks;
		std::vector<ChunkMeshRecord> m_Candidates;
	};

	class PendingCpuMeshBatch final
	{
	private:
		struct ConstructionToken
		{
		};

		struct State
		{
			bool m_BaseWasPublished = false;
			std::uint64_t m_BaseVisibleWorldRevision = 0;
			WorldMeshValidationResult m_BaseWorldMeshValidation{};
			BoundaryContourValidationResult m_BaseBoundaryValidation{};

			VoxelWorldConfig m_Config{};
			std::uint64_t m_TargetWorldVoxelRevision = 0;
			std::uint64_t m_CandidateChunkCount = 0;
			std::vector<ChunkMeshRecord> m_Chunks;
			WorldMeshValidationResult m_WorldMeshValidation{};
			BoundaryContourValidationResult m_BoundaryValidation{};
		};

	public:
		PendingCpuMeshBatch(ConstructionToken, State state) noexcept;

		PendingCpuMeshBatch(const PendingCpuMeshBatch&) = delete;
		PendingCpuMeshBatch& operator=(const PendingCpuMeshBatch&) = delete;

		[[nodiscard]] std::uint64_t GetTargetWorldVoxelRevision() const noexcept;
		[[nodiscard]] std::uint64_t GetCandidateChunkCount() const noexcept;
		[[nodiscard]] std::span<const ChunkMeshRecord> GetChunks() const noexcept;
		[[nodiscard]] const WorldMeshValidationResult& GetWorldMeshValidation() const noexcept;
		[[nodiscard]] const BoundaryContourValidationResult&
			GetBoundaryValidation() const noexcept;

	private:
		friend ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
			const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
		friend ValidationResult PublishCpuMeshBatch(
			std::unique_ptr<PendingCpuMeshBatch>& pending, VisibleMeshSet& visible) noexcept;

		State m_State;
	};

	class VisibleMeshSet final
	{
	private:
		struct State
		{
			bool m_HasPublishedMeshes = false;
			VoxelWorldConfig m_Config{};
			std::uint64_t m_VisibleWorldRevision = 0;
			std::vector<ChunkMeshRecord> m_Chunks;
			WorldMeshValidationResult m_WorldMeshValidation{};
			BoundaryContourValidationResult m_BoundaryValidation{};
		};

	public:
		VisibleMeshSet() = default;
		VisibleMeshSet(VisibleMeshSet&&) noexcept = default;
		VisibleMeshSet& operator=(VisibleMeshSet&&) noexcept = default;

		VisibleMeshSet(const VisibleMeshSet&) = delete;
		VisibleMeshSet& operator=(const VisibleMeshSet&) = delete;

		[[nodiscard]] bool HasPublishedMeshes() const noexcept;
		[[nodiscard]] const VoxelWorldConfig& GetConfig() const noexcept;
		[[nodiscard]] std::uint64_t GetVisibleWorldRevision() const noexcept;
		[[nodiscard]] std::span<const ChunkMeshRecord> GetChunks() const noexcept;
		[[nodiscard]] const WorldMeshValidationResult& GetWorldMeshValidation() const noexcept;
		[[nodiscard]] const BoundaryContourValidationResult&
			GetBoundaryValidation() const noexcept;

	private:
		friend ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
			const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
		friend ValidationResult PublishCpuMeshBatch(
			std::unique_ptr<PendingCpuMeshBatch>& pending, VisibleMeshSet& visible) noexcept;
		friend ValidationResult ComputeVisibleWorldMeshHash(
			const VisibleMeshSet& visible, WorldMeshValidationResult& result);

		State m_State;
	};
}
