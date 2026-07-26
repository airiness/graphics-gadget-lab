#include "NapaVoxelCore/World/VoxelChunk.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] constexpr std::size_t ComputeSampleCount(
			std::uint32_t chunkCellCount) noexcept
		{
			if (!IsSupportedChunkCellCount(chunkCellCount))
			{
				return 0;
			}

			const std::size_t count = chunkCellCount;
			return count * count * count;
		}
	}

	ValidationResult VoxelChunk::Create(
		std::uint32_t chunkCellCount,
		std::unique_ptr<VoxelChunk>& chunk)
	{
		if (!IsSupportedChunkCellCount(chunkCellCount))
		{
			return { ValidationError::InvalidChunkCellCount };
		}

		std::unique_ptr<VoxelChunk> created =
			std::make_unique<VoxelChunk>(
				ConstructionToken{},
				chunkCellCount);
		chunk = std::move(created);
		return {};
	}

	VoxelChunk::VoxelChunk(
		ConstructionToken,
		std::uint32_t chunkCellCount)
		: m_ChunkCellCount(chunkCellCount)
		, m_OriginalSamples(
			ComputeSampleCount(chunkCellCount),
			DefaultVoxelSample)
		, m_CurrentSamples(
			ComputeSampleCount(chunkCellCount),
			DefaultVoxelSample)
	{
	}

	std::uint32_t VoxelChunk::GetChunkCellCount() const noexcept
	{
		return m_ChunkCellCount;
	}

	std::size_t VoxelChunk::GetSampleCount() const noexcept
	{
		return m_CurrentSamples.size();
	}

	std::uint64_t VoxelChunk::GetVoxelRevision() const noexcept
	{
		return m_VoxelRevision;
	}

	ValidationResult VoxelChunk::ReadOriginalSample(
		LocalCoord local,
		VoxelSample& sample) const noexcept
	{
		std::size_t flatIndex = 0;
		const ValidationResult indexResult =
			ResolveFlatIndex(local, flatIndex);
		if (indexResult.Failed())
		{
			return indexResult;
		}

		sample = m_OriginalSamples[flatIndex];
		return {};
	}

	ValidationResult VoxelChunk::ReadCurrentSample(
		LocalCoord local,
		VoxelSample& sample) const noexcept
	{
		std::size_t flatIndex = 0;
		const ValidationResult indexResult =
			ResolveFlatIndex(local, flatIndex);
		if (indexResult.Failed())
		{
			return indexResult;
		}

		sample = m_CurrentSamples[flatIndex];
		return {};
	}

	ValidationResult VoxelChunk::WriteOriginalAndCurrentSample(
		LocalCoord local,
		VoxelSample input,
		bool& changed) noexcept
	{
		std::size_t flatIndex = 0;
		const ValidationResult indexResult =
			ResolveFlatIndex(local, flatIndex);
		if (indexResult.Failed())
		{
			return indexResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		if (m_OriginalSamples[flatIndex] == prepared &&
			m_CurrentSamples[flatIndex] == prepared)
		{
			changed = false;
			return {};
		}

		const ValidationResult revisionResult = AdvanceRevision();
		if (revisionResult.Failed())
		{
			return revisionResult;
		}

		m_OriginalSamples[flatIndex] = prepared;
		m_CurrentSamples[flatIndex] = prepared;
		changed = true;
		return {};
	}

	ValidationResult VoxelChunk::WriteCurrentSample(
		LocalCoord local,
		VoxelSample input,
		bool& changed) noexcept
	{
		std::size_t flatIndex = 0;
		const ValidationResult indexResult =
			ResolveFlatIndex(local, flatIndex);
		if (indexResult.Failed())
		{
			return indexResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		if (m_CurrentSamples[flatIndex] == prepared)
		{
			changed = false;
			return {};
		}

		const ValidationResult revisionResult = AdvanceRevision();
		if (revisionResult.Failed())
		{
			return revisionResult;
		}

		m_CurrentSamples[flatIndex] = prepared;
		changed = true;
		return {};
	}

	ValidationResult VoxelChunk::ResolveFlatIndex(
		LocalCoord local,
		std::size_t& flatIndex) const noexcept
	{
		const ValidationResult result =
			FlattenLocal(local, m_ChunkCellCount, flatIndex);
		if (result.Failed())
		{
			return result;
		}
		if (flatIndex >= m_CurrentSamples.size() ||
			flatIndex >= m_OriginalSamples.size())
		{
			return { ValidationError::FlatIndexOutOfRange };
		}
		return {};
	}

	ValidationResult VoxelChunk::AdvanceRevision() noexcept
	{
		if (m_VoxelRevision == std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		++m_VoxelRevision;
		return {};
	}

	ValidationResult VoxelChunk::RestoreCurrentSamples(
		std::span<const LocalCoord> coordinates,
		bool& changed) noexcept
	{
		bool hasDifference = false;
		for (const LocalCoord coordinate : coordinates)
		{
			std::size_t flatIndex = 0;
			const ValidationResult indexResult =
				ResolveFlatIndex(coordinate, flatIndex);
			if (indexResult.Failed())
			{
				return indexResult;
			}

			hasDifference =
				hasDifference ||
				m_CurrentSamples[flatIndex] !=
					m_OriginalSamples[flatIndex];
		}

		if (!hasDifference)
		{
			changed = false;
			return {};
		}

		const ValidationResult revisionResult = AdvanceRevision();
		if (revisionResult.Failed())
		{
			return revisionResult;
		}

		for (const LocalCoord coordinate : coordinates)
		{
			const std::size_t count = m_ChunkCellCount;
			const std::size_t flatIndex =
				(static_cast<std::size_t>(coordinate.m_Z) * count +
					coordinate.m_Y) *
					count +
				coordinate.m_X;
			m_CurrentSamples[flatIndex] = m_OriginalSamples[flatIndex];
		}

		changed = true;
		return {};
	}
}
