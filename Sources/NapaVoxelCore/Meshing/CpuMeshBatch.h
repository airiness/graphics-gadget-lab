#pragma once

#include "NapaVoxelCore/Meshing/BoundaryContour.h"
#include "NapaVoxelCore/Meshing/ChunkMeshRecord.h"
#include "NapaVoxelCore/Meshing/WorldMeshHash.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorld.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace napa::voxel
{
	struct CpuMeshBatch;
	struct VoxelMutationResult;
	class PendingCpuMeshBatch;
	class PreparedCpuMeshPublication;
	class PendingDataOnlyPublication;
	class VisibleMeshSet;
	class CpuMeshReplacementView;

	[[nodiscard]] ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
		std::uint64_t targetWorldVoxelRevision, std::span<const ChunkCoord> requestedChunks,
		CpuMeshBatch& batch);
	[[nodiscard]] ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
		const VoxelMutationResult& mutation, CpuMeshBatch& batch);
	[[nodiscard]] ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
		const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
	[[nodiscard]] ValidationResult PrepareCpuMeshBatchPublication(
		std::unique_ptr<PendingCpuMeshBatch>& pending, const VisibleMeshSet& visible,
		std::unique_ptr<PreparedCpuMeshPublication>& publication);
	void CommitCpuMeshBatchPublication(
		std::unique_ptr<PreparedCpuMeshPublication>& publication, VisibleMeshSet& visible) noexcept;
	[[nodiscard]] ValidationResult ComputeVisibleWorldMeshHash(
		const VisibleMeshSet& visible, WorldMeshValidationResult& result);

	struct CpuMeshBatch
	{
		VoxelWorldConfig m_Config{};
		std::uint64_t m_BaseWorldVoxelRevision = 0;
		std::uint64_t m_TargetWorldVoxelRevision = 0;
		std::vector<ChunkCoord> m_RequestedChunks;
		std::vector<ChunkMeshRecord> m_Candidates;

	private:
		friend ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
			std::uint64_t targetWorldVoxelRevision,
			std::span<const ChunkCoord> requestedChunks, CpuMeshBatch& batch);
		friend ValidationResult BuildCpuMeshBatch(const VoxelWorld& world,
			const VoxelMutationResult& mutation, CpuMeshBatch& batch);
		friend ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
			const VisibleMeshSet& visible,
			std::unique_ptr<PendingCpuMeshBatch>& pending);

		std::uint64_t m_TargetSurfaceStateRevision = 0;
	};

	class CpuMeshReplacementView final
	{
	public:
		[[nodiscard]] std::size_t size() const noexcept;
		[[nodiscard]] bool empty() const noexcept;
		[[nodiscard]] const ChunkMeshRecord& operator[](std::size_t index) const noexcept;

	private:
		friend class PendingCpuMeshBatch;

		CpuMeshReplacementView(std::span<const ChunkMeshRecord> chunks,
			std::span<const std::size_t> indices) noexcept;

		std::span<const ChunkMeshRecord> m_Chunks;
		std::span<const std::size_t> m_Indices;
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
			std::uint64_t m_BaseSurfaceStateRevision = 0;
			WorldMeshValidationResult m_BaseWorldMeshValidation{};
			BoundaryContourValidationResult m_BaseBoundaryValidation{};

			VoxelWorldConfig m_Config{};
			std::uint64_t m_TargetWorldVoxelRevision = 0;
			std::uint64_t m_TargetSurfaceStateRevision = 0;
			std::uint64_t m_CandidateChunkCount = 0;
			std::vector<ChunkMeshRecord> m_Chunks;
			std::vector<std::size_t> m_ReplacementChunkIndices;
			WorldMeshValidationResult m_WorldMeshValidation{};
			BoundaryContourValidationResult m_BoundaryValidation{};
		};

	public:
		PendingCpuMeshBatch(ConstructionToken, State state) noexcept;

		PendingCpuMeshBatch(const PendingCpuMeshBatch&) = delete;
		PendingCpuMeshBatch& operator=(const PendingCpuMeshBatch&) = delete;

		[[nodiscard]] const VoxelWorldConfig& GetConfig() const noexcept;
		[[nodiscard]] std::uint64_t GetBaseWorldVoxelRevision() const noexcept;
		[[nodiscard]] std::uint64_t GetTargetWorldVoxelRevision() const noexcept;
		[[nodiscard]] std::uint64_t GetCandidateChunkCount() const noexcept;
		[[nodiscard]] std::span<const ChunkMeshRecord> GetChunks() const noexcept;
		[[nodiscard]] CpuMeshReplacementView GetReplacementChunks() const noexcept;
		[[nodiscard]] const WorldMeshValidationResult& GetWorldMeshValidation() const noexcept;
		[[nodiscard]] const BoundaryContourValidationResult&
			GetBoundaryValidation() const noexcept;

	private:
		friend ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
			const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
		friend ValidationResult PrepareCpuMeshBatchPublication(
			std::unique_ptr<PendingCpuMeshBatch>& pending, const VisibleMeshSet& visible,
			std::unique_ptr<PreparedCpuMeshPublication>& publication);
		friend void CommitCpuMeshBatchPublication(
			std::unique_ptr<PreparedCpuMeshPublication>& publication,
			VisibleMeshSet& visible) noexcept;

		State m_State;
	};

	class PreparedCpuMeshPublication final
	{
	private:
		struct ConstructionToken
		{
		};

	public:
		explicit PreparedCpuMeshPublication(ConstructionToken) noexcept {}
		PreparedCpuMeshPublication(const PreparedCpuMeshPublication&) = delete;
		PreparedCpuMeshPublication& operator=(const PreparedCpuMeshPublication&) = delete;

	private:
		friend ValidationResult PrepareCpuMeshBatchPublication(
			std::unique_ptr<PendingCpuMeshBatch>& pending, const VisibleMeshSet& visible,
			std::unique_ptr<PreparedCpuMeshPublication>& publication);
		friend void CommitCpuMeshBatchPublication(
			std::unique_ptr<PreparedCpuMeshPublication>& publication,
			VisibleMeshSet& visible) noexcept;

		std::unique_ptr<PendingCpuMeshBatch> m_Pending;
	};

	class VisibleMeshSet final
	{
	private:
		struct State
		{
			bool m_HasPublishedMeshes = false;
			VoxelWorldConfig m_Config{};
			std::uint64_t m_VisibleWorldRevision = 0;
			std::uint64_t m_SurfaceStateRevision = 0;
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
		[[nodiscard]] std::uint64_t GetSurfaceStateRevision() const noexcept;
		[[nodiscard]] std::span<const ChunkMeshRecord> GetChunks() const noexcept;
		[[nodiscard]] const WorldMeshValidationResult& GetWorldMeshValidation() const noexcept;
		[[nodiscard]] const BoundaryContourValidationResult&
			GetBoundaryValidation() const noexcept;

	private:
		friend ValidationResult ValidateCpuMeshBatch(const CpuMeshBatch& batch,
			const VisibleMeshSet& visible, std::unique_ptr<PendingCpuMeshBatch>& pending);
		friend ValidationResult PrepareCpuMeshBatchPublication(
			std::unique_ptr<PendingCpuMeshBatch>& pending, const VisibleMeshSet& visible,
			std::unique_ptr<PreparedCpuMeshPublication>& publication);
		friend void CommitCpuMeshBatchPublication(
			std::unique_ptr<PreparedCpuMeshPublication>& publication,
			VisibleMeshSet& visible) noexcept;
		friend ValidationResult ComputeVisibleWorldMeshHash(
			const VisibleMeshSet& visible, WorldMeshValidationResult& result);
		friend void CommitDataOnlyPublication(
			std::unique_ptr<PendingDataOnlyPublication>& pending,
			VisibleMeshSet& visible) noexcept;

		State m_State;
	};
}
