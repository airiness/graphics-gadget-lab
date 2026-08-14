#pragma once

#include "Application/Lab/NapaVoxel/NapaVoxelRaycast.h"

#include "NapaVoxelCore/Edit/SphereEdit.h"
#include "NapaVoxelCore/World/Coordinates.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <variant>

namespace gglab
{
	enum class NapaVoxelCommandType : std::uint8_t
	{
		FireRay = 0,
		MoveProbeRay,
		RestoreAll,
		RestoreProbeChunk,
		ScriptedBoundaryShot,
	};

	enum class NapaVoxelCommandQueueError : std::uint8_t
	{
		None = 0,
		Empty,
		InvalidRay,
		InvalidEdit,
		AllocationFailure,
		EnqueueSerialExhausted,
		OperationSerialExhausted,
		HostPreparationFailed,
		PublicationSerialExhausted,
	};

	struct NapaVoxelFireParameters
	{
		double m_Radius = 0.0;
		double m_Strength = 0.0;
		napa::voxel::VoxelEditMaterialRules m_MaterialRules{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelFireParameters& lhs, const NapaVoxelFireParameters& rhs) noexcept
		{
			return lhs.m_Radius == rhs.m_Radius && lhs.m_Strength == rhs.m_Strength &&
				lhs.m_MaterialRules.m_DamagePerHit == rhs.m_MaterialRules.m_DamagePerHit &&
				lhs.m_MaterialRules.m_StoneBreakThreshold ==
				rhs.m_MaterialRules.m_StoneBreakThreshold;
		}
	};

	struct NapaVoxelFireRayCommand
	{
		NapaVoxelRay m_Ray{};
		NapaVoxelFireParameters m_Parameters{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelFireRayCommand&, const NapaVoxelFireRayCommand&) noexcept = default;
	};

	struct NapaVoxelMoveProbeRayCommand
	{
		NapaVoxelRay m_Ray{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelMoveProbeRayCommand&,
			const NapaVoxelMoveProbeRayCommand&) noexcept = default;
	};

	struct NapaVoxelRestoreAllCommand
	{
		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelRestoreAllCommand&, const NapaVoxelRestoreAllCommand&) noexcept = default;
	};

	struct NapaVoxelRestoreProbeChunkCommand
	{
		napa::voxel::ChunkCoord m_Chunk{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelRestoreProbeChunkCommand&,
			const NapaVoxelRestoreProbeChunkCommand&) noexcept = default;
	};

	struct NapaVoxelScriptedBoundaryShotCommand
	{
		napa::voxel::SphereEditRequest m_Edit{};

		[[nodiscard]] friend constexpr bool operator==(
			const NapaVoxelScriptedBoundaryShotCommand& lhs,
			const NapaVoxelScriptedBoundaryShotCommand& rhs) noexcept
		{
			return lhs.m_Edit.m_Brush.m_CenterWorld == rhs.m_Edit.m_Brush.m_CenterWorld &&
				lhs.m_Edit.m_Brush.m_Radius == rhs.m_Edit.m_Brush.m_Radius &&
				lhs.m_Edit.m_Brush.m_Strength == rhs.m_Edit.m_Brush.m_Strength &&
				lhs.m_Edit.m_MaterialRules.m_DamagePerHit ==
				rhs.m_Edit.m_MaterialRules.m_DamagePerHit &&
				lhs.m_Edit.m_MaterialRules.m_StoneBreakThreshold ==
				rhs.m_Edit.m_MaterialRules.m_StoneBreakThreshold;
		}
	};

	using NapaVoxelCommandData = std::variant<
		NapaVoxelFireRayCommand,
		NapaVoxelMoveProbeRayCommand,
		NapaVoxelRestoreAllCommand,
		NapaVoxelRestoreProbeChunkCommand,
		NapaVoxelScriptedBoundaryShotCommand>;

	struct NapaVoxelCommand
	{
		std::uint64_t m_EnqueueSerial = 0;
		NapaVoxelCommandData m_Data{};

		[[nodiscard]] NapaVoxelCommandType GetType() const noexcept;
		[[nodiscard]] friend bool operator==(
			const NapaVoxelCommand&, const NapaVoxelCommand&) noexcept = default;
	};

	struct NapaVoxelDequeuedCommand
	{
		std::uint64_t m_OperationSerial = 0;
		NapaVoxelCommand m_Command{};

		[[nodiscard]] friend bool operator==(
			const NapaVoxelDequeuedCommand&, const NapaVoxelDequeuedCommand&) noexcept = default;
	};

	struct NapaVoxelCommandQueueSerialState
	{
		std::uint64_t m_LastEnqueueSerial = 0;
		std::uint64_t m_LastOperationSerial = 0;
	};

	class NapaVoxelCommandQueue final
	{
	public:
		explicit NapaVoxelCommandQueue(NapaVoxelCommandQueueSerialState serialState = {}) noexcept;

		[[nodiscard]] NapaVoxelCommandQueueError EnqueueFireRay(
			const NapaVoxelRay& ray, const NapaVoxelFireParameters& parameters) noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError EnqueueMoveProbeRay(
			const NapaVoxelRay& ray) noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError EnqueueRestoreAll() noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError EnqueueRestoreProbeChunk(
			napa::voxel::ChunkCoord chunk) noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError EnqueueScriptedBoundaryShot(
			const napa::voxel::SphereEditRequest& edit) noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError Dequeue(
			NapaVoxelDequeuedCommand& command) noexcept;
		void Freeze(NapaVoxelCommandQueueError error) noexcept;

		void ResetForNewSession() noexcept;
		[[nodiscard]] bool IsTerminal() const noexcept
		{
			return m_TerminalError != NapaVoxelCommandQueueError::None;
		}
		[[nodiscard]] NapaVoxelCommandQueueError GetTerminalError() const noexcept
		{
			return m_TerminalError;
		}
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Commands.empty(); }
		[[nodiscard]] std::size_t GetSize() const noexcept { return m_Commands.size(); }
		[[nodiscard]] std::uint64_t GetLastEnqueueSerial() const noexcept
		{
			return m_LastEnqueueSerial;
		}
		[[nodiscard]] std::uint64_t GetLastOperationSerial() const noexcept
		{
			return m_LastOperationSerial;
		}

	private:
		[[nodiscard]] NapaVoxelCommandQueueError Enqueue(NapaVoxelCommandData data) noexcept;
		[[nodiscard]] NapaVoxelCommandQueueError CheckTerminal() const noexcept;

		std::deque<NapaVoxelCommand> m_Commands;
		std::uint64_t m_LastEnqueueSerial = 0;
		std::uint64_t m_LastOperationSerial = 0;
		NapaVoxelCommandQueueError m_TerminalError = NapaVoxelCommandQueueError::None;
	};

	[[nodiscard]] napa::voxel::ValidationResult PrepareNapaVoxelFireEditRequest(
		const NapaVoxelFireRayCommand& command, napa::voxel::Double3 visibleHitPosition,
		napa::voxel::SphereEditRequest& request) noexcept;
}
