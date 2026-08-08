#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"

#include <limits>
#include <type_traits>

namespace gglab
{
	static_assert(static_cast<std::size_t>(NapaVoxelCommandType::FireRay) == 0);
	static_assert(static_cast<std::size_t>(NapaVoxelCommandType::MoveProbeRay) == 1);
	static_assert(static_cast<std::size_t>(NapaVoxelCommandType::RestoreAll) == 2);
	static_assert(static_cast<std::size_t>(NapaVoxelCommandType::RestoreProbeChunk) == 3);
	static_assert(static_cast<std::size_t>(NapaVoxelCommandType::ScriptedBoundaryShot) == 4);
	static_assert(std::is_nothrow_move_constructible_v<NapaVoxelCommandData>);
	static_assert(std::is_nothrow_move_assignable_v<NapaVoxelCommandData>);

	NapaVoxelCommandType NapaVoxelCommand::GetType() const noexcept
	{
		return static_cast<NapaVoxelCommandType>(m_Data.index());
	}

	NapaVoxelCommandQueue::NapaVoxelCommandQueue(
		NapaVoxelCommandQueueSerialState serialState) noexcept :
		m_LastEnqueueSerial(serialState.m_LastEnqueueSerial),
		m_LastOperationSerial(serialState.m_LastOperationSerial)
	{
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::EnqueueFireRay(
		const NapaVoxelRay& ray, const napa::voxel::SphereEditRequest& edit) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		if (!IsValidNapaVoxelRay(ray))
		{
			return NapaVoxelCommandQueueError::InvalidRay;
		}
		if (napa::voxel::ValidateEdit(edit).Failed())
		{
			return NapaVoxelCommandQueueError::InvalidEdit;
		}
		return Enqueue(NapaVoxelFireRayCommand{ ray, edit });
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::EnqueueMoveProbeRay(
		const NapaVoxelRay& ray) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		if (!IsValidNapaVoxelRay(ray))
		{
			return NapaVoxelCommandQueueError::InvalidRay;
		}
		return Enqueue(NapaVoxelMoveProbeRayCommand{ ray });
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::EnqueueRestoreAll() noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		return Enqueue(NapaVoxelRestoreAllCommand{});
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::EnqueueRestoreProbeChunk(
		napa::voxel::ChunkCoord chunk) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		return Enqueue(NapaVoxelRestoreProbeChunkCommand{ chunk });
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::EnqueueScriptedBoundaryShot(
		const napa::voxel::SphereEditRequest& edit) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		if (napa::voxel::ValidateEdit(edit).Failed())
		{
			return NapaVoxelCommandQueueError::InvalidEdit;
		}
		return Enqueue(NapaVoxelScriptedBoundaryShotCommand{ edit });
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::Dequeue(
		NapaVoxelDequeuedCommand& command) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		if (m_Commands.empty())
		{
			return NapaVoxelCommandQueueError::Empty;
		}
		if (m_LastOperationSerial == std::numeric_limits<std::uint64_t>::max())
		{
			m_TerminalError = NapaVoxelCommandQueueError::OperationSerialExhausted;
			return m_TerminalError;
		}

		NapaVoxelDequeuedCommand prepared{
			.m_OperationSerial = m_LastOperationSerial + 1,
			.m_Command = std::move(m_Commands.front()),
		};
		m_Commands.pop_front();
		m_LastOperationSerial = prepared.m_OperationSerial;
		command = std::move(prepared);
		return NapaVoxelCommandQueueError::None;
	}

	void NapaVoxelCommandQueue::ResetForNewSession() noexcept
	{
		m_Commands.clear();
		m_LastEnqueueSerial = 0;
		m_LastOperationSerial = 0;
		m_TerminalError = NapaVoxelCommandQueueError::None;
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::Enqueue(NapaVoxelCommandData data) noexcept
	{
		if (const NapaVoxelCommandQueueError terminal = CheckTerminal();
			terminal != NapaVoxelCommandQueueError::None)
		{
			return terminal;
		}
		if (m_LastEnqueueSerial == std::numeric_limits<std::uint64_t>::max())
		{
			m_TerminalError = NapaVoxelCommandQueueError::EnqueueSerialExhausted;
			return m_TerminalError;
		}

		const std::uint64_t enqueueSerial = m_LastEnqueueSerial + 1;
		try
		{
			m_Commands.push_back({
				.m_EnqueueSerial = enqueueSerial,
				.m_Data = std::move(data),
				});
		}
		catch (...)
		{
			return NapaVoxelCommandQueueError::AllocationFailure;
		}
		m_LastEnqueueSerial = enqueueSerial;
		return NapaVoxelCommandQueueError::None;
	}

	NapaVoxelCommandQueueError NapaVoxelCommandQueue::CheckTerminal() const noexcept
	{
		return m_TerminalError;
	}
}
