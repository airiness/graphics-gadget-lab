#include "NapaVoxelCore/World/VoxelWorld.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace napa::voxel
{
	ValidationResult VoxelWorld::Create(
		const VoxelWorldConfig& config,
		std::unique_ptr<VoxelWorld>& world)
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}

		LogicalDomainMetrics metrics{};
		const ValidationResult metricsResult =
			ComputeLogicalDomainMetrics(config, metrics);
		if (metricsResult.Failed())
		{
			return metricsResult;
		}

		SampleAabb sampleBounds{};
		const ValidationResult boundsResult =
			LogicalCellBoundsToSampleBounds(
				config.m_LogicalCellBounds,
				sampleBounds);
		if (boundsResult.Failed())
		{
			return boundsResult;
		}

		std::unique_ptr<VoxelWorld> created{
			new VoxelWorld(config, metrics, sampleBounds),
		};
		world = std::move(created);
		return {};
	}

	VoxelWorld::VoxelWorld(
		const VoxelWorldConfig& config,
		const LogicalDomainMetrics& metrics,
		SampleAabb logicalSampleBounds)
		: m_Config(config)
		, m_LogicalDomainMetrics(metrics)
		, m_LogicalSampleBounds(logicalSampleBounds)
	{
	}

	const VoxelWorldConfig& VoxelWorld::GetConfig() const noexcept
	{
		return m_Config;
	}

	const LogicalDomainMetrics& VoxelWorld::GetLogicalDomainMetrics()
		const noexcept
	{
		return m_LogicalDomainMetrics;
	}

	SampleAabb VoxelWorld::GetLogicalSampleBounds() const noexcept
	{
		return m_LogicalSampleBounds;
	}

	std::size_t VoxelWorld::GetResidentChunkCount() const noexcept
	{
		return m_Chunks.size();
	}

	std::uint64_t VoxelWorld::GetWorldVoxelRevision() const noexcept
	{
		return m_WorldVoxelRevision;
	}

	const VoxelChunk* VoxelWorld::FindChunk(ChunkCoord chunk) const noexcept
	{
		const auto iterator = m_Chunks.find(chunk);
		return iterator != m_Chunks.end()
			? iterator->second.get()
			: nullptr;
	}

	ValidationResult VoxelWorld::EnsureChunkAllocated(
		ChunkCoord chunk,
		bool& allocated)
	{
		if (!m_LogicalDomainMetrics.m_SampleOwnerChunkBounds.Contains(chunk))
		{
			return {
				ValidationError::ChunkOutsideLogicalSampleDomain,
			};
		}

		VoxelChunk* result = nullptr;
		return FindOrCreateChunk(chunk, result, allocated);
	}

	ValidationResult VoxelWorld::ReadOriginalSample(
		SampleCoord coordinate,
		VoxelSample& sample) const noexcept
	{
		return ReadSample(coordinate, sample, true);
	}

	ValidationResult VoxelWorld::ReadCurrentSample(
		SampleCoord coordinate,
		VoxelSample& sample) const noexcept
	{
		return ReadSample(coordinate, sample, false);
	}

	ValidationResult VoxelWorld::WriteOriginalAndCurrentSample(
		SampleCoord coordinate,
		VoxelSample input,
		bool& changed)
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end() &&
			prepared == DefaultVoxelSample)
		{
			changed = false;
			return {};
		}

		if (iterator != m_Chunks.end())
		{
			VoxelSample original{};
			VoxelSample current{};
			const ValidationResult originalResult =
				iterator->second->ReadOriginalSample(
					address.m_Local,
					original);
			const ValidationResult currentResult =
				iterator->second->ReadCurrentSample(
					address.m_Local,
					current);
			if (originalResult.Failed())
			{
				return originalResult;
			}
			if (currentResult.Failed())
			{
				return currentResult;
			}
			if (original == prepared && current == prepared)
			{
				changed = false;
				return {};
			}
		}

		if (m_WorldVoxelRevision ==
			std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		VoxelChunk* chunk = nullptr;
		bool allocated = false;
		const ValidationResult allocationResult = FindOrCreateChunk(
			address.m_Owner,
			chunk,
			allocated);
		if (allocationResult.Failed())
		{
			return allocationResult;
		}
		static_cast<void>(allocated);
		bool chunkChanged = false;
		const ValidationResult writeResult =
			chunk->WriteOriginalAndCurrentSample(
				address.m_Local,
				prepared,
				chunkChanged);
		if (writeResult.Failed())
		{
			return writeResult;
		}
		if (!chunkChanged)
		{
			changed = false;
			return {};
		}

		++m_WorldVoxelRevision;
		changed = true;
		return {};
	}

	ValidationResult VoxelWorld::WriteCurrentSample(
		SampleCoord coordinate,
		VoxelSample input,
		bool& changed)
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end() &&
			prepared == DefaultVoxelSample)
		{
			changed = false;
			return {};
		}

		if (iterator != m_Chunks.end())
		{
			VoxelSample current{};
			const ValidationResult readResult =
				iterator->second->ReadCurrentSample(
					address.m_Local,
					current);
			if (readResult.Failed())
			{
				return readResult;
			}
			if (current == prepared)
			{
				changed = false;
				return {};
			}
		}

		if (m_WorldVoxelRevision ==
			std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		VoxelChunk* chunk = nullptr;
		bool allocated = false;
		const ValidationResult allocationResult = FindOrCreateChunk(
			address.m_Owner,
			chunk,
			allocated);
		if (allocationResult.Failed())
		{
			return allocationResult;
		}
		static_cast<void>(allocated);
		bool chunkChanged = false;
		const ValidationResult writeResult =
			chunk->WriteCurrentSample(
				address.m_Local,
				prepared,
				chunkChanged);
		if (writeResult.Failed())
		{
			return writeResult;
		}
		if (!chunkChanged)
		{
			changed = false;
			return {};
		}

		++m_WorldVoxelRevision;
		changed = true;
		return {};
	}

	ValidationResult VoxelWorld::ResolveLogicalSample(
		SampleCoord coordinate,
		OwnedSampleAddress& address) const noexcept
	{
		if (!m_LogicalSampleBounds.Contains(coordinate))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		return ResolveSampleOwner(
			coordinate,
			m_Config.m_ChunkCellCount,
			address);
	}

	ValidationResult VoxelWorld::ReadSample(
		SampleCoord coordinate,
		VoxelSample& sample,
		bool original) const noexcept
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		const auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end())
		{
			sample = DefaultVoxelSample;
			return {};
		}

		return original
			? iterator->second->ReadOriginalSample(address.m_Local, sample)
			: iterator->second->ReadCurrentSample(address.m_Local, sample);
	}

	ValidationResult VoxelWorld::FindOrCreateChunk(
		ChunkCoord coordinate,
		VoxelChunk*& chunk,
		bool& allocated)
	{
		const auto existing = m_Chunks.find(coordinate);
		if (existing != m_Chunks.end())
		{
			chunk = existing->second.get();
			allocated = false;
			return {};
		}

		std::unique_ptr<VoxelChunk> created;
		const ValidationResult createResult = VoxelChunk::Create(
			m_Config.m_ChunkCellCount,
			created);
		if (createResult.Failed())
		{
			return createResult;
		}

		const auto [iterator, inserted] = m_Chunks.try_emplace(
			coordinate,
			std::move(created));
		chunk = iterator->second.get();
		allocated = inserted;
		return {};
	}
}
